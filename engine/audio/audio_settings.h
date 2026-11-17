// The audio settings (Phase 26 Day 419): the device, the rate, the buffer size, the channel count, the master volume,
// the voice limit and the resampler quality, declared with `settings::Registry`.
#pragma once

#include "audio/device.h"
#include "settings/registry.h"

namespace engine_placeholder::audio
{

/// The quality of the sample-rate conversion a voice uses (Day 420 implements all three).
enum class ResamplerQuality : u32
{
    /// Linear interpolation: cheapest.
    Linear,
    /// Four-point Hermite interpolation: the default.
    Hermite,
    /// A windowed-sinc filter: best, most expensive.
    Sinc,
};

/// The audio settings as the mixer and the device read them.
///
/// - Thread-safety: thread-compatible plain data.
struct AudioSettings
{
    /// The device name; `default` is the system's default.
    char device[96] = {'d', 'e', 'f', 'a', 'u', 'l', 't', 0};
    /// The wanted sample rate; 0 takes the device's own.
    u32 sample_rate = 48000;
    /// The frames per render call.
    u32 buffer_frames = 1024;
    /// The output channel count (1, 2, 4, 6 or 8).
    u32 channels = 2;
    /// The master volume as a linear gain, 0 to 4.
    f32 master_volume = 1.0f;
    /// The most voices playing at once.
    u32 max_voices = 64;
    /// The resampler.
    ResamplerQuality resampler = ResamplerQuality::Hermite;
};

/// Declares the audio settings, each with an environment variable and a flag.
///
/// They are `audio.device`, `audio.sample_rate`, `audio.buffer_frames`, `audio.channels` (these four need a device
/// restart, `kRestartRequired`), `audio.master_volume`, `audio.max_voices` and `audio.resampler_quality` (live).
///
/// - Param registry: the registry to declare them in; not null.
/// - Ownership: borrowed for the call.
/// - Fails: never.
void RegisterAudioSettings(settings::Registry* registry);

/// Reads the audio settings from a registry.
///
/// - Param registry: a registry in which `RegisterAudioSettings` was called; a missing setting keeps its default.
/// - Returns: the settings.
/// - Fails: never.
AudioSettings AudioSettingsFromRegistry(const settings::Registry& registry);

/// The channel layout for a channel count of the settings.
///
/// - Param channels: 1, 2, 4, 6 or 8.
/// - Returns: the layout; stereo for any other count.
/// - Fails: never.
ChannelLayout LayoutForChannelCount(u32 channels);

/// The device configuration the settings ask for.
///
/// - Param settings: the settings.
/// - Returns: a config whose `name` points into `settings.device`, so keep `settings` alive while it is used.
/// - Fails: never.
DeviceConfig DeviceConfigFromSettings(const AudioSettings& settings);

}  // namespace engine_placeholder::audio
