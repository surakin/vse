#include "audio/audio_system.h"
#include "log/output.h"

#include "lang/utility.h"
#include "os/console.h"

namespace vse::audio
{

namespace
{

constexpr u64 kMaxPumpFrames = 48000;

bool SameName(const char* a, const char* b)
{
    u32 i = 0;
    while (a[i] != 0 && a[i] == b[i])
    {
        i += 1;
    }
    return a[i] == b[i];
}

}  // namespace

AudioSystem::~AudioSystem()
{
    Shutdown();
}

void AudioSystem::RenderThunk(void* user, f32* out, u32 frames)
{
    static_cast<AudioSystem*>(user)->mixer_.Render(out, frames);
}

void AudioSystem::ErrorThunk(void* user, DeviceStatus)
{
    static_cast<AudioSystem*>(user)->lost_.Store(true);
}

UniquePtr<AudioDevice> AudioSystem::Make(DeviceKind kind)
{
    return config_.factory != nullptr ? config_.factory(config_.factory_user, kind) : CreateDevice(kind);
}

void AudioSystem::LogTransition(const char* what)
{
    transitions_ += 1;
    out::PrintError("audio: %s\n", what);
}

// Opens the wanted device with the current settings and, when the mixer exists, re-plans it for what was granted.
DeviceStatus AudioSystem::OpenWanted(u64 now_nanos)
{
    UniquePtr<AudioDevice> device = Make(config_.kind);
    if (device == nullptr)
    {
        return DeviceStatus::OutOfMemory;
    }
    device->SetCallbacks(&AudioSystem::RenderThunk, &AudioSystem::ErrorThunk, this);
    DeviceConfig dc;
    dc.name = StringView(settings_.device);
    dc.sample_rate = settings_.sample_rate;
    dc.layout = LayoutForChannelCount(settings_.channels);
    dc.buffer_frames = settings_.buffer_frames;
    DeviceStatus status = device->Open(dc);
    if (status != DeviceStatus::Ok)
    {
        return status;
    }
    const DeviceInfo info = device->Info();
    if (!mixer_ready_)
    {
        MixerConfig mc = MixerConfigFromSettings(settings_, info.sample_rate);
        mc.layout = info.layout;
        const MixerStatus ms = mixer_.Init(mc);
        if (ms != MixerStatus::Ok)
        {
            device->Close();
            return ms == MixerStatus::OutOfMemory ? DeviceStatus::OutOfMemory : DeviceStatus::InvalidConfig;
        }
        mixer_ready_ = true;
        mixer_.SetBusGain(kMasterBus, settings_.master_volume);
    }
    else if (mixer_.Config().sample_rate != info.sample_rate || mixer_.Config().layout != info.layout)
    {
        if (mixer_.Reconfigure(info.sample_rate, info.layout) != MixerStatus::Ok)
        {
            device->Close();
            return DeviceStatus::OutOfMemory;
        }
    }
    // Cleared before the start: a device that fails right after starting reports from its own thread, and that report
    // must not be erased.
    lost_.Store(false);
    status = device->Start();
    if (status != DeviceStatus::Ok)
    {
        device->Close();
        return status;
    }
    device_ = Move(device);
    switches_ += 1;
    state_ = OutputState::Device;
    (void)now_nanos;
    return DeviceStatus::Ok;
}

// The null device: it runs at the mixer's own rate and layout and is pumped by `Update`.
void AudioSystem::OpenFallback(u64 now_nanos)
{
    // A failure leaves nothing open (`Closed`), never a stale state with a null or closed device.
    device_ = nullptr;
    state_ = OutputState::Closed;
    UniquePtr<AudioDevice> device = Make(DeviceKind::Null);
    if (device == nullptr)
    {
        return;
    }
    device->SetCallbacks(&AudioSystem::RenderThunk, &AudioSystem::ErrorThunk, this);
    DeviceConfig dc;
    dc.sample_rate = mixer_ready_ ? mixer_.Config().sample_rate
                                  : (settings_.sample_rate == 0 ? 48000 : settings_.sample_rate);
    dc.layout = mixer_ready_ ? mixer_.Config().layout : LayoutForChannelCount(settings_.channels);
    dc.buffer_frames = settings_.buffer_frames;
    if (device->Open(dc) != DeviceStatus::Ok)
    {
        return;
    }
    if (!mixer_ready_)
    {
        MixerConfig mc = MixerConfigFromSettings(settings_, device->Info().sample_rate);
        mc.layout = device->Info().layout;
        if (mixer_.Init(mc) != MixerStatus::Ok)
        {
            device->Close();
            return;
        }
        mixer_ready_ = true;
        mixer_.SetBusGain(kMasterBus, settings_.master_volume);
    }
    device->Start();
    device_ = Move(device);
    switches_ += 1;
    state_ = OutputState::Fallback;
    last_update_ = now_nanos;
    next_retry_ = now_nanos + config_.retry_nanos;
}

DeviceStatus AudioSystem::Init(const AudioSystemConfig& config, u64 now_nanos)
{
    Shutdown();
    config_ = config;
    settings_ = config.settings;
    mixer_ready_ = false;
    const DeviceStatus status = OpenWanted(now_nanos);
    if (status == DeviceStatus::Ok)
    {
        LogTransition("device opened");
        return DeviceStatus::Ok;
    }
    OpenFallback(now_nanos);
    if (state_ != OutputState::Fallback)
    {
        return status == DeviceStatus::Ok ? DeviceStatus::OutOfMemory : status;
    }
    LogTransition("device unavailable: running on the null device");
    return DeviceStatus::Ok;
}

void AudioSystem::Shutdown()
{
    if (device_ != nullptr)
    {
        device_->Stop();
        device_->Close();
        device_ = nullptr;
    }
    state_ = OutputState::Closed;
}

DeviceStatus AudioSystem::ApplySettings(const AudioSettings& settings, u64 now_nanos, ApplyCost* cost)
{
    *cost = ApplyCost::None;
    if (state_ == OutputState::Closed)
    {
        return DeviceStatus::NotOpen;
    }
    const bool rate_or_layout = settings.sample_rate != settings_.sample_rate ||
                                settings.channels != settings_.channels;
    const bool device_changed = !SameName(settings.device, settings_.device) ||
                                settings.buffer_frames != settings_.buffer_frames;
    const bool volume = settings.master_volume != settings_.master_volume;
    settings_ = settings;
    if (!rate_or_layout && !device_changed)
    {
        if (volume)
        {
            mixer_.SetBusGain(kMasterBus, settings.master_volume);
            *cost = ApplyCost::Free;
        }
        return DeviceStatus::Ok;
    }
    *cost = device_changed ? ApplyCost::Reopen : ApplyCost::Replan;
    // The old device is stopped before the mixer is touched: nothing renders while it is re-planned.
    device_->Stop();
    device_->Close();
    device_ = nullptr;
    const DeviceStatus status = OpenWanted(now_nanos);
    if (status != DeviceStatus::Ok)
    {
        OpenFallback(now_nanos);
        LogTransition("the new device could not be opened: running on the null device");
        return status;
    }
    mixer_.SetBusGain(kMasterBus, settings.master_volume);
    return DeviceStatus::Ok;
}

void AudioSystem::Update(u64 now_nanos)
{
    if (state_ == OutputState::Device)
    {
        if (!lost_.Load())
        {
            return;
        }
        // The device reported its loss from its own thread; the control thread does the switching.
        device_->Stop();
        device_->Close();
        device_ = nullptr;
        LogTransition("device lost: running on the null device");
        OpenFallback(now_nanos);
        return;
    }
    if (state_ != OutputState::Fallback)
    {
        return;
    }
    // The clock goes on: pump the null device for the time that passed.
    const u64 elapsed = now_nanos > last_update_ ? now_nanos - last_update_ : 0;
    last_update_ = now_nanos;
    u64 frames = elapsed * mixer_.Config().sample_rate / 1000000000ull;
    frames = frames > kMaxPumpFrames ? kMaxPumpFrames : frames;
    if (frames > 0)
    {
        device_->Pump(frames);
    }
    if (now_nanos < next_retry_)
    {
        return;
    }
    next_retry_ = now_nanos + config_.retry_nanos;
    UniquePtr<AudioDevice> fallback = Move(device_);
    fallback->Stop();
    fallback->Close();
    if (OpenWanted(now_nanos) == DeviceStatus::Ok)
    {
        LogTransition("device recovered");
        return;
    }
    // Still not there: back to the null device (the mixer kept its format).
    device_ = Move(fallback);
    OpenFallback(now_nanos);
}

}  // namespace vse::audio
