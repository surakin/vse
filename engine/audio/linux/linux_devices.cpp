#include "audio/linux/linux_devices.h"

#include "audio/linux/alsa_device.h"
#include "audio/linux/pipewire_device.h"
#include "lang/utility.h"

namespace engine_placeholder::audio
{

namespace
{

bool IsDefaultName(StringView name)
{
    return name.Empty() || name == StringView("default");
}

// The default device on Linux: it tries the backends in order on `Open` and then forwards to the one that opened.
class LinuxPlatformDevice final : public AudioDevice
{
public:
    ~LinuxPlatformDevice() override { Close(); }

    void SetCallbacks(RenderCallback render, DeviceErrorCallback error, void* user) override
    {
        render_ = render;
        error_ = error;
        user_ = user;
    }

    DeviceStatus Open(const DeviceConfig& config) override
    {
        if (active_ != nullptr)
        {
            return DeviceStatus::AlreadyOpen;
        }
        DeviceStatus best = DeviceStatus::NotFound;
        const bool want_pipewire = IsDefaultName(config.name) || config.name == StringView("pipewire");
        const bool want_alsa = IsDefaultName(config.name) || (config.name.Length() > 3 && config.name[0] == 'h');
        for (u32 attempt = 0; attempt < 2; ++attempt)
        {
            if ((attempt == 0 && (!want_pipewire || !PipeWireAvailable())) || (attempt == 1 && !want_alsa))
            {
                continue;
            }
            UniquePtr<AudioDevice> device = attempt == 0 ? CreatePipeWireDevice() : CreateAlsaDevice();
            if (device == nullptr)
            {
                return DeviceStatus::OutOfMemory;
            }
            device->SetCallbacks(render_, error_, user_);
            const DeviceStatus status = device->Open(config);
            if (status == DeviceStatus::Ok)
            {
                active_ = Move(device);
                return DeviceStatus::Ok;
            }
            // A failure that says more than "not there" wins over one that does not.
            if (best == DeviceStatus::NotFound || status != DeviceStatus::NotFound)
            {
                best = status;
            }
            if (status == DeviceStatus::InvalidConfig)
            {
                return status;
            }
        }
        return best;
    }

    DeviceStatus Start() override { return active_ != nullptr ? active_->Start() : DeviceStatus::NotOpen; }

    void Stop() override
    {
        if (active_ != nullptr)
        {
            active_->Stop();
        }
    }

    DeviceStatus Close() override
    {
        if (active_ != nullptr)
        {
            active_->Close();
            active_ = nullptr;
        }
        return DeviceStatus::Ok;
    }

    DeviceInfo Info() const override { return active_ != nullptr ? active_->Info() : DeviceInfo(); }
    bool IsOpen() const override { return active_ != nullptr && active_->IsOpen(); }
    bool IsRunning() const override { return active_ != nullptr && active_->IsRunning(); }
    DeviceStatus Pump(u64) override { return DeviceStatus::Unsupported; }
    u64 FramesRendered() const override { return active_ != nullptr ? active_->FramesRendered() : 0; }
    DeviceCounters Counters() const override { return active_ != nullptr ? active_->Counters() : DeviceCounters(); }

private:
    RenderCallback render_ = nullptr;
    DeviceErrorCallback error_ = nullptr;
    void* user_ = nullptr;
    UniquePtr<AudioDevice> active_;
};

}  // namespace

UniquePtr<AudioDevice> CreateLinuxPlatformDevice()
{
    return MakeUnique<LinuxPlatformDevice>();
}

DeviceStatus EnumerateLinuxDevices(Vector<DeviceDescription>* out)
{
    out->Clear();
    Vector<DeviceDescription> list;
    DeviceStatus status = EnumeratePipeWireDevices(&list);
    if (status != DeviceStatus::Ok)
    {
        return status;
    }
    for (u32 i = 0; i < list.Size(); ++i)
    {
        out->Push(list[i]);
    }
    status = EnumerateAlsaDevices(&list);
    if (status != DeviceStatus::Ok)
    {
        return status;
    }
    for (u32 i = 0; i < list.Size(); ++i)
    {
        DeviceDescription d = list[i];
        d.is_default = out->Size() == 0 && d.is_default;
        out->Push(d);
    }
    return DeviceStatus::Ok;
}

}  // namespace engine_placeholder::audio
