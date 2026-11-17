// Occlusion (Phase 26 Day 431): the smoothing, the rate limit, the round-robin and the budget, against a test double
// that needs no physics.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/components.h"
#include "audio/occlusion.h"
#include "audio/spatial_audio.h"
#include "containers/vector.h"
#include "ecs/transform.h"
#include "lang/scalar.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio;
using Catch::Approx;

namespace
{

// Blocked behind the plane x = 5: a path that starts beyond it is fully occluded; counts the calls.
class WallDouble final : public OcclusionQuery
{
public:
    f32 Occlusion(const Vec3& from, const Vec3&, u32* crossed) override
    {
        calls += 1;
        positions.Push(from.x);
        if (crossed != nullptr)
        {
            *crossed = from.x > wall_x ? 1u : 0u;
        }
        return from.x > wall_x ? level : 0.0f;
    }
    f32 wall_x = 5.0f;
    f32 level = 1.0f;
    u32 calls = 0;
    Vector<f32> positions;
};

ecs::WorldTransform OcAt(f32 x, f32 y, f32 z)
{
    ecs::WorldTransform t;
    t.matrix = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(x, y, z));
    return t;
}

AudioClipConstPtr OcClip()
{
    Vector<f32> samples;
    for (u32 i = 0; i < 48000; ++i)
    {
        samples.Push(0.5f);
    }
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = 48000;
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "oc", &clip) ==
            ClipStatus::Ok);
    return clip;
}

audio::AudioSource OcSource(const AudioClipConstPtr& clip)
{
    audio::AudioSource s;
    s.clip = clip;
    s.loop = true;
    s.attenuation.min_distance = 100.0f;  // no distance loss: the level is the occlusion's
    s.attenuation.max_distance = 1000.0f;
    return s;
}

struct OcRig
{
    Mixer mixer;
    SpatialAudio audio;
    ecs::World world;
    WallDouble wall;
    explicit OcRig(const SpatialAudioConfig& config = SpatialAudioConfig())
    {
        MixerConfig mc;
        mc.layout = ChannelLayout::Mono;
        mc.quality = ResamplerQuality::Linear;
        mc.max_voices = 64;
        mc.lead_blocks = 0;
        REQUIRE(mixer.Init(mc) == MixerStatus::Ok);
        audio.Init(&mixer, config);
        audio.SetOcclusion(&wall);
        world.Create(AudioListener(), OcAt(0, 0, 0));
    }
};

}  // namespace

TEST_CASE("occlusion: open, behind a wall, and not tested when the source opts out", "[audio][occlusion]")
{
    OcRig rig;
    const ecs::Entity open = rig.world.Create(OcSource(OcClip()), OcAt(3.0f, 0, 0));
    const ecs::Entity blocked = rig.world.Create(OcSource(OcClip()), OcAt(8.0f, 0, 0));
    audio::AudioSource off = OcSource(OcClip());
    off.occlusion = false;
    const ecs::Entity ignored = rig.world.Create(off, OcAt(9.0f, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.OcclusionOf(open) == 0.0f);
    CHECK(rig.audio.OcclusionOf(blocked) == Approx(1.0f));
    CHECK(rig.audio.OcclusionOf(ignored) == 0.0f);
    CHECK(rig.audio.Report().occlusion_tests == 2);
}

TEST_CASE("occlusion: a blocked emitter is quieter and duller by the documented amounts", "[audio][occlusion]")
{
    OcRig rig;
    rig.world.Create(OcSource(OcClip()), OcAt(8.0f, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    for (u32 i = 0; i < 8; ++i)
    {
        rig.audio.Update(rig.world, 0.016f);
    }
    Vector<f32> out;
    out.Resize(256);
    for (u32 i = 0; i < 6; ++i)
    {
        rig.mixer.Render(out.Data(), 256);
    }
    // gain 0.5 clip level x 0.35 at full occlusion, the low-pass settled on a constant input.
    CHECK(out[255] == Approx(0.5f * 0.35f).margin(2e-3));
}

TEST_CASE("occlusion: a door that opens fades over the smoothing time", "[audio][occlusion]")
{
    SpatialAudioConfig config;
    config.occlusion.smooth_seconds = 0.2f;
    config.occlusion.tests_per_second = 100.0f;
    OcRig rig(config);
    const ecs::Entity e = rig.world.Create(OcSource(OcClip()), OcAt(8.0f, 0, 0));
    rig.audio.Update(rig.world, 0.01f);
    CHECK(rig.audio.OcclusionOf(e) == Approx(1.0f));
    rig.wall.wall_x = 50.0f;  // the door opens
    f32 previous = 1.0f;
    f32 after_one_tau = 0.0f;
    for (u32 i = 1; i <= 100; ++i)
    {
        rig.audio.Update(rig.world, 0.01f);
        const f32 now = rig.audio.OcclusionOf(e);
        CHECK(now <= previous);
        previous = now;
        if (i == 20)
        {
            after_one_tau = now;
        }
    }
    CHECK(after_one_tau == Approx(0.3679f).margin(0.03f));
    CHECK(previous < 0.01f);
}

TEST_CASE("occlusion: an emitter is tested at most tests_per_second", "[audio][occlusion]")
{
    SpatialAudioConfig config;
    config.occlusion.tests_per_second = 2.0f;
    OcRig rig(config);
    rig.world.Create(OcSource(OcClip()), OcAt(8.0f, 0, 0));
    for (u32 i = 0; i < 100; ++i)
    {
        rig.audio.Update(rig.world, 0.01f);  // one second
    }
    CHECK(rig.wall.calls >= 2);
    CHECK(rig.wall.calls <= 3);
}

TEST_CASE("occlusion: the budget is respected and the round-robin visits every emitter", "[audio][occlusion]")
{
    SpatialAudioConfig config;
    config.occlusion.max_tests_per_update = 5;
    config.occlusion.tests_per_second = 1000.0f;
    OcRig rig(config);
    for (u32 i = 0; i < 20; ++i)
    {
        rig.world.Create(OcSource(OcClip()), OcAt(1.0f + 0.1f * static_cast<f32>(i), 0, 0));
    }
    // 20 emitters at 5 tests per update: every one is visited within 4 updates.
    for (u32 u = 0; u < 4; ++u)
    {
        rig.audio.Update(rig.world, 0.01f);
        CHECK(rig.audio.Report().occlusion_tests == 5);
    }
    CHECK(rig.wall.calls == 20);
    for (u32 i = 0; i < 20; ++i)
    {
        u32 seen = 0;
        for (u32 k = 0; k < rig.wall.positions.Size(); ++k)
        {
            if (rig.wall.positions[k] == Approx(1.0f + 0.1f * static_cast<f32>(i)))
            {
                seen += 1;
            }
        }
        CHECK(seen == 1);
    }
}
