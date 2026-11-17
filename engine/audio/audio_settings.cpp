#include "audio/audio_settings.h"

#include "cstr/mem_ops.h"
#include "settings/setting.h"

namespace engine_placeholder::audio
{

namespace
{

const char* const kResamplerNames[] = {"linear", "hermite", "sinc"};

void Declare(settings::Registry* registry, settings::SettingDesc desc, const char* env, const char* flag,
             const char* label, const char* description, u32 flags)
{
    desc.env = env;
    desc.flag = flag;
    desc.label = label;
    desc.description = description;
    desc.group = "audio";
    desc.flags = flags;
    registry->Register(desc);
}

}  // namespace

void RegisterAudioSettings(settings::Registry* registry)
{
    Declare(registry, settings::MakeText("audio.device", "default"), "ENGINE_AUDIO_DEVICE", "audio-device",
            "Audio output", "the sound output's name; default is the system's", settings::kRestartRequired);
    Declare(registry, settings::MakeInt("audio.sample_rate", 48000, 0, 384000), "ENGINE_AUDIO_SAMPLE_RATE",
            "audio-sample-rate", "Sample rate", "Hz; 0 takes the device's own rate", settings::kRestartRequired);
    Declare(registry, settings::MakeInt("audio.buffer_frames", 1024, 64, 16384), "ENGINE_AUDIO_BUFFER_FRAMES",
            "audio-buffer-frames", "Audio buffer",
            "frames per render call: smaller is lower latency and more risk of a dropout", settings::kRestartRequired);
    Declare(registry, settings::MakeInt("audio.channels", 2, 1, 8), "ENGINE_AUDIO_CHANNELS", "audio-channels",
            "Output channels", "1, 2, 4, 6 or 8 (another count plays as stereo)", settings::kRestartRequired);
    Declare(registry, settings::MakeFloat("audio.master_volume", 1.0, 0.0, 4.0), "ENGINE_AUDIO_MASTER_VOLUME",
            "audio-master-volume", "Master volume", "a linear gain applied to the whole mix", 0);
    Declare(registry, settings::MakeInt("audio.max_voices", 64, 1, 4096), "ENGINE_AUDIO_MAX_VOICES", "audio-max-voices",
            "Voice limit", "the most sounds playing at once; the quietest is dropped first", 0);
    Declare(registry, settings::MakeEnum("audio.resampler_quality", kResamplerNames, 3, 1),
            "ENGINE_AUDIO_RESAMPLER_QUALITY", "audio-resampler-quality", "Resampler",
            "linear, hermite or sinc: how a clip at another rate is converted", settings::kAdvanced);
}

AudioSettings AudioSettingsFromRegistry(const settings::Registry& registry)
{
    AudioSettings s;
    const settings::SettingId device = registry.Find(StringView("audio.device"));
    if (device.IsValid())
    {
        char text[sizeof(s.device)];
        const usize length = settings::TextSetting(registry, device).Get(text, sizeof(text));
        if (length > 0 && length < sizeof(text))
        {
            MemCopy(s.device, text, length);
            s.device[length] = 0;
        }
    }
    const settings::SettingId rate = registry.Find(StringView("audio.sample_rate"));
    if (rate.IsValid())
    {
        s.sample_rate = static_cast<u32>(settings::Setting<i64>(registry, rate).Get());
    }
    const settings::SettingId buffer = registry.Find(StringView("audio.buffer_frames"));
    if (buffer.IsValid())
    {
        s.buffer_frames = static_cast<u32>(settings::Setting<i64>(registry, buffer).Get());
    }
    const settings::SettingId channels = registry.Find(StringView("audio.channels"));
    if (channels.IsValid())
    {
        s.channels = static_cast<u32>(settings::Setting<i64>(registry, channels).Get());
    }
    const settings::SettingId volume = registry.Find(StringView("audio.master_volume"));
    if (volume.IsValid())
    {
        s.master_volume = static_cast<f32>(settings::Setting<f64>(registry, volume).Get());
    }
    const settings::SettingId voices = registry.Find(StringView("audio.max_voices"));
    if (voices.IsValid())
    {
        s.max_voices = static_cast<u32>(settings::Setting<i64>(registry, voices).Get());
    }
    const settings::SettingId quality = registry.Find(StringView("audio.resampler_quality"));
    if (quality.IsValid())
    {
        s.resampler = settings::Setting<ResamplerQuality>(registry, quality).Get();
    }
    return s;
}

ChannelLayout LayoutForChannelCount(u32 channels)
{
    switch (channels)
    {
    case 1: return ChannelLayout::Mono;
    case 4: return ChannelLayout::Quad;
    case 6: return ChannelLayout::Surround51;
    case 8: return ChannelLayout::Surround71;
    default: return ChannelLayout::Stereo;
    }
}

DeviceConfig DeviceConfigFromSettings(const AudioSettings& settings)
{
    DeviceConfig config;
    config.name = StringView(settings.device);
    config.sample_rate = settings.sample_rate;
    config.layout = LayoutForChannelCount(settings.channels);
    config.buffer_frames = settings.buffer_frames;
    return config;
}

}  // namespace engine_placeholder::audio
