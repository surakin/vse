// The device interface (Phase 26 Day 419): the null device, the WAV-file device, refused configurations and the
// settings.
#include <catch2/catch_test_macros.hpp>

#include "audio/audio_settings.h"
#include "audio/device.h"
#include "audio/device_list.h"
#include "audio/wav.h"
#include "io/file.h"
#include "io/file_system.h"
#include "os/console.h"
#include "settings/settings.h"
#include "wav_builder.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;

namespace
{

// A render callback that writes a running ramp (the frame index in channel 0, its negation in channel 1) and counts
// calls.
struct RampSource
{
    u64 next_frame = 0;
    u32 calls = 0;
    u32 largest = 0;
    u32 channels = 2;
    u32 errors = 0;
    DeviceStatus last_error = DeviceStatus::Ok;
};

void RenderRamp(void* user, f32* out, u32 frames)
{
    RampSource* source = static_cast<RampSource*>(user);
    for (u32 i = 0; i < frames; ++i)
    {
        const f32 v = static_cast<f32>(source->next_frame + i) / 65536.0f;
        for (u32 c = 0; c < source->channels; ++c)
        {
            out[i * source->channels + c] = (c & 1) ? -v : v;
        }
    }
    source->next_frame += frames;
    source->calls += 1;
    if (frames > source->largest)
    {
        source->largest = frames;
    }
}

void NoteError(void* user, DeviceStatus status)
{
    RampSource* source = static_cast<RampSource*>(user);
    source->errors += 1;
    source->last_error = status;
}

class AudioTempDir
{
public:
    AudioTempDir()
    {
        io::Path tmp;
        if (io::TempDirectory(&tmp).Failed() || io::CreateTempDirectory(tmp, "re_audio_test", &path_).Failed())
        {
            os::Abort();
        }
    }
    ~AudioTempDir() { io::RemoveAll(path_); }
    AudioTempDir(const AudioTempDir&) = delete;
    AudioTempDir& operator=(const AudioTempDir&) = delete;
    io::Path File(StringView name) const { return path_.Join(name); }

private:
    io::Path path_;
};

}  // namespace

TEST_CASE("AudioDevice: the null device calls back the requested frames in order, in buffer-sized calls",
          "[audio][device][null]")
{
    UniquePtr<AudioDevice> device = CreateDevice(DeviceKind::Null);
    RampSource source;
    device->SetCallbacks(RenderRamp, NoteError, &source);
    DeviceConfig config;
    config.buffer_frames = 256;
    REQUIRE(device->Open(config) == DeviceStatus::Ok);
    CHECK(device->Info().sample_rate == 48000);
    CHECK(device->Info().buffer_frames == 256);
    REQUIRE(device->Start() == DeviceStatus::Ok);
    CHECK(device->IsRunning());
    CHECK(device->Pump(1000) == DeviceStatus::Ok);
    CHECK(source.next_frame == 1000);
    CHECK(source.calls == 4);  // 256 + 256 + 256 + 232
    CHECK(source.largest == 256);
    CHECK(device->FramesRendered() == 1000);
    device->Stop();
    CHECK(device->Pump(10) == DeviceStatus::NotOpen);
    CHECK(device->Close() == DeviceStatus::Ok);
    CHECK(device->Close() == DeviceStatus::Ok);
    CHECK(!device->IsOpen());
}

TEST_CASE("AudioDevice: a device refuses a configuration and says why", "[audio][device][status]")
{
    UniquePtr<AudioDevice> device = CreateDevice(DeviceKind::Null);
    DeviceConfig config;
    config.sample_rate = 100;
    CHECK(device->Open(config) == DeviceStatus::InvalidConfig);
    config = DeviceConfig();
    config.buffer_frames = 0;
    CHECK(device->Open(config) == DeviceStatus::InvalidConfig);
    config.buffer_frames = kMaxDeviceBufferFrames + 1;
    CHECK(device->Open(config) == DeviceStatus::InvalidConfig);
    config = DeviceConfig();
    config.name = StringView("no such card");
    CHECK(device->Open(config) == DeviceStatus::NotFound);
    CHECK(device->Start() == DeviceStatus::NotOpen);
    config = DeviceConfig();
    REQUIRE(device->Open(config) == DeviceStatus::Ok);
    CHECK(device->Open(config) == DeviceStatus::AlreadyOpen);
    CHECK(device->Start() == DeviceStatus::NotOpen);  // no render callback
    RampSource source;
    device->SetCallbacks(RenderRamp, nullptr, &source);
    REQUIRE(device->Start() == DeviceStatus::Ok);
    CHECK(device->Start() == DeviceStatus::Busy);
    CHECK(device->Close() == DeviceStatus::Ok);
    // It can be opened again after Close, and a rate of 0 takes the device's own.
    config.sample_rate = 0;
    CHECK(device->Open(config) == DeviceStatus::Ok);
    CHECK(device->Info().sample_rate == 48000);
}

TEST_CASE("AudioDevice: the file device writes a WAV that the parser reads back", "[audio][device][wavfile]")
{
    AudioTempDir dir;
    const io::Path path = dir.File(StringView("out.wav"));
    UniquePtr<AudioDevice> device = CreateWavFileDevice(path);
    RampSource source;
    source.channels = 2;
    device->SetCallbacks(RenderRamp, NoteError, &source);
    DeviceConfig config;
    config.sample_rate = 44100;
    config.buffer_frames = 300;
    REQUIRE(device->Open(config) == DeviceStatus::Ok);
    REQUIRE(device->Start() == DeviceStatus::Ok);
    REQUIRE(device->Pump(1000) == DeviceStatus::Ok);
    REQUIRE(device->Close() == DeviceStatus::Ok);

    Vector<u8> bytes;
    REQUIRE(io::ReadFile(path, &bytes).Ok());
    WavInfo info;
    REQUIRE(ParseWav(ByteSpan(reinterpret_cast<const byte*>(bytes.Data()), bytes.Size()), &info) == WavStatus::Ok);
    CHECK(info.format.sample_rate == 44100);
    CHECK(info.format.layout == ChannelLayout::Stereo);
    CHECK(info.frame_count == 1000);
    CHECK(info.encoding == WavEncoding::F32);
    AudioClipPtr clip;
    WavInfo again;
    REQUIRE(DecodeWav(ByteSpan(reinterpret_cast<const byte*>(bytes.Data()), bytes.Size()), StringView("out"), &clip,
                      &again) == WavStatus::Ok);
    const audio::ClipView view = clip->View();
    CHECK(view.Frame(0)[0] == 0.0f);
    CHECK(view.Frame(999)[0] == 999.0f / 65536.0f);
    CHECK(view.Frame(999)[1] == -999.0f / 65536.0f);
}

TEST_CASE("AudioDevice: a file device that cannot write reports IoError from Close", "[audio][device][wavfile]")
{
    AudioTempDir dir;
    UniquePtr<AudioDevice> device = CreateWavFileDevice(dir.File(StringView("missing/dir/out.wav")));
    RampSource source;
    device->SetCallbacks(RenderRamp, nullptr, &source);
    REQUIRE(device->Open(DeviceConfig()) == DeviceStatus::Ok);
    REQUIRE(device->Start() == DeviceStatus::Ok);
    REQUIRE(device->Pump(10) == DeviceStatus::Ok);
    CHECK(device->Close() == DeviceStatus::IoError);
    // A WAV-file device from CreateDevice has no path.
    UniquePtr<AudioDevice> pathless = CreateDevice(DeviceKind::WavFile);
    CHECK(pathless->Open(DeviceConfig()) == DeviceStatus::InvalidConfig);
}

TEST_CASE("AudioDevice: the null and file devices cannot be told apart through the interface", "[audio][device]")
{
    AudioTempDir dir;
    UniquePtr<AudioDevice> devices[2] = {CreateDevice(DeviceKind::Null),
                                         CreateWavFileDevice(dir.File(StringView("a.wav")))};
    RampSource sources[2];
    for (u32 i = 0; i < 2; ++i)
    {
        devices[i]->SetCallbacks(RenderRamp, nullptr, &sources[i]);
        REQUIRE(devices[i]->Open(DeviceConfig()) == DeviceStatus::Ok);
        REQUIRE(devices[i]->Start() == DeviceStatus::Ok);
        REQUIRE(devices[i]->Pump(5000) == DeviceStatus::Ok);
        CHECK(devices[i]->FramesRendered() == 5000);
        CHECK(devices[i]->Close() == DeviceStatus::Ok);
    }
    CHECK(sources[0].calls == sources[1].calls);
}

TEST_CASE("AudioDevice: the device list reports one fake default device per on-demand kind", "[audio][device][list]")
{
    Vector<DeviceDescription> list;
    REQUIRE(EnumerateDevices(DeviceKind::Null, &list) == DeviceStatus::Ok);
    REQUIRE(list.Size() == 1);
    CHECK(StringView(list[0].name) == StringView("null"));
    CHECK(list[0].is_default);
    REQUIRE(EnumerateDevices(DeviceKind::WavFile, &list) == DeviceStatus::Ok);
    REQUIRE(list.Size() == 1);
    CHECK(StringView(list[0].name) == StringView("wav-file"));
}

TEST_CASE("Settings: the audio settings are declared, read back and changed", "[audio][settings]")
{
    settings::Registry registry;
    RegisterAudioSettings(&registry);
    AudioSettings s = AudioSettingsFromRegistry(registry);
    CHECK(StringView(s.device) == StringView("default"));
    CHECK(s.sample_rate == 48000);
    CHECK(s.buffer_frames == 1024);
    CHECK(s.channels == 2);
    CHECK(s.master_volume == 1.0f);
    CHECK(s.max_voices == 64);
    CHECK(s.resampler == ResamplerQuality::Hermite);
    CHECK(s.ramp_frames == 64);

    const settings::SettingId channels = registry.Find(StringView("audio.channels"));
    REQUIRE(channels.IsValid());
    CHECK((registry.Desc(channels)->flags & settings::kRestartRequired) != 0);
    const settings::SettingId volume = registry.Find(StringView("audio.master_volume"));
    REQUIRE(volume.IsValid());
    CHECK((registry.Desc(volume)->flags & settings::kRestartRequired) == 0);
    CHECK(StringView(registry.Desc(volume)->env) == StringView("ENGINE_AUDIO_MASTER_VOLUME"));
    CHECK(StringView(registry.Desc(volume)->flag) == StringView("audio-master-volume"));

    settings::Settings service(registry);
    CHECK(service.SetText(StringView("audio.channels"), StringView("6")).Ok());
    CHECK(service.SetText(StringView("audio.master_volume"), StringView("0.5")).Ok());
    CHECK(service.SetText(StringView("audio.resampler_quality"), StringView("sinc")).Ok());
    CHECK(service.SetText(StringView("audio.device"), StringView("hdmi")).Ok());
    s = AudioSettingsFromRegistry(registry);
    CHECK(s.channels == 6);
    CHECK(s.master_volume == 0.5f);
    CHECK(s.resampler == ResamplerQuality::Sinc);
    CHECK(StringView(s.device) == StringView("hdmi"));
    const DeviceConfig config = DeviceConfigFromSettings(s);
    CHECK(config.layout == ChannelLayout::Surround51);
    CHECK(config.name == StringView("hdmi"));
    CHECK(LayoutForChannelCount(3) == ChannelLayout::Stereo);
}

TEST_CASE("Device: a file device destroyed while open still writes its file", "[audio][device][wavfile]")
{
    AudioTempDir dir;
    const io::Path path = dir.File(StringView("dropped.wav"));
    {
        UniquePtr<AudioDevice> device = CreateWavFileDevice(path);
        RampSource source;
        device->SetCallbacks(RenderRamp, nullptr, &source);
        REQUIRE(device->Open(DeviceConfig()) == DeviceStatus::Ok);
        REQUIRE(device->Start() == DeviceStatus::Ok);
        REQUIRE(device->Pump(300) == DeviceStatus::Ok);
    }
    Vector<u8> bytes;
    REQUIRE(io::ReadFile(path, &bytes).Ok());
    WavInfo info;
    REQUIRE(ParseWav(ByteSpan(reinterpret_cast<const byte*>(bytes.Data()), bytes.Size()), &info) == WavStatus::Ok);
    CHECK(info.frame_count == 300);
}
