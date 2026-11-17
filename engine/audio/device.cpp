#include "audio/device.h"

#include "audio/convert.h"
#include "audio/device_list.h"
#include "audio/wav.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "io/file.h"
#include "lang/utility.h"
#include "build_defs.h"
#if ENGINE_PLATFORM_WINDOWS
#include "audio/windows/wasapi_device.h"
#endif
#if ENGINE_PLATFORM_LINUX
#include "audio/linux/alsa_device.h"
#include "audio/linux/linux_devices.h"
#include "audio/linux/pipewire_device.h"
#endif

namespace engine_placeholder::audio
{

namespace
{

bool IsDefaultName(StringView name)
{
    return name.Empty() || name == StringView("default");
}

// The shared part of the Null and WAV-file devices: they render on demand into a staging buffer, so the two differ only
// in what is done with the samples.
class OnDemandDevice : public AudioDevice
{
public:
    void SetCallbacks(RenderCallback render, DeviceErrorCallback error, void* user) override
    {
        render_ = render;
        error_ = error;
        user_ = user;
    }

    DeviceStatus Open(const DeviceConfig& config) override
    {
        if (open_)
        {
            return DeviceStatus::AlreadyOpen;
        }
        if (!IsValidDeviceConfig(config))
        {
            return DeviceStatus::InvalidConfig;
        }
        if (!IsDefaultName(config.name) && config.name != StringView(DeviceName()))
        {
            return DeviceStatus::NotFound;
        }
        info_.sample_rate = config.sample_rate == 0 ? 48000 : config.sample_rate;
        info_.layout = config.layout;
        info_.buffer_frames = config.buffer_frames;
        info_.latency_frames = 0;
        rendered_ = 0;
        channels_ = ChannelCount(config.layout);
        if (!staging_.TryResizeUninitialized(static_cast<usize>(config.buffer_frames) * channels_))
        {
            return DeviceStatus::OutOfMemory;
        }
        const DeviceStatus status = OnOpen();
        if (status != DeviceStatus::Ok)
        {
            staging_.Reset();
            return status;
        }
        open_ = true;
        return DeviceStatus::Ok;
    }

    DeviceStatus Start() override
    {
        if (!open_ || render_ == nullptr)
        {
            return DeviceStatus::NotOpen;
        }
        if (running_)
        {
            return DeviceStatus::Busy;
        }
        running_ = true;
        return DeviceStatus::Ok;
    }

    void Stop() override { running_ = false; }

    DeviceStatus Close() override
    {
        Stop();
        if (!open_)
        {
            return DeviceStatus::Ok;
        }
        open_ = false;
        const DeviceStatus status = OnClose();
        staging_.Reset();
        return status;
    }

    u64 FramesRendered() const override { return rendered_; }
    DeviceInfo Info() const override { return info_; }
    bool IsOpen() const override { return open_; }
    bool IsRunning() const override { return running_; }

    DeviceStatus Pump(u64 frames) override
    {
        if (!running_)
        {
            return DeviceStatus::NotOpen;
        }
        while (frames > 0)
        {
            const u32 count = frames > info_.buffer_frames ? info_.buffer_frames : static_cast<u32>(frames);
            render_(user_, staging_.Data(), count);
            const DeviceStatus status = OnRendered(staging_.Data(), count);
            if (status != DeviceStatus::Ok)
            {
                running_ = false;
                if (error_ != nullptr)
                {
                    error_(user_, status);
                }
                return status;
            }
            rendered_ += count;
            frames -= count;
        }
        return DeviceStatus::Ok;
    }

protected:
    virtual const char* DeviceName() const = 0;
    virtual DeviceStatus OnOpen() { return DeviceStatus::Ok; }
    virtual DeviceStatus OnClose() { return DeviceStatus::Ok; }
    virtual DeviceStatus OnRendered(const f32* samples, u32 frames) = 0;

    u32 Channels() const { return channels_; }
    u32 Rate() const { return info_.sample_rate; }

private:
    RenderCallback render_ = nullptr;
    DeviceErrorCallback error_ = nullptr;
    void* user_ = nullptr;
    DeviceInfo info_;
    u32 channels_ = 0;
    u64 rendered_ = 0;
    bool open_ = false;
    bool running_ = false;
    Vector<f32> staging_;
};

class NullDevice final : public OnDemandDevice
{
protected:
    const char* DeviceName() const override { return "null"; }
    DeviceStatus OnRendered(const f32*, u32) override { return DeviceStatus::Ok; }
};

void PutU16(Vector<byte>& out, u32 v)
{
    out.Push(static_cast<byte>(v & 0xFF));
    out.Push(static_cast<byte>((v >> 8) & 0xFF));
}

void PutU32(Vector<byte>& out, u32 v)
{
    PutU16(out, v & 0xFFFF);
    PutU16(out, v >> 16);
}

void PutTag(Vector<byte>& out, const char* tag)
{
    for (u32 i = 0; i < 4; ++i)
    {
        out.Push(static_cast<byte>(tag[i]));
    }
}

class WavFileDevice final : public OnDemandDevice
{
public:
    explicit WavFileDevice(const io::Path& path) : path_(path) {}
    // A device that is destroyed while open still writes its file (the error, if any, has no one to report to).
    ~WavFileDevice() override { Close(); }

    DeviceStatus Open(const DeviceConfig& config) override
    {
        if (path_.View().Empty())
        {
            return DeviceStatus::InvalidConfig;
        }
        return OnDemandDevice::Open(config);
    }

protected:
    const char* DeviceName() const override { return "wav-file"; }
    DeviceStatus OnOpen() override
    {
        samples_.Clear();
        return DeviceStatus::Ok;
    }

    DeviceStatus OnRendered(const f32* samples, u32 frames) override
    {
        const usize count = static_cast<usize>(frames) * Channels();
        const usize old_size = samples_.Size();
        if (samples_.Size() / Channels() + frames > kMaxWavFrames || !samples_.TryResizeUninitialized(old_size + count))
        {
            return DeviceStatus::OutOfMemory;
        }
        MemCopy(samples_.Data() + old_size, samples, count * sizeof(f32));
        return DeviceStatus::Ok;
    }

    DeviceStatus OnClose() override
    {
        // A 32-bit float WAV: RIFF, a 16-byte fmt chunk (format 3), and the data chunk.
        const u64 data_bytes = static_cast<u64>(samples_.Size()) * sizeof(f32);
        if (data_bytes + 36 > 0xFFFFFFFFull)
        {
            return DeviceStatus::IoError;
        }
        Vector<byte> file;
        file.Reserve(static_cast<usize>(data_bytes) + 44);
        PutTag(file, "RIFF");
        PutU32(file, static_cast<u32>(data_bytes + 36));
        PutTag(file, "WAVE");
        PutTag(file, "fmt ");
        PutU32(file, 16);
        PutU16(file, 3);
        PutU16(file, Channels());
        PutU32(file, Rate());
        PutU32(file, Rate() * Channels() * 4);
        PutU16(file, Channels() * 4);
        PutU16(file, 32);
        PutTag(file, "data");
        PutU32(file, static_cast<u32>(data_bytes));
        const usize header = file.Size();
        if (!file.TryResizeUninitialized(header + static_cast<usize>(data_bytes)))
        {
            return DeviceStatus::OutOfMemory;
        }
        if (data_bytes > 0)
        {
            MemCopy(file.Data() + header, samples_.Data(), static_cast<usize>(data_bytes));
        }
        const io::Status status = io::WriteFile(path_, ByteSpan(file.Data(), file.Size()));
        samples_.Reset();
        return status.Ok() ? DeviceStatus::Ok : DeviceStatus::IoError;
    }

private:
    io::Path path_;
    Vector<f32> samples_;
};

}  // namespace

bool IsValidDeviceConfig(const DeviceConfig& config)
{
    if (config.sample_rate != 0 && (config.sample_rate < kMinSampleRate || config.sample_rate > kMaxSampleRate))
    {
        return false;
    }
    if (ChannelCount(config.layout) == 0)
    {
        return false;
    }
    return config.buffer_frames >= 1 && config.buffer_frames <= kMaxDeviceBufferFrames;
}

UniquePtr<AudioDevice> CreateDevice(DeviceKind kind)
{
    if (kind == DeviceKind::WavFile)
    {
        return MakeUnique<WavFileDevice>(io::Path());
    }
#if ENGINE_PLATFORM_WINDOWS
    if (kind == DeviceKind::Platform)
    {
        return CreateWasapiDevice();  // Day 440
    }
#endif
#if ENGINE_PLATFORM_LINUX
    if (kind == DeviceKind::Platform)
    {
        return CreateLinuxPlatformDevice();
    }
    if (kind == DeviceKind::PipeWire)
    {
        return CreatePipeWireDevice();
    }
    if (kind == DeviceKind::Alsa)
    {
        return CreateAlsaDevice();
    }
#endif
    return MakeUnique<NullDevice>();
}

UniquePtr<AudioDevice> CreateWavFileDevice(const io::Path& path)
{
    return MakeUnique<WavFileDevice>(path);
}

DeviceStatus EnumerateDevices(DeviceKind kind, Vector<DeviceDescription>* out)
{
    out->Clear();
#if ENGINE_PLATFORM_WINDOWS
    if (kind == DeviceKind::Platform)
    {
        return EnumerateWasapiDevices(out);
    }
#endif
#if ENGINE_PLATFORM_LINUX
    if (kind == DeviceKind::Platform || kind == DeviceKind::PipeWire || kind == DeviceKind::Alsa)
    {
        if (kind == DeviceKind::PipeWire)
        {
            return EnumeratePipeWireDevices(out);
        }
        return kind == DeviceKind::Alsa ? EnumerateAlsaDevices(out) : EnumerateLinuxDevices(out);
    }
#endif
    DeviceDescription d;
    const char* name = kind == DeviceKind::WavFile ? "wav-file" : "null";
    for (u32 i = 0; name[i] != 0; ++i)
    {
        d.name[i] = name[i];
    }
    d.is_default = true;
    d.max_channels = kMaxChannels;
    out->Push(d);
    return DeviceStatus::Ok;
}

}  // namespace engine_placeholder::audio
