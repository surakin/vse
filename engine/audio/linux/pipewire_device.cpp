#include "audio/linux/pipewire_device.h"

#include "audio/format.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "os/os.h"
#include "sync/atomic.h"
#include "sync/thread.h"

#if ENGINE_AUDIO_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#endif

namespace engine_placeholder::audio
{

#if ENGINE_AUDIO_HAVE_PIPEWIRE

namespace
{

// The functions of libpipewire the backend uses, resolved by name when the library is loaded.
struct PwApi
{
    void (*init)(int*, char***) = nullptr;
    void (*deinit)(void) = nullptr;
    pw_thread_loop* (*thread_loop_new)(const char*, const spa_dict*) = nullptr;
    pw_loop* (*thread_loop_get_loop)(pw_thread_loop*) = nullptr;
    int (*thread_loop_start)(pw_thread_loop*) = nullptr;
    void (*thread_loop_stop)(pw_thread_loop*) = nullptr;
    void (*thread_loop_destroy)(pw_thread_loop*) = nullptr;
    void (*thread_loop_lock)(pw_thread_loop*) = nullptr;
    void (*thread_loop_unlock)(pw_thread_loop*) = nullptr;
    pw_properties* (*properties_new)(const char*, ...) = nullptr;
    int (*properties_set)(pw_properties*, const char*, const char*) = nullptr;
    pw_stream* (*stream_new_simple)(pw_loop*, const char*, pw_properties*, const pw_stream_events*, void*) = nullptr;
    int (*stream_connect)(pw_stream*, enum pw_direction, u32, enum pw_stream_flags, const spa_pod**, u32) = nullptr;
    int (*stream_set_active)(pw_stream*, bool) = nullptr;
    int (*stream_disconnect)(pw_stream*) = nullptr;
    void (*stream_destroy)(pw_stream*) = nullptr;
    pw_buffer* (*stream_dequeue_buffer)(pw_stream*) = nullptr;
    int (*stream_queue_buffer)(pw_stream*, pw_buffer*) = nullptr;
};

PwApi g_api;
bool g_loaded = false;

template <class F> bool Resolve(os::DynamicLibraryHandle library, const char* name, F* out)
{
    void* symbol = os::GetSymbol(library, name);
    if (symbol == nullptr)
    {
        return false;
    }
    MemCopy(out, &symbol, sizeof(symbol));
    return true;
}

bool LoadPipeWire()
{
    if (g_loaded)
    {
        return true;
    }
    os::DynamicLibraryHandle library = os::LoadLibrary(StringView("libpipewire-0.3.so.0"));
    if (library == nullptr)
    {
        return false;
    }
    PwApi api;
    const bool ok = Resolve(library, "pw_init", &api.init) && Resolve(library, "pw_deinit", &api.deinit) &&
                    Resolve(library, "pw_thread_loop_new", &api.thread_loop_new) &&
                    Resolve(library, "pw_thread_loop_get_loop", &api.thread_loop_get_loop) &&
                    Resolve(library, "pw_thread_loop_start", &api.thread_loop_start) &&
                    Resolve(library, "pw_thread_loop_stop", &api.thread_loop_stop) &&
                    Resolve(library, "pw_thread_loop_destroy", &api.thread_loop_destroy) &&
                    Resolve(library, "pw_thread_loop_lock", &api.thread_loop_lock) &&
                    Resolve(library, "pw_thread_loop_unlock", &api.thread_loop_unlock) &&
                    Resolve(library, "pw_properties_new", &api.properties_new) &&
                    Resolve(library, "pw_properties_set", &api.properties_set) &&
                    Resolve(library, "pw_stream_new_simple", &api.stream_new_simple) &&
                    Resolve(library, "pw_stream_connect", &api.stream_connect) &&
                    Resolve(library, "pw_stream_set_active", &api.stream_set_active) &&
                    Resolve(library, "pw_stream_disconnect", &api.stream_disconnect) &&
                    Resolve(library, "pw_stream_destroy", &api.stream_destroy) &&
                    Resolve(library, "pw_stream_dequeue_buffer", &api.stream_dequeue_buffer) &&
                    Resolve(library, "pw_stream_queue_buffer", &api.stream_queue_buffer);
    if (!ok)
    {
        os::UnloadLibrary(library);
        return false;
    }
    // The library stays loaded for the life of the process (its modules and the device's callbacks live in it).
    g_api = api;
    g_loaded = true;
    return true;
}

class PipeWireDevice final : public AudioDevice
{
public:
    ~PipeWireDevice() override { Close(); }

    void SetCallbacks(RenderCallback render, DeviceErrorCallback error, void* user) override
    {
        render_ = render;
        error_ = error;
        user_ = user;
    }

    DeviceStatus Open(const DeviceConfig& config) override
    {
        if (loop_ != nullptr)
        {
            return DeviceStatus::AlreadyOpen;
        }
        if (config.buffer_frames < 1 || config.buffer_frames > kMaxDeviceBufferFrames ||
            ChannelCount(config.layout) == 0 ||
            (config.sample_rate != 0 && (config.sample_rate < kMinSampleRate || config.sample_rate > kMaxSampleRate)))
        {
            return DeviceStatus::InvalidConfig;
        }
        if (!(config.name.Empty() || config.name == StringView("default") || config.name == StringView("pipewire")))
        {
            return DeviceStatus::NotFound;
        }
        if (!LoadPipeWire())
        {
            return DeviceStatus::NotFound;
        }
        g_api.init(nullptr, nullptr);  // counted: each device pairs it with a `deinit` in `Teardown`
        initialised_ = true;
        rate_ = config.sample_rate == 0 ? 48000 : config.sample_rate;
        channels_ = ChannelCount(config.layout);
        buffer_frames_ = config.buffer_frames;
        loop_ = g_api.thread_loop_new("engine-audio", nullptr);
        if (loop_ == nullptr)
        {
            g_api.deinit();
            initialised_ = false;
            return DeviceStatus::OutOfMemory;
        }
        char latency[48];
        FormatLatency(latency, sizeof(latency), buffer_frames_, rate_);
        pw_properties* props = g_api.properties_new(nullptr, nullptr);
        if (props == nullptr)
        {
            g_api.thread_loop_destroy(loop_);
            loop_ = nullptr;
            g_api.deinit();
            initialised_ = false;
            return DeviceStatus::OutOfMemory;
        }
        g_api.properties_set(props, PW_KEY_MEDIA_TYPE, "Audio");
        g_api.properties_set(props, PW_KEY_MEDIA_CATEGORY, "Playback");
        g_api.properties_set(props, PW_KEY_MEDIA_ROLE, "Game");
        g_api.properties_set(props, PW_KEY_NODE_LATENCY, latency);
        MemSet(&events_, 0, sizeof(events_));
        events_.version = PW_VERSION_STREAM_EVENTS;
        events_.state_changed = &PipeWireDevice::OnState;
        events_.process = &PipeWireDevice::OnProcess;
        state_.Store(static_cast<int>(PW_STREAM_STATE_UNCONNECTED));
        stream_ = g_api.stream_new_simple(g_api.thread_loop_get_loop(loop_), "engine", props, &events_, this);
        if (stream_ == nullptr)
        {
            g_api.thread_loop_destroy(loop_);
            loop_ = nullptr;
            g_api.deinit();
            initialised_ = false;
            return DeviceStatus::OutOfMemory;
        }
        u8 pod_buffer[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(pod_buffer, sizeof(pod_buffer));
        spa_audio_info_raw info;
        MemSet(&info, 0, sizeof(info));
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = rate_;
        info.channels = channels_;
        const spa_pod* params[1];
        params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
        g_api.thread_loop_lock(loop_);
        const int result = g_api.stream_connect(
            stream_, PW_DIRECTION_OUTPUT, PW_ID_ANY,
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                         PW_STREAM_FLAG_RT_PROCESS | PW_STREAM_FLAG_INACTIVE),
            params, 1);
        g_api.thread_loop_unlock(loop_);
        if (result < 0 || g_api.thread_loop_start(loop_) < 0)
        {
            Teardown();
            return DeviceStatus::NotFound;
        }
        // Wait for the server: paused (negotiated) is success, an error is NotFound (no server).
        for (u32 waited = 0; waited < 3000; waited += 5)
        {
            const int state = state_.Load();
            if (state == static_cast<int>(PW_STREAM_STATE_PAUSED) ||
                state == static_cast<int>(PW_STREAM_STATE_STREAMING))
            {
                break;
            }
            if (state == static_cast<int>(PW_STREAM_STATE_ERROR))
            {
                Teardown();
                return DeviceStatus::NotFound;
            }
            ThisThread::SleepForMilliseconds(5);
        }
        const int final_state = state_.Load();
        if (final_state != static_cast<int>(PW_STREAM_STATE_PAUSED) &&
            final_state != static_cast<int>(PW_STREAM_STATE_STREAMING))
        {
            Teardown();
            return DeviceStatus::NotFound;
        }
        info_.sample_rate = rate_;
        info_.layout = config.layout;
        info_.buffer_frames = buffer_frames_;
        info_.latency_frames = buffer_frames_ * 2;
        rendered_.Store(0);
        callbacks_.Store(0);
        slow_.Store(0);
        longest_.Store(0);
        return DeviceStatus::Ok;
    }

    DeviceStatus Start() override
    {
        if (loop_ == nullptr || render_ == nullptr)
        {
            return DeviceStatus::NotOpen;
        }
        if (running_.Load())
        {
            return DeviceStatus::Busy;
        }
        running_.Store(true);
        g_api.thread_loop_lock(loop_);
        g_api.stream_set_active(stream_, true);
        g_api.thread_loop_unlock(loop_);
        return DeviceStatus::Ok;
    }

    void Stop() override
    {
        if (!running_.Load() || loop_ == nullptr)
        {
            return;
        }
        // `process` runs on the server's thread: after `running_` is false a call that has not rendered yet will not,
        // and a call that is rendering is waited for, so nothing renders once `Stop` returns.
        running_.Store(false);
        g_api.thread_loop_lock(loop_);
        g_api.stream_set_active(stream_, false);
        g_api.thread_loop_unlock(loop_);
        while (in_callback_.Load())
        {
            ThisThread::SleepForMilliseconds(1);
        }
    }

    DeviceStatus Close() override
    {
        Stop();
        Teardown();
        return DeviceStatus::Ok;
    }

    DeviceInfo Info() const override { return info_; }
    bool IsOpen() const override { return loop_ != nullptr; }
    bool IsRunning() const override { return running_.Load(); }
    DeviceStatus Pump(u64) override { return DeviceStatus::Unsupported; }
    u64 FramesRendered() const override { return rendered_.Load(); }

    DeviceCounters Counters() const override
    {
        DeviceCounters c;
        c.callbacks = callbacks_.Load();
        c.slow_callbacks = slow_.Load();
        c.longest_callback_micros = longest_.Load();
        return c;
    }

private:
    static void FormatLatency(char* out, usize capacity, u32 frames, u32 rate)
    {
        // "<frames>/<rate>"
        char digits[24];
        usize n = 0;
        auto put = [&](u32 value)
        {
            u32 count = 0;
            do
            {
                digits[count++] = static_cast<char>('0' + value % 10);
                value /= 10;
            } while (value > 0 && count < 10);
            while (count > 0 && n + 1 < capacity)
            {
                out[n++] = digits[--count];
            }
        };
        put(frames);
        if (n + 1 < capacity)
        {
            out[n++] = '/';
        }
        put(rate);
        out[n] = 0;
    }

    void Teardown()
    {
        if (loop_ == nullptr)
        {
            return;
        }
        g_api.thread_loop_stop(loop_);
        if (stream_ != nullptr)
        {
            g_api.stream_destroy(stream_);
            stream_ = nullptr;
        }
        g_api.thread_loop_destroy(loop_);
        loop_ = nullptr;
        if (initialised_)
        {
            g_api.deinit();
            initialised_ = false;
        }
    }

    static void OnState(void* user, enum pw_stream_state, enum pw_stream_state state, const char*)
    {
        PipeWireDevice* self = static_cast<PipeWireDevice*>(user);
        self->state_.Store(static_cast<int>(state));
        if (state == PW_STREAM_STATE_ERROR && self->running_.Load() && self->error_ != nullptr)
        {
            self->error_(self->user_, DeviceStatus::Lost);
        }
    }

    static void OnProcess(void* user)
    {
        PipeWireDevice* self = static_cast<PipeWireDevice*>(user);
        self->in_callback_.Store(true);
        if (!self->running_.Load())
        {
            self->in_callback_.Store(false);
            pw_buffer* idle = g_api.stream_dequeue_buffer(self->stream_);
            if (idle != nullptr)
            {
                idle->buffer->datas[0].chunk->size = 0;
                g_api.stream_queue_buffer(self->stream_, idle);
            }
            return;
        }
        pw_buffer* buffer = g_api.stream_dequeue_buffer(self->stream_);
        if (buffer == nullptr)
        {
            self->in_callback_.Store(false);
            return;
        }
        spa_data& data = buffer->buffer->datas[0];
        const u32 stride = self->channels_ * static_cast<u32>(sizeof(f32));
        u32 frames = data.maxsize / stride;
        if (buffer->requested != 0 && buffer->requested < frames)
        {
            frames = static_cast<u32>(buffer->requested);
        }
        if (data.data == nullptr)
        {
            g_api.stream_queue_buffer(self->stream_, buffer);
            self->in_callback_.Store(false);
            return;
        }
        const u64 start = os::MonotonicTimeNanos();
        f32* out = static_cast<f32*>(data.data);
        u32 done = 0;
        while (done < frames)
        {
            const u32 chunk = frames - done < self->buffer_frames_ ? frames - done : self->buffer_frames_;
            self->render_(self->user_, out + static_cast<usize>(done) * self->channels_, chunk);
            done += chunk;
        }
        const u64 took = (os::MonotonicTimeNanos() - start) / 1000;
        data.chunk->offset = 0;
        data.chunk->stride = static_cast<i32>(stride);
        data.chunk->size = frames * stride;
        g_api.stream_queue_buffer(self->stream_, buffer);
        self->in_callback_.Store(false);
        self->callbacks_.FetchAdd(1);
        self->rendered_.FetchAdd(frames);
        if (took > static_cast<u64>(frames) * 1000000ull / self->rate_)
        {
            self->slow_.FetchAdd(1);
        }
        u64 longest = self->longest_.Load();
        while (took > longest && !self->longest_.CompareExchangeWeak(longest, took))
        {
        }
    }

    RenderCallback render_ = nullptr;
    DeviceErrorCallback error_ = nullptr;
    void* user_ = nullptr;
    bool initialised_ = false;
    pw_thread_loop* loop_ = nullptr;
    pw_stream* stream_ = nullptr;
    pw_stream_events events_;
    u32 rate_ = 48000;
    u32 channels_ = 2;
    u32 buffer_frames_ = 1024;
    DeviceInfo info_;
    Atomic<int> state_{0};
    Atomic<bool> running_{false};
    Atomic<bool> in_callback_{false};
    Atomic<u64> rendered_{0};
    Atomic<u64> callbacks_{0};
    Atomic<u64> slow_{0};
    Atomic<u64> longest_{0};
};

}  // namespace

bool PipeWireAvailable()
{
    return LoadPipeWire();
}

DeviceStatus EnumeratePipeWireDevices(Vector<DeviceDescription>* out)
{
    out->Clear();
    if (!LoadPipeWire())
    {
        return DeviceStatus::Ok;
    }
    DeviceDescription d;
    const char name[] = "pipewire";
    for (u32 i = 0; i < sizeof(name); ++i)
    {
        d.name[i] = name[i];
    }
    d.is_default = true;
    d.max_channels = 8;
    d.min_rate = kMinSampleRate;
    d.max_rate = kMaxSampleRate;
    out->Push(d);
    return DeviceStatus::Ok;
}

UniquePtr<AudioDevice> CreatePipeWireDevice()
{
    return MakeUnique<PipeWireDevice>();
}

#else  // built without the PipeWire headers: the backend reports itself absent

bool PipeWireAvailable()
{
    return false;
}

DeviceStatus EnumeratePipeWireDevices(Vector<DeviceDescription>* out)
{
    out->Clear();
    return DeviceStatus::Ok;
}

UniquePtr<AudioDevice> CreatePipeWireDevice()
{
    return CreateDevice(DeviceKind::Null);
}

#endif

}  // namespace engine_placeholder::audio
