// The audio half of the C API (Phase 26 Day 438).
#include "engine_c/audio_api.h"

#include "audio/clip.h"
#include "audio/delay.h"
#include "audio/filter.h"
#include "audio/snapshot.h"
#include "cstr/format.h"
#include "engine_c/engine_internal.h"
#include "engine_c/runtime.h"
#include "lang/scalar.h"
#include "lang/utility.h"
#include "sync/mutex.h"

namespace vse::engine_c
{

namespace
{

constexpr usize kAudioMaxClips = 4096;
constexpr usize kAudioMaxClipBytes = usize{512} * 1024 * 1024;
constexpr usize kAudioMaxBusName = 63;

u32 AudioSizeOf(const void* p)
{
    u32 size = 0;
    MemCopy(&size, p, sizeof(size));
    return size;
}

engine_status AudioCheckIn(const void* value, usize first_version, const char* what)
{
    if (value == nullptr)
    {
        SetLastError("invalid_argument: `%s` is NULL", what);
        return ENGINE_ERROR_INVALID_ARGUMENT;
    }
    if (AudioSizeOf(value) < first_version)
    {
        SetLastError("struct_size: %s.struct_size is smaller than the struct", what);
        return ENGINE_ERROR_STRUCT_SIZE;
    }
    return ENGINE_OK;
}

// Finds the engine's audio host; sets the failure text when the engine is stale or audio is not open.
engine_status AudioResolve(engine_handle engine, AudioHost** out, AudioHostPtr* keep)
{
    *out = nullptr;
    Runtime* runtime = nullptr;
    const engine_status status = ResolveRuntime(engine, &runtime);
    if (status != ENGINE_OK)
    {
        return status;
    }
    {
        // The share is taken under the lock `audio_close` takes, so the host outlives the call.
        LockGuard<Mutex> lock(runtime->audio_mutex);
        *keep = runtime->audio_owner;
    }
    if (keep->Get() == nullptr)
    {
        return Fail(ENGINE_ERROR_NOT_READY, "audio is not open");
    }
    *out = keep->Get();
    return ENGINE_OK;
}

bool AudioLayoutOf(u32 channels, audio::ChannelLayout* out)
{
    switch (channels)
    {
    case 1: *out = audio::ChannelLayout::Mono; return true;
    case 2: *out = audio::ChannelLayout::Stereo; return true;
    case 4: *out = audio::ChannelLayout::Quad; return true;
    case 6: *out = audio::ChannelLayout::Surround51; return true;
    case 8: *out = audio::ChannelLayout::Surround71; return true;
    default: return false;
    }
}

engine_status AudioFromMixer(audio::MixerStatus status)
{
    switch (status)
    {
    case audio::MixerStatus::Ok: return ENGINE_OK;
    case audio::MixerStatus::InvalidVoice: return Fail(ENGINE_ERROR_STALE_HANDLE, "the voice is gone");
    case audio::MixerStatus::InvalidParam: return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a value is out of range");
    case audio::MixerStatus::NoClip: return Fail(ENGINE_ERROR_STALE_HANDLE, "the clip is gone");
    case audio::MixerStatus::Full: return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the voice pool is full of better voices");
    case audio::MixerStatus::QueueFull: return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the command queue is full");
    case audio::MixerStatus::InvalidBus: return Fail(ENGINE_ERROR_NOT_FOUND, "no such bus");
    case audio::MixerStatus::Cycle: return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the bus would make a cycle");
    case audio::MixerStatus::NoBusSlot: return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "no bus slot is left");
    case audio::MixerStatus::OutOfMemory: return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "out of memory");
    case audio::MixerStatus::NoEffectSlot: return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "no effect slot is left");
    case audio::MixerStatus::InvalidConfig: return Fail(ENGINE_ERROR_UNSUPPORTED, "the mixer does not take this setup");
    }
    return Fail(ENGINE_ERROR_INTERNAL, "unknown mixer status");
}

u64 AudioPack(u32 generation, u32 slot)
{
    return (static_cast<u64>(generation) << 32) | (static_cast<u64>(slot) + 1);
}

audio::VoiceId AudioUnpackVoice(u64 id)
{
    audio::VoiceId voice;
    const u32 low = static_cast<u32>(id & 0xFFFFFFFFull);
    if (low != 0)
    {
        voice.index = low - 1;
        voice.generation = static_cast<u32>(id >> 32);
    }
    return voice;
}

bool AudioFinite(f32 v)
{
    return IsFinite(v);
}

}  // namespace

engine_status AudioOpenFn(engine_handle engine, const engine_audio_info* info)
{
    Runtime* runtime = nullptr;
    engine_status status = ResolveRuntime(engine, &runtime);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckIn(info, 24, "engine_audio_info");
    if (status != ENGINE_OK)
    {
        return status;
    }
    audio::MixerConfig config;
    if (!AudioLayoutOf(info->channels, &config.layout))
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "channels is not 1, 2, 4, 6 or 8");
    }
    if (info->sample_rate < 8000 || info->sample_rate > 384000 || info->max_voices > audio::kMaxVoices ||
        info->max_buses > audio::kMaxBuses)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "sample_rate, max_voices or max_buses is out of range");
    }
    config.sample_rate = info->sample_rate;
    config.max_voices = info->max_voices == 0 ? 256 : info->max_voices;
    config.max_buses = info->max_buses == 0 ? 16 : info->max_buses;
    config.limiter = info->limiter != 0;
    LockGuard<Mutex> lock(runtime->audio_mutex);
    if (runtime->audio_host.Load(MemoryOrder::Acquire) != nullptr)
    {
        return Fail(ENGINE_ERROR_ALREADY_EXISTS, "audio is already open");
    }
    AudioHostPtr host = MakeShared<AudioHost>();
    if (host.Get() == nullptr)
    {
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "out of memory");
    }
    const audio::MixerStatus made = host->mixer.Init(config);
    if (made != audio::MixerStatus::Ok)
    {
        return AudioFromMixer(made);
    }
    runtime->audio_owner = Move(host);
    runtime->audio_host.Store(runtime->audio_owner.Get(), MemoryOrder::Release);
    return ENGINE_OK;
}

engine_status AudioCloseFn(engine_handle engine)
{
    Runtime* runtime = nullptr;
    const engine_status status = ResolveRuntime(engine, &runtime);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(runtime->audio_mutex);
    if (runtime->audio_host.Load(MemoryOrder::Acquire) == nullptr)
    {
        return Fail(ENGINE_ERROR_NOT_READY, "audio is not open");
    }
    runtime->audio_host.Store(nullptr, MemoryOrder::SeqCst);
    AudioRenderWait();             // an `audio_render` that read the host before the clear finishes with it first
    runtime->audio_owner.Reset();  // the last share (a call in flight may hold one) frees the host
    return ENGINE_OK;
}

engine_status AudioClipCreateFn(engine_handle engine, const engine_audio_clip_info* info, const void* data, size_t size,
                                u64* out_clip)
{
    if (out_clip == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out_clip` is NULL");
    }
    *out_clip = 0;
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckIn(info, 24, "engine_audio_clip_info");
    if (status != ENGINE_OK)
    {
        return status;
    }
    audio::ChannelLayout layout = audio::ChannelLayout::Mono;
    if (data == nullptr || !AudioLayoutOf(info->channels, &layout) || info->sample_rate < 8000 ||
        info->sample_rate > 384000 || info->format > ENGINE_AUDIO_FORMAT_S16)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "data is NULL, or channels, sample_rate or format is out of range");
    }
    const usize bytes_per_sample = info->format == ENGINE_AUDIO_FORMAT_F32 ? 4 : 2;
    const usize frame_bytes = bytes_per_sample * info->channels;
    if (size == 0 || size % frame_bytes != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "size is not a whole number of frames");
    }
    if (size > kAudioMaxClipBytes)
    {
        return Fail(ENGINE_ERROR_TOO_LARGE, "the clip is above 512 MiB");
    }
    const usize frames = size / frame_bytes;
    if (info->loop_end != 0 && (info->loop_start >= info->loop_end || info->loop_end > frames))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the loop points are not inside the clip");
    }
    Vector<f32> samples;
    if (!samples.TryResizeUninitialized(frames * info->channels))
    {
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "out of memory");
    }
    const u8* bytes = static_cast<const u8*>(data);
    for (usize i = 0; i < samples.Size(); ++i)
    {
        if (info->format == ENGINE_AUDIO_FORMAT_F32)
        {
            u32 bits = static_cast<u32>(bytes[i * 4]) | (static_cast<u32>(bytes[i * 4 + 1]) << 8) |
                       (static_cast<u32>(bytes[i * 4 + 2]) << 16) | (static_cast<u32>(bytes[i * 4 + 3]) << 24);
            f32 v = 0.0f;
            MemCopy(&v, &bits, 4);
            if (!AudioFinite(v))
            {
                return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a sample is not finite");
            }
            samples[i] = v;
        }
        else
        {
            const i32 s = static_cast<i16>(static_cast<u16>(bytes[i * 2]) | (static_cast<u16>(bytes[i * 2 + 1]) << 8));
            samples[i] = static_cast<f32>(s) / 32768.0f;
        }
    }
    audio::ClipDesc desc;
    desc.format.sample_rate = info->sample_rate;
    desc.format.layout = layout;
    desc.format.format = audio::SampleFormat::F32;
    desc.frame_count = frames;
    desc.loop_start = info->loop_start;
    desc.loop_end = info->loop_end;
    LockGuard<Mutex> lock(host->mutex);
    // The clip is named after its generation, so a snapshot can name it ("c7") and the host gives it back.
    char name[16];
    FormatTo(name, sizeof(name), "c%u", static_cast<unsigned>(host->next_generation));
    audio::AudioClipPtr clip;
    if (audio::AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), name, &clip) !=
        audio::ClipStatus::Ok)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the clip was refused");
    }
    usize slot = host->clips.Size();
    for (usize i = 0; i < host->clips.Size(); ++i)
    {
        if (!host->clips[i].clip)
        {
            slot = i;
            break;
        }
    }
    if (slot == host->clips.Size())
    {
        if (host->clips.Size() >= kAudioMaxClips)
        {
            return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "4096 clips exist");
        }
        host->clips.Push(AudioHost::ClipSlot());
    }
    host->clips[slot].clip = clip;
    host->clips[slot].generation = host->next_generation++;
    *out_clip = AudioPack(host->clips[slot].generation, static_cast<u32>(slot));
    return ENGINE_OK;
}

namespace
{

// The clip a handle names; the lock is held.
audio::AudioClipConstPtr AudioFindClip(AudioHost* host, u64 handle)
{
    const u32 low = static_cast<u32>(handle & 0xFFFFFFFFull);
    if (low == 0 || low > host->clips.Size())
    {
        return audio::AudioClipConstPtr();
    }
    const AudioHost::ClipSlot& slot = host->clips[low - 1];
    return slot.clip && slot.generation == static_cast<u32>(handle >> 32) ? slot.clip : audio::AudioClipConstPtr();
}

}  // namespace

engine_status AudioClipOfHandle(Runtime& runtime, u64 handle, audio::AudioClipConstPtr* out)
{
    out->Reset();
    if (handle == 0)
    {
        return ENGINE_OK;
    }
    AudioHostPtr keep;
    {
        LockGuard<Mutex> lock(runtime.audio_mutex);
        keep = runtime.audio_owner;
    }
    if (keep.Get() == nullptr)
    {
        return Fail(ENGINE_ERROR_NOT_READY, "audio is not open");
    }
    LockGuard<Mutex> lock(keep->mutex);
    *out = AudioFindClip(keep.Get(), handle);
    return out->Get() == nullptr ? Fail(ENGINE_ERROR_STALE_HANDLE, "the clip is gone") : ENGINE_OK;
}

u64 AudioHandleOfClip(Runtime& runtime, const audio::AudioClipConstPtr& clip)
{
    AudioHostPtr keep;
    {
        LockGuard<Mutex> lock(runtime.audio_mutex);
        keep = runtime.audio_owner;
    }
    if (keep.Get() == nullptr || clip.Get() == nullptr)
    {
        return 0;
    }
    LockGuard<Mutex> lock(keep->mutex);
    for (usize i = 0; i < keep->clips.Size(); ++i)
    {
        if (keep->clips[i].clip.Get() == clip.Get())
        {
            return AudioPack(keep->clips[i].generation, static_cast<u32>(i));
        }
    }
    return 0;
}

engine_status AudioClipDestroyFn(engine_handle engine, u64 clip)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    if (!AudioFindClip(host, clip))
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the clip is gone");
    }
    host->clips[static_cast<usize>((clip & 0xFFFFFFFFull) - 1)].clip.Reset();
    return ENGINE_OK;
}

engine_status AudioPlayFn(engine_handle engine, const engine_audio_play* desc, u64* out_voice)
{
    if (out_voice == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out_voice` is NULL");
    }
    *out_voice = 0;
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckIn(desc, 40, "engine_audio_play");
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    audio::VoiceDesc voice;
    voice.clip = AudioFindClip(host, desc->clip);
    if (!voice.clip)
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the clip is gone");
    }
    voice.gain = desc->gain;
    voice.pitch = desc->pitch;
    voice.pan = desc->pan;
    voice.loop = desc->loop != 0;
    voice.priority = desc->priority;
    voice.bus = audio::BusId{desc->bus};
    audio::VoiceId id;
    status = AudioFromMixer(host->mixer.Play(voice, &id));
    if (status == ENGINE_OK)
    {
        *out_voice = AudioPack(id.generation, id.index);
    }
    return status;
}

engine_status AudioVoiceSetFn(engine_handle engine, u64 voice, const engine_audio_voice_params* params)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckIn(params, 28, "engine_audio_voice_params");
    if (status != ENGINE_OK)
    {
        return status;
    }
    if ((params->mask & ~15u) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "mask has an unknown bit");
    }
    const audio::VoiceId id = AudioUnpackVoice(voice);
    if (!id.IsValid())
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the voice id is zero");
    }
    LockGuard<Mutex> lock(host->mutex);
    if ((params->mask & ENGINE_AUDIO_VOICE_GAIN) != 0)
    {
        const u32 frames = params->fade_frames;
        status = AudioFromMixer(frames == 0 ? host->mixer.SetGain(id, params->gain)
                                            : host->mixer.Fade(id, params->gain, frames));
        if (status != ENGINE_OK)
        {
            return status;
        }
    }
    if ((params->mask & ENGINE_AUDIO_VOICE_PITCH) != 0)
    {
        status = AudioFromMixer(host->mixer.SetPitch(id, params->pitch));
        if (status != ENGINE_OK)
        {
            return status;
        }
    }
    if ((params->mask & ENGINE_AUDIO_VOICE_PAN) != 0)
    {
        status = AudioFromMixer(host->mixer.SetPan(id, params->pan));
        if (status != ENGINE_OK)
        {
            return status;
        }
    }
    if ((params->mask & ENGINE_AUDIO_VOICE_LOOP) != 0)
    {
        status = AudioFromMixer(host->mixer.SetLoop(id, params->loop != 0));
    }
    return status;
}

engine_status AudioVoiceStopFn(engine_handle engine, u64 voice, u32 fade_frames)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    const audio::VoiceId id = AudioUnpackVoice(voice);
    if (!id.IsValid())
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the voice id is zero");
    }
    LockGuard<Mutex> lock(host->mutex);
    return AudioFromMixer(fade_frames == 0 ? host->mixer.Stop(id) : host->mixer.Stop(id, fade_frames));
}

engine_status AudioBusCreateFn(engine_handle engine, engine_string name, u32 parent, u32* out_bus)
{
    if (out_bus == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out_bus` is NULL");
    }
    *out_bus = 0;
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    if (name.data == nullptr || name.length == 0 || name.length > kAudioMaxBusName)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the bus name is empty, NULL or above 63 bytes");
    }
    LockGuard<Mutex> lock(host->mutex);
    audio::BusId bus;
    const audio::MixerStatus made = host->mixer.CreateBus(StringView(name.data, name.length), audio::BusId{parent},
                                                          &bus);
    if (made != audio::MixerStatus::Ok)
    {
        return AudioFromMixer(made);
    }
    *out_bus = bus.index;
    return ENGINE_OK;
}

engine_status AudioBusSetFn(engine_handle engine, u32 bus, const engine_audio_bus_params* params)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckIn(params, 20, "engine_audio_bus_params");
    if (status != ENGINE_OK)
    {
        return status;
    }
    if ((params->mask & ~7u) != 0 || ((params->mask & ENGINE_AUDIO_BUS_LIMITER) != 0 && bus != 0))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT,
                    "mask has an unknown bit, or the limiter is set on a bus but the master");
    }
    LockGuard<Mutex> lock(host->mutex);
    const audio::BusId id{bus};
    if ((params->mask & ENGINE_AUDIO_BUS_GAIN) != 0)
    {
        status = AudioFromMixer(host->mixer.SetBusGain(id, params->gain));
        if (status != ENGINE_OK)
        {
            return status;
        }
    }
    if ((params->mask & ENGINE_AUDIO_BUS_MUTE) != 0)
    {
        status = AudioFromMixer(host->mixer.SetBusMute(id, params->mute != 0));
        if (status != ENGINE_OK)
        {
            return status;
        }
    }
    if ((params->mask & ENGINE_AUDIO_BUS_LIMITER) != 0)
    {
        status = AudioFromMixer(host->mixer.SetLimiter(params->limiter != 0));
    }
    return status;
}

engine_status AudioRenderFn(engine_handle engine, u32 frames, float* out)
{
    // No lock, no allocation, no failure text on the success path: the host's audio thread. The call is counted from
    // enter to leave, so `audio_close` and the engine's destruction wait for it instead of freeing what it uses.
    if (out == nullptr)
    {
        return ENGINE_ERROR_INVALID_ARGUMENT;
    }
    Runtime* runtime = AudioRenderEnter(engine);
    engine_status status = ENGINE_OK;
    AudioHost* host = runtime != nullptr ? runtime->audio_host.Load(MemoryOrder::SeqCst) : nullptr;
    if (runtime == nullptr)
    {
        status = ENGINE_ERROR_STALE_HANDLE;
    }
    else if (host == nullptr)
    {
        status = ENGINE_ERROR_NOT_READY;
    }
    else
    {
        host->mixer.Render(out, frames);
    }
    AudioRenderLeave();
    return status;
}

engine_status AudioEventsFn(engine_handle engine, engine_audio_event* out, size_t capacity, size_t* total)
{
    if (total == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`total` is NULL");
    }
    *total = 0;
    if (out == nullptr && capacity != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL with a capacity above zero");
    }
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    for (size_t i = 0; i < capacity; ++i)
    {
        const engine_status checked = AudioCheckIn(&out[i], 24, "engine_audio_event");
        if (checked != ENGINE_OK)
        {
            return checked;
        }
    }
    LockGuard<Mutex> lock(host->mutex);
    size_t written = 0;
    audio::VoiceEvent event;
    while (host->mixer.PollEvents(&event, 1) == 1)
    {
        *total += 1;
        if (event.type == audio::VoiceEventType::BusLevel || written >= capacity)
        {
            if (event.type == audio::VoiceEventType::BusLevel)
            {
                *total -= 1;
            }
            continue;
        }
        u32 kind = ENGINE_AUDIO_EVENT_FINISHED;
        switch (event.type)
        {
        case audio::VoiceEventType::Finished: kind = ENGINE_AUDIO_EVENT_FINISHED; break;
        case audio::VoiceEventType::Looped: kind = ENGINE_AUDIO_EVENT_LOOPED; break;
        case audio::VoiceEventType::Virtualised: kind = ENGINE_AUDIO_EVENT_VIRTUALISED; break;
        case audio::VoiceEventType::Stolen: kind = ENGINE_AUDIO_EVENT_STOLEN; break;
        case audio::VoiceEventType::Stopped: kind = ENGINE_AUDIO_EVENT_STOPPED; break;
        case audio::VoiceEventType::BusLevel: break;
        }
        out[written].kind = kind;
        out[written].voice = AudioPack(event.id.generation, event.id.index);
        out[written].block = event.block;
        written += 1;
    }
    return ENGINE_OK;
}

engine_status AudioStateFn(engine_handle engine, engine_audio_state* out)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckIn(out, 48, "engine_audio_state");
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    const audio::MixerStats stats = host->mixer.Stats();
    out->sample_rate = host->mixer.Config().sample_rate;
    out->channels = host->mixer.Channels();
    out->live_voices = host->mixer.VoiceCount();
    out->blocks_rendered = stats.blocks;
    out->voices_stolen = stats.voices_stolen;
    out->voices_finished = stats.voices_finished;
    out->commands_dropped = stats.commands_dropped;
    return ENGINE_OK;
}

namespace
{

constexpr usize kAudioMaxStreams = 256;

// A stream over the host's own callbacks.
class AudioHostSource final : public audio::StreamSource
{
public:
    explicit AudioHostSource(const engine_audio_stream_source& source) : source_(source) {}
    u32 Channels() const override { return source_.channels; }
    u32 SampleRate() const override { return source_.sample_rate; }
    u64 Length() const override { return source_.frames; }
    u64 LoopStart() const override { return source_.loop_start; }
    u64 LoopEnd() const override { return source_.loop_end; }
    u32 Read(f32* out, u32 frames) override
    {
        u32 got = source_.read(source_.user, out, frames);
        got = got > frames ? frames : got;
        // A host's decoder may hand over a NaN or an infinity: the mixer never sees one.
        for (u32 i = 0; i < got * source_.channels; ++i)
        {
            if (!AudioFinite(out[i]))
            {
                out[i] = 0.0f;
            }
        }
        return got;
    }
    bool Seek(u64 frame) override { return source_.seek(source_.user, frame) == 0; }

private:
    engine_audio_stream_source source_;
};

audio::StreamBufferPtr AudioFindStream(AudioHost* host, u64 handle)
{
    const u32 low = static_cast<u32>(handle & 0xFFFFFFFFull);
    if (low == 0 || low > host->streams.Size())
    {
        return audio::StreamBufferPtr();
    }
    const AudioHost::StreamSlot& slot = host->streams[low - 1];
    return slot.buffer && slot.generation == static_cast<u32>(handle >> 32) ? slot.buffer : audio::StreamBufferPtr();
}

// The slot of a new stream (a free one, or a new one); the lock is held.
engine_status AudioFreeStreamSlot(AudioHost* host, usize* out)
{
    usize slot = host->streams.Size();
    for (usize i = 0; i < host->streams.Size(); ++i)
    {
        if (!host->streams[i].buffer)
        {
            slot = i;
            break;
        }
    }
    if (slot == host->streams.Size())
    {
        if (host->streams.Size() >= kAudioMaxStreams)
        {
            return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "256 streams exist");
        }
        host->streams.Push(AudioHost::StreamSlot());
    }
    *out = slot;
    return ENGINE_OK;
}

// Opens a stream over `source` and stores it; the lock is held.
engine_status AudioAddStream(AudioHost* host, Runtime* runtime, UniquePtr<audio::StreamSource> source, u64* out)
{
    usize slot = 0;
    const engine_status slot_status = AudioFreeStreamSlot(host, &slot);
    if (slot_status != ENGINE_OK)
    {
        return slot_status;
    }
    audio::StreamConfig config;
    config.sample_rate = host->mixer.Config().sample_rate;
    config.id = host->next_stream_generation;
    audio::StreamBufferPtr buffer;
    if (audio::StreamBuffer::Create(Move(source), config, &buffer) != audio::StreamStatus::Ok)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the stream was refused");
    }
    if (!host->pump)
    {
        host->pump = MakeUnique<audio::StreamPump>(*runtime->EnsureJobs().GetScheduler());
    }
    host->pump->Add(buffer);
    buffer->Refill();
    host->streams[slot].buffer = buffer;
    host->streams[slot].generation = host->next_stream_generation++;
    *out = AudioPack(host->streams[slot].generation, static_cast<u32>(slot));
    return ENGINE_OK;
}

engine_status AudioResolveBoth(engine_handle engine, AudioHost** host, Runtime** runtime, AudioHostPtr* keep)
{
    *host = nullptr;
    *runtime = nullptr;
    const engine_status status = ResolveRuntime(engine, runtime);
    if (status != ENGINE_OK)
    {
        return status;
    }
    {
        // The share is taken under the lock `audio_close` takes, so the host outlives the call.
        LockGuard<Mutex> lock((*runtime)->audio_mutex);
        *keep = (*runtime)->audio_owner;
    }
    *host = keep->Get();
    return *host == nullptr ? Fail(ENGINE_ERROR_NOT_READY, "audio is not open") : ENGINE_OK;
}

audio::Tempo AudioTempoOf(const engine_audio_tempo* tempo)
{
    audio::Tempo out;
    if (tempo != nullptr)
    {
        out.beats_per_minute = tempo->beats_per_minute;
        out.beats_per_bar = tempo->beats_per_bar;
    }
    return out;
}

engine_status AudioCheckTempo(const engine_audio_tempo* tempo)
{
    if (tempo == nullptr)
    {
        return ENGINE_OK;
    }
    const engine_status status = AudioCheckIn(tempo, 12, "engine_audio_tempo");
    if (status != ENGINE_OK)
    {
        return status;
    }
    if (!AudioFinite(tempo->beats_per_minute) || !(tempo->beats_per_minute > 0.0f) || tempo->beats_per_bar == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the tempo is not above zero");
    }
    return ENGINE_OK;
}

// The player, made on `bus` by the first call; the lock is held.
audio::MusicPlayer* AudioPlayer(AudioHost* host, u32 bus)
{
    if (!host->music)
    {
        host->music = MakeUnique<audio::MusicPlayer>(&host->mixer, audio::BusId{bus});
        host->music_bus = bus;
    }
    return host->music.Get();
}

}  // namespace

engine_status AudioStreamOpenClipFn(engine_handle engine, u64 clip, u64* out_stream)
{
    if (out_stream == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out_stream` is NULL");
    }
    *out_stream = 0;
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    Runtime* runtime = nullptr;
    const engine_status status = AudioResolveBoth(engine, &host, &runtime, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    const audio::AudioClipConstPtr found = AudioFindClip(host, clip);
    if (!found)
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the clip is gone");
    }
    UniquePtr<audio::StreamSource> source;
    if (audio::MakeMemoryStream(found, &source) != audio::StreamStatus::Ok)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the clip cannot be streamed");
    }
    return AudioAddStream(host, runtime, Move(source), out_stream);
}

engine_status AudioStreamOpenFn(engine_handle engine, const engine_audio_stream_source* source, u64* out_stream)
{
    if (out_stream == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out_stream` is NULL");
    }
    *out_stream = 0;
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    Runtime* runtime = nullptr;
    engine_status status = AudioResolveBoth(engine, &host, &runtime, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckIn(source, 56, "engine_audio_stream_source");
    if (status != ENGINE_OK)
    {
        return status;
    }
    audio::ChannelLayout layout = audio::ChannelLayout::Mono;
    if (source->read == nullptr || source->seek == nullptr || !AudioLayoutOf(source->channels, &layout) ||
        source->sample_rate < 8000 || source->sample_rate > 384000 || source->frames == 0 ||
        source->frames > (u64{1} << 40) ||
        (source->loop_end != 0 && (source->loop_start >= source->loop_end || source->loop_end > source->frames)))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the stream source's callbacks, rate, channels or length are bad");
    }
    LockGuard<Mutex> lock(host->mutex);
    UniquePtr<audio::StreamSource> made = MakeUnique<AudioHostSource>(*source);
    if (made.Get() == nullptr)
    {
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "out of memory");
    }
    return AudioAddStream(host, runtime, Move(made), out_stream);
}

engine_status AudioStreamOfHandle(Runtime& runtime, u64 handle, audio::StreamBufferPtr* out)
{
    out->Reset();
    if (handle == 0)
    {
        return ENGINE_OK;
    }
    AudioHostPtr keep;
    {
        LockGuard<Mutex> lock(runtime.audio_mutex);
        keep = runtime.audio_owner;
    }
    if (keep.Get() == nullptr)
    {
        return Fail(ENGINE_ERROR_NOT_READY, "audio is not open");
    }
    LockGuard<Mutex> lock(keep->mutex);
    *out = AudioFindStream(keep.Get(), handle);
    return out->Get() == nullptr ? Fail(ENGINE_ERROR_STALE_HANDLE, "the stream is gone") : ENGINE_OK;
}

u64 AudioHandleOfStream(Runtime& runtime, const audio::StreamBufferPtr& stream)
{
    AudioHostPtr keep;
    {
        LockGuard<Mutex> lock(runtime.audio_mutex);
        keep = runtime.audio_owner;
    }
    if (keep.Get() == nullptr || stream.Get() == nullptr)
    {
        return 0;
    }
    LockGuard<Mutex> lock(keep->mutex);
    for (usize i = 0; i < keep->streams.Size(); ++i)
    {
        if (keep->streams[i].buffer.Get() == stream.Get())
        {
            return AudioPack(keep->streams[i].generation, static_cast<u32>(i));
        }
    }
    return 0;
}

engine_status AudioStreamOpenPushFn(engine_handle engine, const engine_audio_push_info* info, u64* out_stream)
{
    if (out_stream == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out_stream` is NULL");
    }
    *out_stream = 0;
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckIn(info, 20, "engine_audio_push_info");
    if (status != ENGINE_OK)
    {
        return status;
    }
    audio::ChannelLayout layout = audio::ChannelLayout::Mono;
    if (!AudioLayoutOf(info->channels, &layout) || info->capacity_frames < 64 || info->capacity_frames > 480000 ||
        info->prefill_frames > info->capacity_frames)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT,
                    "a push stream takes 1, 2, 4, 6 or 8 channels, 64 to 480000 frames and a prefill within them");
    }
    LockGuard<Mutex> lock(host->mutex);
    if (info->sample_rate != host->mixer.Config().sample_rate)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT,
                    "a push stream has the mixer's sample rate (audio_state): it is not resampled");
    }
    usize slot = 0;
    status = AudioFreeStreamSlot(host, &slot);
    if (status != ENGINE_OK)
    {
        return status;
    }
    audio::PushStreamConfig config;
    config.sample_rate = info->sample_rate;
    config.channels = info->channels;
    config.capacity_frames = info->capacity_frames;
    config.prefill_frames = info->prefill_frames;
    config.id = host->next_stream_generation;
    audio::StreamBufferPtr buffer;
    if (audio::StreamBuffer::CreatePush(config, &buffer) != audio::StreamStatus::Ok)
    {
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the push stream could not be made (memory)");
    }
    host->streams[slot].buffer = buffer;
    host->streams[slot].generation = host->next_stream_generation++;
    *out_stream = AudioPack(host->streams[slot].generation, static_cast<u32>(slot));
    return ENGINE_OK;
}

namespace
{
// The stream of a handle, a push stream; takes the lock for the lookup only (the buffer is shared, so a close during
// the push leaves it alive).
engine_status AudioPushTarget(engine_handle engine, u64 stream, audio::StreamBufferPtr* out)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    {
        LockGuard<Mutex> lock(host->mutex);
        *out = AudioFindStream(host, stream);
    }
    if (!*out)
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the stream is gone");
    }
    if (!(*out)->IsPush())
    {
        out->Reset();
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the stream is not a push stream (audio_stream_open_push)");
    }
    return ENGINE_OK;
}
}  // namespace

engine_status AudioStreamPushFn(engine_handle engine, u64 stream, const float* data, u32 frames, u32* accepted)
{
    if (accepted != nullptr)
    {
        *accepted = 0;
    }
    if (data == nullptr && frames != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`data` is NULL");
    }
    audio::StreamBufferPtr buffer;
    const engine_status status = AudioPushTarget(engine, stream, &buffer);
    if (status != ENGINE_OK)
    {
        return status;
    }
    const u32 took = buffer->Push(data, frames);
    if (accepted != nullptr)
    {
        *accepted = took;
    }
    return ENGINE_OK;
}

engine_status AudioStreamPushI16Fn(engine_handle engine, u64 stream, const i16* data, u32 frames, u32* accepted)
{
    if (accepted != nullptr)
    {
        *accepted = 0;
    }
    if (data == nullptr && frames != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`data` is NULL");
    }
    audio::StreamBufferPtr buffer;
    const engine_status status = AudioPushTarget(engine, stream, &buffer);
    if (status != ENGINE_OK)
    {
        return status;
    }
    const u32 took = buffer->PushI16(data, frames);
    if (accepted != nullptr)
    {
        *accepted = took;
    }
    return ENGINE_OK;
}

engine_status AudioStreamStatsFn(engine_handle engine, u64 stream, engine_audio_stream_stats* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    const engine_status checked = AudioCheckIn(out, 40, "engine_audio_stream_stats");
    if (checked != ENGINE_OK)
    {
        return checked;
    }
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    audio::StreamBufferPtr buffer;
    {
        LockGuard<Mutex> lock(host->mutex);
        buffer = AudioFindStream(host, stream);
    }
    if (!buffer)
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the stream is gone");
    }
    out->is_push = buffer->IsPush() ? 1u : 0u;
    out->buffered_frames = buffer->Buffered();
    out->underruns = buffer->Underruns();
    out->frames_pushed = 0;
    out->frames_dropped = 0;
    if (buffer->IsPush())
    {
        const audio::PushStreamStats stats = buffer->PushStats();
        out->frames_pushed = stats.frames_pushed;
        out->frames_dropped = stats.frames_dropped;
        out->underruns = stats.underruns;
    }
    return ENGINE_OK;
}

engine_status AudioStreamCloseFn(engine_handle engine, u64 stream)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    if (!AudioFindStream(host, stream))
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the stream is gone");
    }
    const usize index = static_cast<usize>((stream & 0xFFFFFFFFull) - 1);
    // The host's callbacks are not called once this returns: the pump drops the buffer and waits for the refills in
    // flight.
    if (host->pump)
    {
        host->pump->Remove(host->streams[index].buffer);
    }
    host->streams[index].buffer->Close();  // a voice that plays a push stream (an entity's, say) ends
    host->streams[index].buffer.Reset();
    return ENGINE_OK;
}

engine_status AudioStreamPlayFn(engine_handle engine, u64 stream, const engine_audio_play* desc, u64* out_voice)
{
    if (out_voice == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out_voice` is NULL");
    }
    *out_voice = 0;
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckIn(desc, 40, "engine_audio_play");
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    const audio::StreamBufferPtr buffer = AudioFindStream(host, stream);
    if (!buffer)
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the stream is gone");
    }
    audio::VoiceDesc voice;
    voice.stream = buffer;
    voice.clip = buffer->Clip();
    voice.gain = desc->gain;
    voice.pan = desc->pan;
    voice.pitch = 1.0f;
    voice.loop = desc->loop != 0;
    voice.priority = desc->priority;
    voice.bus = audio::BusId{desc->bus};
    audio::VoiceId id;
    status = AudioFromMixer(host->mixer.Play(voice, &id));
    if (status == ENGINE_OK)
    {
        *out_voice = AudioPack(id.generation, id.index);
    }
    return status;
}

engine_status AudioUpdateFn(engine_handle engine, u32 wait)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    if (host->music)
    {
        host->music->Update();
    }
    if (host->pump)
    {
        if (wait != 0)
        {
            host->pump->Run();
        }
        else
        {
            host->pump->Update();
        }
    }
    return ENGINE_OK;
}

engine_status AudioMusicPlayFn(engine_handle engine, u64 stream, u32 bus, u32 fade_frames,
                               const engine_audio_tempo* tempo)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckTempo(tempo);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    const audio::StreamBufferPtr buffer = AudioFindStream(host, stream);
    if (!buffer)
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the stream is gone");
    }
    if (host->music && host->music_bus != bus)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the music already plays on another bus");
    }
    return AudioFromMixer(AudioPlayer(host, bus)->Play(buffer, fade_frames, audio::kMusicNow, AudioTempoOf(tempo)));
}

engine_status AudioMusicCrossfadeFn(engine_handle engine, u64 stream, u32 fade_frames, u32 at_bar,
                                    const engine_audio_tempo* tempo)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckTempo(tempo);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    const audio::StreamBufferPtr buffer = AudioFindStream(host, stream);
    if (!buffer)
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the stream is gone");
    }
    audio::MusicPlayer* player = AudioPlayer(host, 0);
    return AudioFromMixer(at_bar != 0
                              ? player->CrossFadeAtBar(buffer, fade_frames, AudioTempoOf(tempo))
                              : player->CrossFadeTo(buffer, fade_frames, audio::kMusicNow, AudioTempoOf(tempo)));
}

engine_status AudioMusicStopFn(engine_handle engine, u32 fade_frames)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    if (host->music)
    {
        host->music->Stop(fade_frames);
    }
    return ENGINE_OK;
}

engine_status AudioMusicLayersFn(engine_handle engine, const u64* streams, const float* gains, u32 count, u32 bus)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    if (streams == nullptr || gains == nullptr || count == 0 || count > 8)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "streams or gains is NULL, or count is not 1 to 8");
    }
    LockGuard<Mutex> lock(host->mutex);
    if (host->music && host->music_bus != bus)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the music already plays on another bus");
    }
    audio::StreamBufferPtr buffers[8];
    for (u32 i = 0; i < count; ++i)
    {
        buffers[i] = AudioFindStream(host, streams[i]);
        if (!buffers[i])
        {
            return Fail(ENGINE_ERROR_STALE_HANDLE, "a stream is gone");
        }
        if (!AudioFinite(gains[i]))
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a gain is not finite");
        }
    }
    return AudioFromMixer(AudioPlayer(host, bus)->PlayLayers(Span<const audio::StreamBufferPtr>(buffers, count),
                                                             Span<const f32>(gains, count)));
}

engine_status AudioMusicLayerGainFn(engine_handle engine, u32 layer, float gain, u32 frames)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    if (!AudioFinite(gain) || gain < 0.0f || gain > 16.0f || layer >= 8)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the gain or the layer is out of range");
    }
    LockGuard<Mutex> lock(host->mutex);
    if (host->music)
    {
        host->music->SetLayerGain(layer, gain, frames);
    }
    return ENGINE_OK;
}

engine_status AudioMusicStingerFn(engine_handle engine, u64 clip, float gain, u32 on_beat, u64* out_frame)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    if (!AudioFinite(gain) || gain < 0.0f || gain > 16.0f)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the gain is out of range");
    }
    LockGuard<Mutex> lock(host->mutex);
    const audio::AudioClipConstPtr found = AudioFindClip(host, clip);
    if (!found)
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the clip is gone");
    }
    u64 frame = 0;
    const engine_status result = AudioFromMixer(AudioPlayer(host, 0)->PlayStinger(found, gain, on_beat != 0, &frame));
    if (result == ENGINE_OK && out_frame != nullptr)
    {
        *out_frame = frame;
    }
    return result;
}

engine_status AudioBusEffectSetFn(engine_handle engine, u32 bus, u32 slot, const engine_audio_effect* effect)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    status = AudioCheckIn(effect, 40, "engine_audio_effect");
    if (status != ENGINE_OK)
    {
        return status;
    }
    if (effect->kind > ENGINE_AUDIO_EFFECT_REVERB || slot >= audio::kMaxInserts)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the effect kind or the slot is out of range");
    }
    for (u32 i = 0; i < 8; ++i)
    {
        if (!AudioFinite(effect->params[i]))
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a parameter is not finite");
        }
    }
    LockGuard<Mutex> lock(host->mutex);
    const audio::BusId id{bus};
    const u32 channels = host->mixer.Channels();
    const u32 rate = host->mixer.Config().sample_rate;
    const u32 ramp = host->mixer.Config().ramp_frames;
    const f32* p = effect->params;
    const bool known_bus = bus < audio::kMaxBuses;
    u8* held = known_bus ? &host->insert_kind[bus][slot] : nullptr;
    f32* held_size = known_bus ? &host->insert_size[bus][slot] : nullptr;
    if (effect->kind == ENGINE_AUDIO_EFFECT_NONE)
    {
        status = AudioFromMixer(host->mixer.ClearInsert(id, slot));
        if (status == ENGINE_OK && held != nullptr)
        {
            *held = 0;
        }
        return status;
    }
    const bool same = held != nullptr && *held == effect->kind;
    if (effect->kind == ENGINE_AUDIO_EFFECT_FILTER)
    {
        if (p[0] < 0.0f || p[0] > 6.0f || !(p[1] > 0.0f) || !(p[2] > 0.0f))
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the filter type, cutoff or q is out of range");
        }
        const audio::FilterDesign design = audio::DesignFilter(static_cast<audio::FilterType>(static_cast<u32>(p[0])),
                                                               rate, p[1], p[2], p[3]);
        if (!same)
        {
            status = AudioFromMixer(host->mixer.SetInsert(id, slot, audio::MakeFilterEffect(channels, ramp)));
            if (status != ENGINE_OK)
            {
                return status;
            }
            if (held != nullptr)
            {
                *held = static_cast<u8>(effect->kind);
            }
        }
        return AudioFromMixer(host->mixer.SetInsertParams(id, slot, design.params));
    }
    if (effect->kind == ENGINE_AUDIO_EFFECT_DELAY)
    {
        if (!(p[0] > 0.0f) || p[0] > 2.0f)
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the delay is not in (0, 2] seconds");
        }
        audio::DelayDesc desc;
        desc.delay_seconds = p[0];
        desc.feedback = p[1];
        desc.wet = p[2];
        desc.dry = p[3];
        desc.damping = p[4];
        if (!same)
        {
            status = AudioFromMixer(
                host->mixer.SetInsert(id, slot, audio::MakeDelayEffect(channels, rate, 2.0f, ramp)));
            if (status != ENGINE_OK)
            {
                return status;
            }
            if (held != nullptr)
            {
                *held = static_cast<u8>(effect->kind);
            }
        }
        return AudioFromMixer(host->mixer.SetInsertParams(id, slot, audio::MakeDelayParams(rate, desc)));
    }
    audio::ReverbDesc desc;
    desc.sample_rate = rate;
    desc.size = p[0];
    desc.decay_seconds = p[1];
    desc.damping = p[2];
    desc.wet = p[3];
    desc.dry = p[4];
    desc.predelay_seconds = p[5];
    // A reverb's lines are sized when it is made: a new size needs a new effect, the same size only new numbers.
    if (!(same && *held_size == desc.size))
    {
        status = AudioFromMixer(host->mixer.SetInsert(id, slot, audio::MakeReverbEffect(channels, desc)));
        if (status != ENGINE_OK)
        {
            return status;
        }
        if (held != nullptr)
        {
            *held = static_cast<u8>(effect->kind);
            *held_size = desc.size;
        }
    }
    return AudioFromMixer(host->mixer.SetInsertParams(id, slot, audio::MakeReverbParams(desc)));
}

engine_status AudioBusSendFn(engine_handle engine, u32 bus, u32 slot, u32 aux, float gain, u32 pre_fader)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    return AudioFromMixer(host->mixer.SetBusSend(audio::BusId{bus}, slot, audio::BusId{aux}, gain, pre_fader != 0));
}

engine_status AudioBusDuckFn(engine_handle engine, u32 target, u32 source, float depth, float threshold,
                             float release_frames)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    return AudioFromMixer(
        host->mixer.SetDuck(audio::BusId{target}, audio::BusId{source}, depth, threshold, release_frames));
}

namespace
{

// Gives the snapshot's voices their clips and streams back from the host's tables.
class AudioHostResolver final : public audio::SnapshotResolver
{
public:
    explicit AudioHostResolver(AudioHost* host) : host_(host) {}
    audio::AudioClipConstPtr FindClip(StringView name, u32 frames) override
    {
        for (usize i = 0; i < host_->clips.Size(); ++i)
        {
            const audio::AudioClipConstPtr& c = host_->clips[i].clip;
            if (c && c->Name() == name && c->Frames() == frames)
            {
                return c;
            }
        }
        return audio::AudioClipConstPtr();
    }
    audio::StreamBufferPtr FindStream(StringView name) override
    {
        for (usize i = 0; i < host_->streams.Size(); ++i)
        {
            const audio::StreamBufferPtr& s = host_->streams[i].buffer;
            if (s && s->Clip()->Name() == name)
            {
                return s;
            }
        }
        return audio::StreamBufferPtr();
    }

private:
    AudioHost* host_;
};

}  // namespace

engine_status AudioSnapshotFn(engine_handle engine, engine_buffer* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    out->data = nullptr;
    out->size = 0;
    out->owner = nullptr;
    out->release = nullptr;
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> lock(host->mutex);
    bytes::ByteWriter writer;
    const audio::SnapshotStatus saved = host->mixer.SaveState(&writer);
    if (saved == audio::SnapshotStatus::LiveStream)
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED,
                    "a voice plays a live push stream, which a snapshot cannot hold: stop it first");
    }
    if (saved != audio::SnapshotStatus::Ok)
    {
        return Fail(ENGINE_ERROR_NOT_READY, "commands are queued: render a block first");
    }
    const ByteSpan bytes = writer.Bytes();
    MakeTextBuffer(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()), out);
    return ENGINE_OK;
}

engine_status AudioRestoreFn(engine_handle engine, const void* data, size_t size)
{
    AudioHostPtr keep;
    AudioHost* host = nullptr;
    const engine_status status = AudioResolve(engine, &host, &keep);
    if (status != ENGINE_OK)
    {
        return status;
    }
    if (data == nullptr && size != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`data` is NULL");
    }
    LockGuard<Mutex> lock(host->mutex);
    AudioHostResolver resolver(host);
    switch (host->mixer.LoadState(ByteSpan(static_cast<const byte*>(data), size), &resolver))
    {
    case audio::SnapshotStatus::Ok: return ENGINE_OK;
    case audio::SnapshotStatus::Busy: return Fail(ENGINE_ERROR_NOT_READY, "commands are queued: render a block first");
    case audio::SnapshotStatus::Corrupt: return Fail(ENGINE_ERROR_CORRUPT, "not an audio snapshot, or damaged");
    case audio::SnapshotStatus::Version: return Fail(ENGINE_ERROR_UNSUPPORTED_VERSION, "another snapshot version");
    case audio::SnapshotStatus::Mismatch: return Fail(ENGINE_ERROR_UNSUPPORTED, "the mixer is set up differently");
    case audio::SnapshotStatus::MissingClip:
        return Fail(ENGINE_ERROR_MISSING_DEPENDENCY, "a clip or stream is not open");
    case audio::SnapshotStatus::LiveStream: return Fail(ENGINE_ERROR_UNSUPPORTED, "a live stream cannot be restored");
    }
    return Fail(ENGINE_ERROR_INTERNAL, "unknown snapshot status");
}

}  // namespace vse::engine_c
