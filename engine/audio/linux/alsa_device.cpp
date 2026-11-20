#include "audio/linux/alsa_device.h"

#include <fcntl.h>
#include <sched.h>
#include <sound/asound.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "audio/convert.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "io/directory.h"
#include "io/path.h"
#include "lang/utility.h"
#include "os/last_error.h"
#include "os/os.h"
#include "sync/atomic.h"
#include "sync/thread.h"

namespace vse::audio
{

namespace
{

constexpr u32 kAlsaMaxCards = 32;
constexpr u32 kAlsaMaxDevices = 8;

DeviceStatus StatusFromErrno(int error)
{
    switch (error)
    {
    case 0: return DeviceStatus::Ok;
    case ENOENT:
    case ENXIO: return DeviceStatus::NotFound;
    case EBUSY: return DeviceStatus::Busy;
    case EACCES:
    case EPERM: return DeviceStatus::PermissionDenied;
    case ENODEV:
    case ENOTTY:
    case EBADFD: return DeviceStatus::Lost;
    case EINVAL:
    case ENOSYS: return DeviceStatus::Unsupported;
    case ENOMEM: return DeviceStatus::OutOfMemory;
    default: return DeviceStatus::IoError;
    }
}

snd_mask* MaskOf(snd_pcm_hw_params* params, int var)
{
    return &params->masks[var - SNDRV_PCM_HW_PARAM_FIRST_MASK];
}

snd_interval* IntervalOf(snd_pcm_hw_params* params, int var)
{
    return &params->intervals[var - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
}

// Every value allowed: the starting point of a configuration, as the kernel's own library builds it.
void ParamsAny(snd_pcm_hw_params* params)
{
    MemSet(params, 0, sizeof(*params));
    for (int var = SNDRV_PCM_HW_PARAM_FIRST_MASK; var <= SNDRV_PCM_HW_PARAM_LAST_MASK; ++var)
    {
        snd_mask* mask = MaskOf(params, var);
        mask->bits[0] = 0xFFFFFFFFu;
        mask->bits[1] = 0xFFFFFFFFu;
    }
    for (int var = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; var <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; ++var)
    {
        snd_interval* interval = IntervalOf(params, var);
        interval->min = 0;
        interval->max = 0xFFFFFFFFu;
    }
    params->rmask = 0xFFFFFFFFu;
    params->cmask = 0;
    params->info = 0xFFFFFFFFu;
}

void SetMask(snd_pcm_hw_params* params, int var, u32 bit)
{
    snd_mask* mask = MaskOf(params, var);
    mask->bits[0] = 0;
    mask->bits[1] = 0;
    mask->bits[bit / 32] = 1u << (bit % 32);
}

bool HasBit(const snd_pcm_hw_params* params, int var, u32 bit)
{
    const snd_mask* mask = &params->masks[var - SNDRV_PCM_HW_PARAM_FIRST_MASK];
    return (mask->bits[bit / 32] & (1u << (bit % 32))) != 0;
}

void SetRange(snd_pcm_hw_params* params, int var, u32 low, u32 high)
{
    snd_interval* interval = IntervalOf(params, var);
    interval->min = low;
    interval->max = high;
    interval->openmin = 0;
    interval->openmax = 0;
    interval->integer = 1;
}

u32 MinOf(const snd_pcm_hw_params* params, int var)
{
    return params->intervals[var - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
}

u32 MaxOf(const snd_pcm_hw_params* params, int var)
{
    return params->intervals[var - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max;
}

u32 FormatBit(PcmFormat format)
{
    switch (format)
    {
    case PcmFormat::F32: return static_cast<u32>(SNDRV_PCM_FORMAT_FLOAT_LE);
    case PcmFormat::S32: return static_cast<u32>(SNDRV_PCM_FORMAT_S32_LE);
    case PcmFormat::S16: return static_cast<u32>(SNDRV_PCM_FORMAT_S16_LE);
    }
    return 0;
}

void WriteName(char* out, usize capacity, u32 card, u32 device)
{
    // "hw:<card>,<device>"
    const char prefix[] = "hw:";
    usize n = 0;
    for (; prefix[n] != 0 && n + 1 < capacity; ++n)
    {
        out[n] = prefix[n];
    }
    char digits[12];
    u32 count = 0;
    u32 value = card;
    do
    {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value > 0 && count < 10);
    while (count > 0 && n + 1 < capacity)
    {
        out[n++] = digits[--count];
    }
    if (n + 1 < capacity)
    {
        out[n++] = ',';
    }
    value = device;
    do
    {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value > 0 && count < 10);
    while (count > 0 && n + 1 < capacity)
    {
        out[n++] = digits[--count];
    }
    out[n] = 0;
}

// Reads "hw:<card>,<device>" (or the empty/default name: card -1).
bool ParseAlsaName(StringView name, i32* card, i32* device)
{
    *card = -1;
    *device = 0;
    if (name.Empty() || name == StringView("default"))
    {
        return true;
    }
    if (name.Length() < 6 || name[0] != 'h' || name[1] != 'w' || name[2] != ':')
    {
        return false;
    }
    u32 i = 3;
    i32 value = 0;
    bool any = false;
    while (i < name.Length() && name[i] >= '0' && name[i] <= '9' && value < 1000)
    {
        value = value * 10 + (name[i] - '0');
        any = true;
        i += 1;
    }
    if (!any || i >= name.Length() || name[i] != ',')
    {
        return false;
    }
    *card = value;
    i += 1;
    value = 0;
    any = false;
    while (i < name.Length() && name[i] >= '0' && name[i] <= '9' && value < 1000)
    {
        value = value * 10 + (name[i] - '0');
        any = true;
        i += 1;
    }
    *device = value;
    return any && i == name.Length();
}

int OpenPcm(i32 card, i32 device)
{
    char path[48];
    const char head[] = "/dev/snd/pcmC";
    usize n = 0;
    for (; head[n] != 0; ++n)
    {
        path[n] = head[n];
    }
    auto number = [&](i32 v)
    {
        char digits[12];
        u32 count = 0;
        u32 value = static_cast<u32>(v);
        do
        {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value > 0 && count < 10);
        while (count > 0)
        {
            path[n++] = digits[--count];
        }
    };
    number(card);
    path[n++] = 'D';
    number(device);
    path[n++] = 'p';
    path[n] = 0;
    return os::Open(StringView(path, n), O_RDWR | O_CLOEXEC);
}

u64 NowMicros()
{
    return os::MonotonicTimeNanos() / 1000;
}

class AlsaDevice final : public AudioDevice
{
public:
    ~AlsaDevice() override { Close(); }

    void SetCallbacks(RenderCallback render, DeviceErrorCallback error, void* user) override
    {
        render_ = render;
        error_ = error;
        user_ = user;
    }

    DeviceStatus Open(const DeviceConfig& config) override
    {
        if (fd_ >= 0)
        {
            return DeviceStatus::AlreadyOpen;
        }
        if (config.buffer_frames < 1 || config.buffer_frames > kMaxDeviceBufferFrames ||
            ChannelCount(config.layout) == 0 ||
            (config.sample_rate != 0 && (config.sample_rate < kMinSampleRate || config.sample_rate > kMaxSampleRate)))
        {
            return DeviceStatus::InvalidConfig;
        }
        i32 card = -1;
        i32 device = 0;
        if (!ParseAlsaName(config.name, &card, &device))
        {
            return DeviceStatus::NotFound;
        }
        DeviceStatus status = DeviceStatus::NotFound;
        if (card >= 0)
        {
            status = Configure(card, device, config);
        }
        else
        {
            // The default: the first playback device that opens and accepts the configuration.
            for (u32 c = 0; c < kAlsaMaxCards && fd_ < 0; ++c)
            {
                for (u32 d = 0; d < kAlsaMaxDevices && fd_ < 0; ++d)
                {
                    const DeviceStatus s = Configure(static_cast<i32>(c), static_cast<i32>(d), config);
                    if (s != DeviceStatus::NotFound || status == DeviceStatus::NotFound)
                    {
                        status = s == DeviceStatus::NotFound && status != DeviceStatus::NotFound ? status : s;
                    }
                }
            }
        }
        return fd_ >= 0 ? DeviceStatus::Ok : status;
    }

    DeviceStatus Start() override
    {
        if (fd_ < 0)
        {
            return DeviceStatus::NotOpen;
        }
        if (render_ == nullptr)
        {
            return DeviceStatus::NotOpen;
        }
        if (running_.Load())
        {
            return DeviceStatus::Busy;
        }
        stop_.Store(false);
        lost_.Store(false);
        if (::ioctl(fd_, SNDRV_PCM_IOCTL_PREPARE) < 0)
        {
            return StatusFromErrno(os::LastError());
        }
        thread_ = Thread(&AlsaDevice::Run, this);
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
        if (fd_ >= 0)
        {
            ::ioctl(fd_, SNDRV_PCM_IOCTL_DROP);  // wakes a write that is waiting for room
        }
        if (thread_.Joinable())
        {
            thread_.Join();
        }
        running_.Store(false);
    }

    DeviceStatus Close() override
    {
        Stop();
        if (fd_ >= 0)
        {
            os::Close(fd_);
            fd_ = -1;
        }
        buffer_.Reset();
        raw_.Reset();
        return DeviceStatus::Ok;
    }

    DeviceInfo Info() const override { return info_; }
    bool IsOpen() const override { return fd_ >= 0; }
    bool IsRunning() const override { return running_.Load(); }
    DeviceStatus Pump(u64) override { return DeviceStatus::Unsupported; }
    u64 FramesRendered() const override { return rendered_.Load(); }

    DeviceCounters Counters() const override
    {
        DeviceCounters c;
        c.callbacks = callbacks_.Load();
        c.underruns = underruns_.Load();
        c.slow_callbacks = slow_.Load();
        c.longest_callback_micros = longest_.Load();
        return c;
    }

private:
    // Opens one card and device and negotiates the configuration. Leaves fd_ open on success.
    DeviceStatus Configure(i32 card, i32 device, const DeviceConfig& config)
    {
        const int fd = OpenPcm(card, device);
        if (fd < 0)
        {
            return StatusFromErrno(os::LastError());
        }
        snd_pcm_hw_params params;
        ParamsAny(&params);
        SetMask(&params, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
        SetMask(&params, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
        if (::ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &params) < 0)
        {
            const DeviceStatus s = StatusFromErrno(os::LastError());
            os::Close(fd);
            return s == DeviceStatus::Unsupported ? DeviceStatus::Unsupported : s;
        }
        PcmCaps caps;
        caps.rate_min = MinOf(&params, SNDRV_PCM_HW_PARAM_RATE);
        caps.rate_max = MaxOf(&params, SNDRV_PCM_HW_PARAM_RATE);
        caps.channels_min = MinOf(&params, SNDRV_PCM_HW_PARAM_CHANNELS);
        caps.channels_max = MaxOf(&params, SNDRV_PCM_HW_PARAM_CHANNELS);
        caps.has_f32 = HasBit(&params, SNDRV_PCM_HW_PARAM_FORMAT, FormatBit(PcmFormat::F32));
        caps.has_s32 = HasBit(&params, SNDRV_PCM_HW_PARAM_FORMAT, FormatBit(PcmFormat::S32));
        caps.has_s16 = HasBit(&params, SNDRV_PCM_HW_PARAM_FORMAT, FormatBit(PcmFormat::S16));
        caps.period_min = MinOf(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
        caps.period_max = MaxOf(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
        caps.periods_min = MinOf(&params, SNDRV_PCM_HW_PARAM_PERIODS);
        caps.periods_max = MaxOf(&params, SNDRV_PCM_HW_PARAM_PERIODS);
        caps.buffer_min = MinOf(&params, SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
        caps.buffer_max = MaxOf(&params, SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
        PcmGrant grant;
        const DeviceStatus negotiated = NegotiatePcm(caps, config.sample_rate, ChannelCount(config.layout),
                                                     config.buffer_frames, &grant);
        if (negotiated != DeviceStatus::Ok)
        {
            os::Close(fd);
            return negotiated;
        }
        // Ask for the grant; the kernel refuses a combination its hardware cannot do, then the period is left to it.
        for (u32 attempt = 0; attempt < 2; ++attempt)
        {
            ParamsAny(&params);
            SetMask(&params, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
            SetMask(&params, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
            SetMask(&params, SNDRV_PCM_HW_PARAM_FORMAT, FormatBit(grant.format));
            SetRange(&params, SNDRV_PCM_HW_PARAM_CHANNELS, grant.channels, grant.channels);
            SetRange(&params, SNDRV_PCM_HW_PARAM_RATE, grant.rate, grant.rate);
            if (attempt == 0)
            {
                SetRange(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, grant.period_frames, grant.period_frames);
                SetRange(&params, SNDRV_PCM_HW_PARAM_PERIODS, grant.periods, grant.periods);
            }
            else
            {
                SetRange(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, caps.period_min, caps.period_max);
                SetRange(&params, SNDRV_PCM_HW_PARAM_PERIODS, caps.periods_min, caps.periods_max);
            }
            if (::ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &params) == 0)
            {
                break;
            }
            if (attempt == 1)
            {
                const DeviceStatus s = StatusFromErrno(os::LastError());
                os::Close(fd);
                return s;
            }
        }
        grant.period_frames = MinOf(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
        grant.periods = MinOf(&params, SNDRV_PCM_HW_PARAM_PERIODS);
        const u32 buffer_size = MinOf(&params, SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
        snd_pcm_sw_params sw;
        MemSet(&sw, 0, sizeof(sw));
        sw.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
        sw.period_step = 1;
        sw.avail_min = grant.period_frames;
        sw.start_threshold = buffer_size;
        sw.stop_threshold = buffer_size;
        sw.xfer_align = 1;
        if (::ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw) < 0)
        {
            const DeviceStatus s = StatusFromErrno(os::LastError());
            os::Close(fd);
            return s;
        }
        ChannelLayout layout = config.layout;
        if (grant.channels != ChannelCount(config.layout) && !LayoutOf(grant.channels, &layout))
        {
            os::Close(fd);
            return DeviceStatus::Unsupported;
        }
        const u32 samples = grant.period_frames * grant.channels;
        if (!buffer_.TryResizeUninitialized(samples) ||
            !raw_.TryResizeUninitialized(static_cast<usize>(samples) * PcmSampleBytes(grant.format)))
        {
            os::Close(fd);
            return DeviceStatus::OutOfMemory;
        }
        fd_ = fd;
        grant_ = grant;
        info_.sample_rate = grant.rate;
        info_.layout = layout;
        info_.buffer_frames = grant.period_frames;
        info_.latency_frames = buffer_size;
        rendered_.Store(0);
        callbacks_.Store(0);
        underruns_.Store(0);
        slow_.Store(0);
        longest_.Store(0);
        return DeviceStatus::Ok;
    }

    // A best-effort real-time priority for the render thread; a refusal is not an error.
    static void RequestRealtime()
    {
        sched_param param;
        MemSet(&param, 0, sizeof(param));
        param.sched_priority = 20;
        ::sched_setscheduler(0, SCHED_FIFO | SCHED_RESET_ON_FORK, &param);
    }

    void Run()
    {
        RequestRealtime();
        const u32 channels = grant_.channels;
        const u32 period = grant_.period_frames;
        const u64 period_micros = static_cast<u64>(period) * 1000000ull / grant_.rate;
        while (!stop_.Load())
        {
            const u64 start = NowMicros();
            render_(user_, buffer_.Data(), period);
            const u64 took = NowMicros() - start;
            callbacks_.FetchAdd(1);
            rendered_.FetchAdd(period);
            if (took > period_micros)
            {
                slow_.FetchAdd(1);
            }
            u64 longest = longest_.Load();
            while (took > longest && !longest_.CompareExchangeWeak(longest, took))
            {
            }
            ConvertForPcm(grant_.format, buffer_.Data(), period * channels, raw_.Data());
            if (!WriteAll(period))
            {
                break;
            }
        }
    }

    // Writes one period, recovering from an underrun. False when the device is gone or the device was stopped.
    bool WriteAll(u32 frames)
    {
        const u32 frame_bytes = grant_.channels * PcmSampleBytes(grant_.format);
        u32 done = 0;
        while (done < frames)
        {
            if (stop_.Load())
            {
                return false;
            }
            snd_xferi xfer;
            xfer.result = 0;
            xfer.buf = raw_.Data() + static_cast<usize>(done) * frame_bytes;
            xfer.frames = frames - done;
            if (::ioctl(fd_, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &xfer) == 0)
            {
                done += static_cast<u32>(xfer.result);
                continue;
            }
            const int error = os::LastError();
            if (error == EINTR)
            {
                continue;
            }
            if (error == EPIPE)
            {
                underruns_.FetchAdd(1);
                ::ioctl(fd_, SNDRV_PCM_IOCTL_PREPARE);
                continue;
            }
            if (error == ESTRPIPE)
            {
                if (::ioctl(fd_, SNDRV_PCM_IOCTL_RESUME) < 0)
                {
                    ::ioctl(fd_, SNDRV_PCM_IOCTL_PREPARE);
                }
                continue;
            }
            if (stop_.Load())
            {
                return false;
            }
            lost_.Store(true);
            if (error_ != nullptr)
            {
                error_(user_, error == ENODEV || error == EBADFD ? DeviceStatus::Lost : StatusFromErrno(error));
            }
            return false;
        }
        return true;
    }

    RenderCallback render_ = nullptr;
    DeviceErrorCallback error_ = nullptr;
    void* user_ = nullptr;
    int fd_ = -1;
    PcmGrant grant_;
    DeviceInfo info_;
    Vector<f32> buffer_;
    Vector<u8> raw_;
    Thread thread_;
    Atomic<bool> running_{false};
    Atomic<bool> stop_{false};
    Atomic<bool> lost_{false};
    Atomic<u64> rendered_{0};
    Atomic<u64> callbacks_{0};
    Atomic<u64> underruns_{0};
    Atomic<u64> slow_{0};
    Atomic<u64> longest_{0};
};

}  // namespace

DeviceStatus EnumerateAlsaDevices(Vector<DeviceDescription>* out)
{
    out->Clear();
    Vector<io::DirectoryItem> items;
    if (io::ListDirectory(io::Path(StringView("/dev/snd")), true, &items).Failed())
    {
        return DeviceStatus::Ok;  // no /dev/snd: no sound devices
    }
    for (u32 i = 0; i < items.Size(); ++i)
    {
        const StringView name = items[i].name.View();
        // pcmC<card>D<device>p
        if (name.Length() < 8 || name[0] != 'p' || name[1] != 'c' || name[2] != 'm' || name[3] != 'C' ||
            name[name.Length() - 1] != 'p')
        {
            continue;
        }
        u32 card = 0;
        u32 device = 0;
        u32 k = 4;
        while (k < name.Length() && name[k] >= '0' && name[k] <= '9')
        {
            card = card * 10 + static_cast<u32>(name[k] - '0');
            k += 1;
        }
        if (k >= name.Length() || name[k] != 'D')
        {
            continue;
        }
        k += 1;
        while (k < name.Length() && name[k] >= '0' && name[k] <= '9')
        {
            device = device * 10 + static_cast<u32>(name[k] - '0');
            k += 1;
        }
        DeviceDescription d;
        WriteName(d.name, sizeof(d.name), card, device);
        d.is_default = out->Size() == 0;
        d.max_channels = 8;
        d.min_rate = 8000;
        d.max_rate = 192000;
        out->Push(d);
    }
    return DeviceStatus::Ok;
}

UniquePtr<AudioDevice> CreateAlsaDevice()
{
    return MakeUnique<AlsaDevice>();
}

}  // namespace vse::audio
