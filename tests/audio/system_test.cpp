// The audio system (Phase 26 Day 427): a changed rate mid-clip, a lost device and its recovery, every setting applied
// in turn, and a device count that does not grow over many resets. A test-local fake device stands in for the sound
// card.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/audio_system.h"
#include "containers/vector.h"

using namespace vse;
using namespace vse::audio;

namespace
{

struct FakeWorld
{
    u32 grant_rate = 0;  // 0: grant what was asked
    bool fail_open = false;
    i32 alive = 0;
    u32 created = 0;
    DeviceErrorCallback last_error = nullptr;
    void* last_user = nullptr;
};

class FakeDevice final : public AudioDevice
{
public:
    FakeDevice(FakeWorld* world, bool null_kind) : world_(world), null_kind_(null_kind)
    {
        world_->alive += 1;
        world_->created += 1;
    }
    ~FakeDevice() override { world_->alive -= 1; }
    void SetCallbacks(RenderCallback render, DeviceErrorCallback error, void* user) override
    {
        render_ = render;
        error_ = error;
        user_ = user;
        world_->last_error = error;
        world_->last_user = user;
    }
    DeviceStatus Open(const DeviceConfig& config) override
    {
        if (world_->fail_open && !null_kind_)
        {
            return DeviceStatus::NotFound;
        }
        info_.sample_rate = world_->grant_rate != 0 && !null_kind_ ? world_->grant_rate : config.sample_rate;
        info_.layout = config.layout;
        info_.buffer_frames = config.buffer_frames;
        open_ = true;
        return DeviceStatus::Ok;
    }
    DeviceStatus Start() override
    {
        running_ = true;
        return DeviceStatus::Ok;
    }
    void Stop() override { running_ = false; }
    DeviceStatus Close() override
    {
        running_ = false;
        open_ = false;
        return DeviceStatus::Ok;
    }
    DeviceInfo Info() const override { return info_; }
    bool IsOpen() const override { return open_; }
    bool IsRunning() const override { return running_; }
    DeviceStatus Pump(u64 frames) override
    {
        Vector<f32> buffer;
        const u32 channels = ChannelCount(info_.layout);
        buffer.Resize(static_cast<usize>(info_.buffer_frames) * channels);
        while (frames > 0)
        {
            const u32 n = frames > info_.buffer_frames ? info_.buffer_frames : static_cast<u32>(frames);
            render_(user_, buffer.Data(), n);
            frames -= n;
            rendered_ += n;
        }
        return DeviceStatus::Ok;
    }
    u64 FramesRendered() const override { return rendered_; }

private:
    FakeWorld* world_;
    bool null_kind_;
    RenderCallback render_ = nullptr;
    DeviceErrorCallback error_ = nullptr;
    void* user_ = nullptr;
    DeviceInfo info_;
    bool open_ = false;
    bool running_ = false;
    u64 rendered_ = 0;
};

UniquePtr<AudioDevice> FakeFactory(void* user, DeviceKind kind)
{
    return MakeUnique<FakeDevice>(static_cast<FakeWorld*>(user), kind == DeviceKind::Null);
}

AudioClipPtr SysClip(u32 frames)
{
    Vector<f32> samples;
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(0.25f);
    }
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "sys", &clip) ==
            ClipStatus::Ok);
    return clip;
}

AudioSystemConfig SysConfig(FakeWorld* world)
{
    AudioSystemConfig config;
    config.factory = &FakeFactory;
    config.factory_user = world;
    config.retry_nanos = 1000000000ull;
    return config;
}

constexpr u64 kSecond = 1000000000ull;

}  // namespace

TEST_CASE("audio system: starts on the fake device and plays", "[audio][system]")
{
    FakeWorld world;
    AudioSystem system;
    REQUIRE(system.Init(SysConfig(&world), 0) == DeviceStatus::Ok);
    CHECK(system.State() == OutputState::Device);
    VoiceDesc desc;
    desc.clip = SysClip(48000);
    VoiceId id;
    REQUIRE(system.GetMixer().Play(desc, &id) == MixerStatus::Ok);
    const_cast<AudioDevice*>(system.CurrentDevice())->Pump(4800);
    f64 position = 0;
    REQUIRE(system.GetMixer().Position(id, &position) == MixerStatus::Ok);
    CHECK(position > 4000.0);
}

TEST_CASE("audio system: a rate change mid-clip keeps the voice and its place", "[audio][system]")
{
    FakeWorld world;
    AudioSystem system;
    REQUIRE(system.Init(SysConfig(&world), 0) == DeviceStatus::Ok);
    VoiceDesc desc;
    desc.clip = SysClip(96000);
    VoiceId id;
    REQUIRE(system.GetMixer().Play(desc, &id) == MixerStatus::Ok);
    const_cast<AudioDevice*>(system.CurrentDevice())->Pump(4800);
    f64 before = 0;
    REQUIRE(system.GetMixer().Position(id, &before) == MixerStatus::Ok);

    AudioSettings settings;
    settings.sample_rate = 44100;
    ApplyCost cost = ApplyCost::None;
    REQUIRE(system.ApplySettings(settings, 0, &cost) == DeviceStatus::Ok);
    CHECK(cost == ApplyCost::Replan);
    CHECK(system.GetMixer().Config().sample_rate == 44100);
    f64 after = 0;
    REQUIRE(system.GetMixer().Position(id, &after) == MixerStatus::Ok);
    CHECK(after == Catch::Approx(before).margin(300.0));
    // The clip still plays at the right pitch: 44100 device frames consume 48000 clip frames.
    const_cast<AudioDevice*>(system.CurrentDevice())->Pump(4410);
    f64 later = 0;
    REQUIRE(system.GetMixer().Position(id, &later) == MixerStatus::Ok);
    CHECK(later - after == Catch::Approx(4800.0).margin(300.0));
}

TEST_CASE("audio system: a granted rate that differs from the wanted one is what the mixer uses", "[audio][system]")
{
    FakeWorld world;
    world.grant_rate = 44100;
    AudioSystem system;
    REQUIRE(system.Init(SysConfig(&world), 0) == DeviceStatus::Ok);
    CHECK(system.GetMixer().Config().sample_rate == 44100);
}

TEST_CASE("audio system: a lost device falls back to null, keeps the clock and recovers", "[audio][system]")
{
    FakeWorld world;
    AudioSystem system;
    REQUIRE(system.Init(SysConfig(&world), 0) == DeviceStatus::Ok);
    VoiceDesc desc;
    desc.clip = SysClip(480000);
    VoiceId id;
    REQUIRE(system.GetMixer().Play(desc, &id) == MixerStatus::Ok);
    const_cast<AudioDevice*>(system.CurrentDevice())->Pump(512);
    const u32 transitions = system.Transitions();

    // The device reports its loss from its own thread.
    world.last_error(world.last_user, DeviceStatus::Lost);
    world.fail_open = true;
    u64 now = 0;
    system.Update(now);
    CHECK(system.State() == OutputState::Fallback);
    CHECK(system.Transitions() == transitions + 1);

    // 100 blocks' worth of time passes on the null device.
    f64 start = 0;
    REQUIRE(system.GetMixer().Position(id, &start) == MixerStatus::Ok);
    for (u32 i = 0; i < 100; ++i)
    {
        now += 256ull * kSecond / 48000;
        system.Update(now);
        if (now >= kSecond)
        {
            break;
        }
    }
    f64 moved = 0;
    REQUIRE(system.GetMixer().Position(id, &moved) == MixerStatus::Ok);
    CHECK(moved > start + 10000.0);
    CHECK(system.State() == OutputState::Fallback);

    // The retry fails while the device is gone and is logged only once; then the device returns.
    now += 2 * kSecond;
    system.Update(now);
    CHECK(system.State() == OutputState::Fallback);
    CHECK(system.Transitions() == transitions + 1);
    world.fail_open = false;
    now += 2 * kSecond;
    system.Update(now);
    CHECK(system.State() == OutputState::Device);
    CHECK(system.Transitions() == transitions + 2);
    CHECK(system.GetMixer().IsPlaying(id));
}

TEST_CASE("audio system: a device that cannot open at start runs on the null device", "[audio][system]")
{
    FakeWorld world;
    world.fail_open = true;
    AudioSystem system;
    REQUIRE(system.Init(SysConfig(&world), 0) == DeviceStatus::Ok);
    CHECK(system.State() == OutputState::Fallback);
    world.fail_open = false;
    system.Update(2 * kSecond);
    CHECK(system.State() == OutputState::Device);
}

TEST_CASE("audio system: every setting applies in turn and the master volume is live", "[audio][system]")
{
    FakeWorld world;
    AudioSystem system;
    REQUIRE(system.Init(SysConfig(&world), 0) == DeviceStatus::Ok);
    VoiceDesc desc;
    desc.clip = SysClip(480000);
    desc.loop = true;
    VoiceId id;
    REQUIRE(system.GetMixer().Play(desc, &id) == MixerStatus::Ok);

    AudioSettings settings;
    ApplyCost cost = ApplyCost::None;
    settings.master_volume = 0.5f;
    REQUIRE(system.ApplySettings(settings, 0, &cost) == DeviceStatus::Ok);
    CHECK(cost == ApplyCost::Free);
    REQUIRE(system.ApplySettings(settings, 0, &cost) == DeviceStatus::Ok);
    CHECK(cost == ApplyCost::None);
    settings.buffer_frames = 512;
    REQUIRE(system.ApplySettings(settings, 0, &cost) == DeviceStatus::Ok);
    CHECK(cost == ApplyCost::Reopen);
    settings.sample_rate = 96000;
    REQUIRE(system.ApplySettings(settings, 0, &cost) == DeviceStatus::Ok);
    CHECK(cost == ApplyCost::Replan);
    settings.channels = 6;
    REQUIRE(system.ApplySettings(settings, 0, &cost) == DeviceStatus::Ok);
    CHECK(cost == ApplyCost::Replan);
    settings.channels = 1;
    settings.sample_rate = 22050;
    REQUIRE(system.ApplySettings(settings, 0, &cost) == DeviceStatus::Ok);
    settings.channels = 2;
    settings.sample_rate = 48000;
    REQUIRE(system.ApplySettings(settings, 0, &cost) == DeviceStatus::Ok);
    const_cast<AudioDevice*>(system.CurrentDevice())->Pump(4800);
    CHECK(system.GetMixer().IsPlaying(id));
    CHECK(system.State() == OutputState::Device);
}

TEST_CASE("audio system: the device count never grows over 100 resets", "[audio][system]")
{
    FakeWorld world;
    AudioSystem system;
    REQUIRE(system.Init(SysConfig(&world), 0) == DeviceStatus::Ok);
    AudioSettings settings;
    ApplyCost cost = ApplyCost::None;
    for (u32 i = 0; i < 100; ++i)
    {
        settings.sample_rate = (i % 2) != 0 ? 48000 : 44100;
        REQUIRE(system.ApplySettings(settings, 0, &cost) == DeviceStatus::Ok);
        CHECK(world.alive == 1);
    }
    CHECK(system.Switches() == 101);
    system.Shutdown();
    CHECK(world.alive == 0);
}

TEST_CASE("audio system: the master volume of the start-up settings applies", "[audio][system]")
{
    FakeWorld world;
    AudioSystemConfig config = SysConfig(&world);
    config.settings.master_volume = 0.5f;
    AudioSystem system;
    REQUIRE(system.Init(config, 0) == DeviceStatus::Ok);
    VoiceDesc desc;
    desc.clip = SysClip(48000);
    desc.loop = true;
    VoiceId id;
    REQUIRE(system.GetMixer().Play(desc, &id) == MixerStatus::Ok);
    Vector<f32> out;
    out.Resize(1024);
    system.GetMixer().Render(out.Data(), 512);
    CHECK(
        out[1000] ==
        Catch::Approx(0.25f * 0.5f * 0.70710678f).margin(1e-3));  // the clip level, half volume, the equal-power centre
}
