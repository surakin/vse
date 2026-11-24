// The WASAPI backend (Phase 26 Day 440). Shared mode, event-driven: the endpoint signals an event when it has room, the
// render thread fills that room through the render callback. The device grants the endpoint's mix format (its rate and
// channel count; the mixer adapts) and converts the rendered f32 frames at the boundary when the endpoint wants 16- or
// 32-bit integers. A device that goes away (AUDCLNT_E_DEVICE_INVALIDATED: unplugged, disabled) or, when the default
// output was opened, a change of the default output, is reported once as `Lost`; the system reopens through Day 427's
// path.
#include "audio/windows/wasapi_device.h"
#include "audio/device_counters.h"

#include "audio/pcm_negotiate.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "os/os.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"
#include "sync/atomic.h"
#include "sync/thread.h"

#include "unity/windows_enable.h"
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include "unity/windows_disable.h"

namespace vse::audio
{

namespace
{

constexpr u32 kWasapiIdChars = sizeof(DeviceDescription::name);
constexpr REFERENCE_TIME kHundredNanosPerSecond = 10000000;
constexpr DWORD kWakeTimeoutMillis = 200;  // the render thread looks at the stop flag at least this often

// The tail every KSDATAFORMAT_SUBTYPE_* GUID shares; the first word says which one (1 integer PCM, 3 IEEE float).
bool IsSubtype(const GUID& g, unsigned long first)
{
    static constexpr Array<u8, 8> kTail = {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
    if (g.Data1 != first || g.Data2 != 0x0000 || g.Data3 != 0x0010)
    {
        return false;
    }
    for (u32 i = 0; i < 8; ++i)
    {
        if (g.Data4[i] != kTail[i])
        {
            return false;
        }
    }
    return true;
}

// The sample format of an endpoint's mix format, or false when the backend cannot write it.
bool PcmFormatOf(const WAVEFORMATEX* wf, PcmFormat* out)
{
    bool is_float = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool is_pcm = wf->wFormatTag == WAVE_FORMAT_PCM;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22)
    {
        const WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        is_float = IsSubtype(ext->SubFormat, 3);
        is_pcm = IsSubtype(ext->SubFormat, 1);
    }
    if (is_float && wf->wBitsPerSample == 32)
    {
        *out = PcmFormat::F32;
        return true;
    }
    if (is_pcm && wf->wBitsPerSample == 16)
    {
        *out = PcmFormat::S16;
        return true;
    }
    if (is_pcm && wf->wBitsPerSample == 32)  // 24 valid bits in a 32-bit container too: the top bits carry the sample
    {
        *out = PcmFormat::S32;
        return true;
    }
    return false;
}

DeviceStatus StatusOf(HRESULT hr)
{
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
    {
        return DeviceStatus::Lost;
    }
    if (hr == AUDCLNT_E_DEVICE_IN_USE || hr == AUDCLNT_E_ENDPOINT_CREATE_FAILED)
    {
        return DeviceStatus::Busy;
    }
    if (hr == E_OUTOFMEMORY)
    {
        return DeviceStatus::OutOfMemory;
    }
    if (hr == E_ACCESSDENIED)
    {
        return DeviceStatus::PermissionDenied;
    }
    if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT)
    {
        return DeviceStatus::Unsupported;
    }
    return DeviceStatus::IoError;
}

template <class T> void ReleaseCom(T** p)
{
    if (*p != nullptr)
    {
        (*p)->Release();
        *p = nullptr;
    }
}

// COM for the calling thread, in the multithreaded apartment. A thread that already joined a single-threaded one keeps
// it (the audio interfaces work from either); only a successful call here is undone.
struct ComScope
{
    bool initialized = false;
    ComScope()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized = hr == S_OK || hr == S_FALSE;
    }
    ~ComScope()
    {
        if (initialized)
        {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
};

// An endpoint id as UTF-8 into `out` (truncated to the description's name size).
void IdToName(const wchar_t* id, char* out, usize capacity)
{
    usize length = 0;
    while (id[length] != 0)
    {
        length += 1;
    }
    const usize written = os::windows::Utf16ToUtf8(id, length, out, capacity - 1);
    out[written < capacity ? written : capacity - 1] = 0;
}

bool SameId(const wchar_t* a, const wchar_t* b)
{
    if (a == nullptr || b == nullptr)
    {
        return false;
    }
    usize i = 0;
    while (a[i] != 0 && a[i] == b[i])
    {
        i += 1;
    }
    return a[i] == b[i];
}

// Watches the system's outputs while a device is open: a change of the default output (when the default was opened)
// or the opened endpoint leaving the active state marks the device lost and wakes its render thread. Runs on the
// system's notification thread, so it only sets a flag and an event. Embedded in the device, which unregisters it
// before it goes; the reference count is kept for COM's sake and never frees it.
class EndpointWatch final : public IMMNotificationClient
{
public:
    Atomic<bool> changed{false};
    HANDLE wake = nullptr;
    bool follows_default = false;
    wchar_t id[kWasapiIdChars] = {};

    ULONG STDMETHODCALLTYPE AddRef() override { return refs_.FetchAdd(1) + 1; }
    ULONG STDMETHODCALLTYPE Release() override { return refs_.FetchSub(1) - 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient))
        {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
    {
        if (follows_default && flow == eRender && role == eConsole)
        {
            Signal();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR device, DWORD state) override
    {
        if (state != DEVICE_STATE_ACTIVE && SameId(device, id))
        {
            Signal();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR device) override
    {
        if (SameId(device, id))
        {
            Signal();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    void Signal()
    {
        changed.Store(true);
        if (wake != nullptr)
        {
            SetEvent(wake);
        }
    }

    Atomic<ULONG> refs_{1};
};

class WasapiDevice final : public AudioDevice
{
public:
    ~WasapiDevice() override { Close(); }

    void SetCallbacks(RenderCallback render, DeviceErrorCallback error, void* user) override
    {
        render_ = render;
        error_ = error;
        user_ = user;
    }

    DeviceStatus Open(const DeviceConfig& config) override
    {
        if (client_ != nullptr)
        {
            return DeviceStatus::AlreadyOpen;
        }
        if (config.buffer_frames < 1 || config.buffer_frames > kMaxDeviceBufferFrames ||
            ChannelCount(config.layout) == 0 ||
            (config.sample_rate != 0 && (config.sample_rate < kMinSampleRate || config.sample_rate > kMaxSampleRate)))
        {
            return DeviceStatus::InvalidConfig;
        }
        com_ = MakeUnique<ComScope>();
        const DeviceStatus status = OpenEndpoint(config);
        if (status != DeviceStatus::Ok)
        {
            Release();
        }
        return status;
    }

    DeviceStatus Start() override
    {
        if (client_ == nullptr || render_ == nullptr)
        {
            return DeviceStatus::NotOpen;
        }
        if (running_.Load())
        {
            return DeviceStatus::Busy;
        }
        if (lost_.Load())
        {
            return DeviceStatus::Lost;
        }
        stop_.Store(false);
        // The first buffer is silence, so the endpoint does not start on an empty buffer.
        BYTE* data = nullptr;
        HRESULT hr = render_client_->GetBuffer(buffer_size_, &data);
        if (SUCCEEDED(hr))
        {
            hr = render_client_->ReleaseBuffer(buffer_size_, AUDCLNT_BUFFERFLAGS_SILENT);
        }
        if (SUCCEEDED(hr))
        {
            hr = client_->Start();
        }
        if (FAILED(hr))
        {
            return StatusOf(hr);
        }
        thread_ = Thread(&WasapiDevice::Run, this);
        running_.Store(true);
        return DeviceStatus::Ok;
    }

    void Stop() override
    {
        if (!running_.Load())
        {
            return;
        }
        stop_.Store(true);
        SetEvent(event_);
        if (thread_.Joinable())
        {
            thread_.Join();
        }
        client_->Stop();
        client_->Reset();
        running_.Store(false);
    }

    DeviceStatus Close() override
    {
        Stop();
        Release();
        return DeviceStatus::Ok;
    }

    DeviceInfo Info() const override { return info_; }
    bool IsOpen() const override { return client_ != nullptr; }
    bool IsRunning() const override { return running_.Load(); }
    DeviceStatus Pump(u64) override { return DeviceStatus::Unsupported; }
    u64 FramesRendered() const override { return rendered_.Load(); }

    DeviceCounters Counters() const override
    {
        DeviceCounters c;
        counters_.Fill(&c);
        c.underruns = underruns_.Load();
        return c;
    }

private:
    // Finds the endpoint, initializes the client on its mix format and allocates the buffers.
    DeviceStatus OpenEndpoint(const DeviceConfig& config)
    {
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                      reinterpret_cast<void**>(&enumerator_));
        if (FAILED(hr))
        {
            return DeviceStatus::NotFound;
        }
        const bool want_default = config.name.Empty() || config.name == StringView("default");
        if (want_default)
        {
            hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint_);
        }
        else
        {
            wchar_t wide[kWasapiIdChars];
            if (config.name.Length() >= kWasapiIdChars)
            {
                return DeviceStatus::NotFound;
            }
            const usize n = os::windows::Utf8ToUtf16(config.name, wide, kWasapiIdChars - 1);
            wide[n] = 0;
            hr = enumerator_->GetDevice(wide, &endpoint_);
        }
        if (FAILED(hr))
        {
            return DeviceStatus::NotFound;
        }
        LPWSTR id = nullptr;
        if (SUCCEEDED(endpoint_->GetId(&id)))
        {
            usize i = 0;
            for (; id[i] != 0 && i + 1 < kWasapiIdChars; ++i)
            {
                watch_.id[i] = id[i];
            }
            watch_.id[i] = 0;
            CoTaskMemFree(id);
        }
        hr = endpoint_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client_));
        if (FAILED(hr))
        {
            return StatusOf(hr);
        }
        WAVEFORMATEX* mix = nullptr;
        hr = client_->GetMixFormat(&mix);
        if (FAILED(hr))
        {
            return StatusOf(hr);
        }
        PcmFormat format = PcmFormat::F32;
        ChannelLayout layout = ChannelLayout::Stereo;
        const bool playable = PcmFormatOf(mix, &format) && LayoutOf(mix->nChannels, &layout);
        const u32 rate = mix->nSamplesPerSec;
        const u32 channels = mix->nChannels;
        if (!playable || rate < kMinSampleRate || rate > kMaxSampleRate)
        {
            CoTaskMemFree(mix);
            return DeviceStatus::Unsupported;
        }
        // Two of the wanted buffers' worth, so one can be filled while the other plays.
        const REFERENCE_TIME duration = static_cast<REFERENCE_TIME>(config.buffer_frames) * 2 * kHundredNanosPerSecond /
                                        static_cast<i64>(rate);
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, duration, 0, mix,
                                 nullptr);
        CoTaskMemFree(mix);
        if (FAILED(hr))
        {
            return StatusOf(hr);
        }
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event_ == nullptr)
        {
            return DeviceStatus::OutOfMemory;
        }
        UINT32 size = 0;
        REFERENCE_TIME stream_latency = 0;
        hr = client_->SetEventHandle(event_);
        if (SUCCEEDED(hr))
        {
            hr = client_->GetBufferSize(&size);
        }
        if (SUCCEEDED(hr))
        {
            hr = client_->GetStreamLatency(&stream_latency);
        }
        if (SUCCEEDED(hr))
        {
            hr = client_->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render_client_));
        }
        if (FAILED(hr))
        {
            return StatusOf(hr);
        }
        buffer_size_ = size;
        const u32 frames = size < kMaxDeviceBufferFrames ? size : kMaxDeviceBufferFrames;
        if (!buffer_.TryResizeUninitialized(static_cast<usize>(frames) * channels) ||
            (format != PcmFormat::F32 &&
             !raw_.TryResizeUninitialized(static_cast<usize>(frames) * channels * PcmSampleBytes(format))))
        {
            return DeviceStatus::OutOfMemory;
        }
        format_ = format;
        channels_ = channels;
        info_.sample_rate = rate;
        info_.layout = layout;
        info_.buffer_frames = frames;
        info_.latency_frames = size +
                               static_cast<u32>(stream_latency * static_cast<i64>(rate) / kHundredNanosPerSecond);
        watch_.wake = event_;
        watch_.follows_default = want_default;
        watch_.changed.Store(false);
        if (SUCCEEDED(enumerator_->RegisterEndpointNotificationCallback(&watch_)))
        {
            watching_ = true;
        }
        lost_.Store(false);
        rendered_.Store(0);
        counters_.Reset();
        underruns_.Store(0);
        return DeviceStatus::Ok;
    }

    // Releases everything Open made, in the reverse order.
    void Release()
    {
        if (watching_)
        {
            enumerator_->UnregisterEndpointNotificationCallback(&watch_);
            watching_ = false;
        }
        watch_.wake = nullptr;
        ReleaseCom(&render_client_);
        ReleaseCom(&client_);
        ReleaseCom(&endpoint_);
        ReleaseCom(&enumerator_);
        if (event_ != nullptr)
        {
            CloseHandle(event_);
            event_ = nullptr;
        }
        buffer_.Reset();
        raw_.Reset();
        com_.Reset();
    }

    // The render thread: waits for the endpoint's event and fills the room it has.
    void Run()
    {
        ComScope com;
        DWORD task_index = 0;
        HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);  // best effort: null is not an error
        const u64 block_micros = static_cast<u64>(info_.buffer_frames) * 1000000ull / info_.sample_rate;
        bool fed = false;
        while (!stop_.Load())
        {
            WaitForSingleObject(event_, kWakeTimeoutMillis);
            if (stop_.Load())
            {
                break;
            }
            if (watch_.changed.Load())
            {
                ReportLost(DeviceStatus::Lost);
                break;
            }
            UINT32 padding = 0;
            HRESULT hr = client_->GetCurrentPadding(&padding);
            if (FAILED(hr))
            {
                ReportLost(StatusOf(hr) == DeviceStatus::Lost ? DeviceStatus::Lost : DeviceStatus::IoError);
                break;
            }
            if (fed && padding == 0)
            {
                underruns_.FetchAdd(1);  // the endpoint played everything before the next wake
            }
            u32 room = buffer_size_ - padding;
            while (room > 0 && !stop_.Load())
            {
                const u32 frames = room < info_.buffer_frames ? room : info_.buffer_frames;
                BYTE* data = nullptr;
                hr = render_client_->GetBuffer(frames, &data);
                if (FAILED(hr))
                {
                    break;
                }
                const u64 start = CallbackCounters::NowMicros();
                render_(user_, buffer_.Data(), frames);
                counters_.Record(CallbackCounters::NowMicros() - start, block_micros);
                rendered_.FetchAdd(frames);
                const u32 samples = frames * channels_;
                if (format_ == PcmFormat::F32)
                {
                    MemCopy(data, buffer_.Data(), static_cast<usize>(samples) * sizeof(f32));
                }
                else
                {
                    ConvertForPcm(format_, buffer_.Data(), samples, data);
                }
                hr = render_client_->ReleaseBuffer(frames, 0);
                if (FAILED(hr))
                {
                    break;
                }
                room -= frames;
                fed = true;
            }
            if (FAILED(hr))
            {
                ReportLost(StatusOf(hr) == DeviceStatus::Lost ? DeviceStatus::Lost : DeviceStatus::IoError);
                break;
            }
        }
        if (task != nullptr)
        {
            AvRevertMmThreadCharacteristics(task);
        }
    }

    // Called once from the render thread when the device stopped on its own.
    void ReportLost(DeviceStatus status)
    {
        if (lost_.Exchange(true))
        {
            return;
        }
        if (error_ != nullptr)
        {
            error_(user_, status);
        }
    }

    RenderCallback render_ = nullptr;
    DeviceErrorCallback error_ = nullptr;
    void* user_ = nullptr;
    UniquePtr<ComScope> com_;
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* endpoint_ = nullptr;
    IAudioClient* client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    HANDLE event_ = nullptr;
    EndpointWatch watch_;
    bool watching_ = false;
    u32 buffer_size_ = 0;
    u32 channels_ = 0;
    PcmFormat format_ = PcmFormat::F32;
    DeviceInfo info_;
    Vector<f32> buffer_;
    Vector<u8> raw_;
    Thread thread_;
    Atomic<bool> running_{false};
    Atomic<bool> stop_{false};
    Atomic<bool> lost_{false};
    Atomic<u64> rendered_{0};
    CallbackCounters counters_;
    Atomic<u64> underruns_{0};
};

}  // namespace

DeviceStatus EnumerateWasapiDevices(Vector<DeviceDescription>* out)
{
    out->Clear();
    ComScope com;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))))
    {
        return DeviceStatus::Ok;  // no audio service: no outputs
    }
    LPWSTR default_id = nullptr;
    IMMDevice* default_device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &default_device)))
    {
        default_device->GetId(&default_id);
        default_device->Release();
    }
    DeviceStatus status = DeviceStatus::Ok;
    IMMDeviceCollection* collection = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)))
    {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count && status == DeviceStatus::Ok; ++i)
        {
            IMMDevice* device = nullptr;
            if (FAILED(collection->Item(i, &device)))
            {
                continue;
            }
            LPWSTR id = nullptr;
            if (SUCCEEDED(device->GetId(&id)))
            {
                DeviceDescription d;
                IdToName(id, d.name, sizeof(d.name));
                d.is_default = SameId(id, default_id);
                IAudioClient* client = nullptr;
                if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                               reinterpret_cast<void**>(&client))))
                {
                    WAVEFORMATEX* mix = nullptr;
                    if (SUCCEEDED(client->GetMixFormat(&mix)))
                    {
                        d.max_channels = mix->nChannels;
                        d.min_rate = mix->nSamplesPerSec;  // shared mode plays the mix rate only
                        d.max_rate = mix->nSamplesPerSec;
                        CoTaskMemFree(mix);
                    }
                    client->Release();
                }
                if (out->TryReserve(out->Size() + 1))
                {
                    out->Push(d);
                }
                else
                {
                    status = DeviceStatus::OutOfMemory;
                }
                CoTaskMemFree(id);
            }
            device->Release();
        }
        collection->Release();
    }
    if (default_id != nullptr)
    {
        CoTaskMemFree(default_id);
    }
    enumerator->Release();
    return status;
}

UniquePtr<AudioDevice> CreateWasapiDevice()
{
    return MakeUnique<WasapiDevice>();
}

}  // namespace vse::audio
