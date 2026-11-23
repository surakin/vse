// Doppler and air absorption (Phase 26 Day 430): the pitch ratio of the formula, teleports that are not speeds, the
// cutoff curve, the one-pole low-pass in the mixer, and a flyby whose render hash is committed.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/block_hash.h"
#include "audio/components.h"
#include "audio/spatial_audio.h"
#include "containers/vector.h"
#include "ecs/transform.h"
#include "lang/scalar.h"
#include "platform_hash.h"

using namespace vse;
using namespace vse::audio;
using Catch::Approx;

namespace
{

ecs::WorldTransform DpAt(f32 x, f32 y, f32 z)
{
    return ecs::WorldTransform::FromMatrix(Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(x, y, z)));
}

AudioClipConstPtr DpClip(u32 frames, bool tone)
{
    Vector<f32> samples;
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(tone ? 0.4f * Sin(0.05f * static_cast<f32>(i)) : 0.5f);
    }
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "dp", &clip) ==
            ClipStatus::Ok);
    return clip;
}

struct DpRig
{
    Mixer mixer;
    SpatialAudio audio;
    ecs::World world;
    DpRig()
    {
        MixerConfig mc;
        mc.layout = ChannelLayout::Stereo;
        mc.quality = ResamplerQuality::Linear;
        mc.max_voices = 8;
        mc.lead_blocks = 0;
        REQUIRE(mixer.Init(mc) == MixerStatus::Ok);
        audio.Init(&mixer, SpatialAudioConfig());
    }
};

audio::AudioSource DpSource(const AudioClipConstPtr& clip)
{
    audio::AudioSource s;
    s.clip = clip;
    s.loop = true;
    s.attenuation.max_distance = 1000.0f;
    return s;
}

}  // namespace

TEST_CASE("doppler: the ratio of the formula", "[audio][doppler]")
{
    const Vec3 u(1, 0, 0);  // listener to emitter
    const Vec3 zero(0, 0, 0);
    // An emitter coming toward the listener at a tenth of the speed of sound: c / (c - v).
    CHECK(DopplerRatio(343.0f, 1.0f, u, zero, Vec3(-34.3f, 0, 0)) == Approx(343.0f / (343.0f - 34.3f)));
    // Going away: lower.
    CHECK(DopplerRatio(343.0f, 1.0f, u, zero, Vec3(34.3f, 0, 0)) == Approx(343.0f / (343.0f + 34.3f)));
    // A listener moving toward the emitter: (c + v) / c.
    CHECK(DopplerRatio(343.0f, 1.0f, u, Vec3(34.3f, 0, 0), zero) == Approx((343.0f + 34.3f) / 343.0f));
    // Across the line: no shift. Factor 0: none. Twice the factor shifts twice the speed.
    CHECK(DopplerRatio(343.0f, 1.0f, u, zero, Vec3(0, 50.0f, 0)) == Approx(1.0f));
    CHECK(DopplerRatio(343.0f, 0.0f, u, zero, Vec3(-34.3f, 0, 0)) == 1.0f);
    CHECK(DopplerRatio(343.0f, 2.0f, u, zero, Vec3(-17.15f, 0, 0)) == Approx(343.0f / (343.0f - 34.3f)));
    // Supersonic is limited, and the ratio stays in range.
    const f32 fast = DopplerRatio(343.0f, 1.0f, u, zero, Vec3(-5000.0f, 0, 0));
    CHECK(fast > 1.0f);
    CHECK(fast <= 4.0f);
    CHECK(DopplerRatio(0.0f, 1.0f, u, zero, Vec3(-34.3f, 0, 0)) == 1.0f);
}

TEST_CASE("doppler: an approaching emitter at a known speed has the formula's pitch", "[audio][doppler]")
{
    DpRig rig;
    rig.world.Create(AudioListener(), DpAt(0, 0, 0));
    const ecs::Entity e = rig.world.Create(DpSource(DpClip(48000, true)), DpAt(100.0f, 0, 0));
    rig.audio.Update(rig.world, 0.1f);
    CHECK(rig.audio.PitchOf(e) == Approx(1.0f));
    rig.world.Set(e, DpAt(96.57f, 0, 0));  // 34.3 units/s toward the listener at dt = 0.1
    rig.audio.Update(rig.world, 0.1f);
    CHECK(rig.audio.PitchOf(e) == Approx(343.0f / (343.0f - 34.3f)).epsilon(1e-3));
    // A stationary emitter has no shift on the next update.
    rig.audio.Update(rig.world, 0.1f);
    CHECK(rig.audio.PitchOf(e) == Approx(1.0f));
}

TEST_CASE("doppler: an explicit velocity wins over the transform's change", "[audio][doppler]")
{
    DpRig rig;
    rig.world.Create(AudioListener(), DpAt(0, 0, 0));
    audio::AudioVelocity v;
    v.velocity = Vec3(-34.3f, 0, 0);
    const ecs::Entity e = rig.world.Create(DpSource(DpClip(48000, true)), DpAt(100.0f, 0, 0), v);
    rig.audio.Update(rig.world, 0.1f);
    CHECK(rig.audio.PitchOf(e) == Approx(343.0f / (343.0f - 34.3f)).epsilon(1e-3));
}

TEST_CASE("doppler: a teleport is not a speed", "[audio][doppler]")
{
    DpRig rig;
    rig.world.Create(AudioListener(), DpAt(0, 0, 0));
    const ecs::Entity e = rig.world.Create(DpSource(DpClip(48000, true)), DpAt(100.0f, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    rig.world.Set(e, DpAt(400.0f, 0, 0));  // 300 units in one update
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.PitchOf(e) == Approx(1.0f));
}

TEST_CASE("doppler: a flyby crosses the unshifted pitch at closest approach", "[audio][doppler]")
{
    DpRig rig;
    rig.world.Create(AudioListener(), DpAt(0, 0, 0));
    const ecs::Entity e = rig.world.Create(DpSource(DpClip(48000, true)), DpAt(-100.0f, 0, 10.0f));
    f32 before = 0.0f;
    f32 after = 0.0f;
    f32 previous = 2.0f;
    bool crossed = false;
    for (u32 step = 0; step < 41; ++step)
    {
        rig.world.Set(e, DpAt(-100.0f + 5.0f * static_cast<f32>(step), 0, 10.0f));  // 50 units/s at dt 0.1
        rig.audio.Update(rig.world, 0.1f);
        const f32 pitch = rig.audio.PitchOf(e);
        if (step == 2)
        {
            before = pitch;
        }
        if (step == 38)
        {
            after = pitch;
        }
        if (step > 1 && previous > 1.0f && pitch <= 1.0f)
        {
            crossed = true;
            CHECK(step >= 19);  // x = -5 .. 0: closest approach is at step 20
            CHECK(step <= 22);
        }
        if (step > 0)
        {
            previous = pitch;
        }
    }
    CHECK(before > 1.1f);
    CHECK(after < 0.9f);
    CHECK(crossed);
}

TEST_CASE("air absorption: the cutoff follows the table and the coefficient the one-pole formula", "[audio][doppler]")
{
    CHECK(AirCutoffHz(0.0f, 1.0f) == Approx(20000.0f));
    CHECK(AirCutoffHz(10.0f, 1.0f) == Approx(16000.0f));
    CHECK(AirCutoffHz(37.5f, 1.0f) == Approx(8000.0f));
    CHECK(AirCutoffHz(400.0f, 1.0f) == Approx(1200.0f));
    CHECK(AirCutoffHz(5000.0f, 1.0f) == Approx(1200.0f));
    CHECK(AirCutoffHz(37.5f, 0.0f) == Approx(20000.0f));
    CHECK(AirCutoffHz(18.75f, 2.0f) == Approx(8000.0f));
    CHECK(AirCoefficient(22000.0f, 48000) == 1.0f);
    CHECK(AirCoefficient(1000.0f, 48000) == Approx(static_cast<f32>(1.0 - Exp(-6.283185307179586 * 1000.0 / 48000.0))));
    f32 previous = 1e9f;
    for (f32 d = 0.0f; d <= 400.0f; d += 10.0f)
    {
        const f32 cutoff = AirCutoffHz(d, 1.0f);
        CHECK(cutoff <= previous);
        previous = cutoff;
    }
}

TEST_CASE("air absorption: the voice's low-pass rounds a step by the coefficient", "[audio][doppler]")
{
    Mixer mixer;
    MixerConfig mc;
    mc.layout = ChannelLayout::Mono;
    mc.quality = ResamplerQuality::Linear;
    mc.max_voices = 2;
    mc.ramp_frames = 0;
    mc.lead_blocks = 0;
    REQUIRE(mixer.Init(mc) == MixerStatus::Ok);
    VoiceDesc desc;
    desc.clip = DpClip(48000, false);
    desc.air_coefficient = 0.1f;
    VoiceId id;
    REQUIRE(mixer.Play(desc, &id) == MixerStatus::Ok);
    Vector<f32> out;
    out.Resize(256);
    mixer.Render(out.Data(), 256);
    CHECK(out[0] == Approx(0.05f));
    CHECK(out[1] == Approx(0.05f + 0.1f * (0.5f - 0.05f)));
    CHECK(out[255] == Approx(0.5f).margin(1e-3));
    REQUIRE(mixer.SetAirAbsorption(id, 1.0f) == MixerStatus::Ok);
    CHECK(mixer.SetAirAbsorption(id, 0.0f) == MixerStatus::InvalidParam);
    CHECK(mixer.SetAirAbsorption(id, 1.5f) == MixerStatus::InvalidParam);
}

TEST_CASE("doppler: the flyby renders the committed hash (a same-build regression check)", "[audio][doppler][hash]")
{
    DpRig rig;
    rig.world.Create(AudioListener(), DpAt(0, 0, 0));
    const ecs::Entity e = rig.world.Create(DpSource(DpClip(48000, true)), DpAt(-60.0f, 0, 8.0f));
    RenderHash hash;
    Vector<f32> block;
    block.Resize(kBlockFrames * 2);
    const f32 dt = static_cast<f32>(kBlockFrames) / 48000.0f;
    for (u32 b = 0; b < 120; ++b)
    {
        rig.world.Set(e, DpAt(-60.0f + 1.0f * static_cast<f32>(b), 0, 8.0f));
        rig.audio.Update(rig.world, dt);
        rig.mixer.RenderBlock(Span<f32>(block.Data(), block.Size()));
        hash.AddBlock(Span<const f32>(block.Data(), block.Size()));
    }
    REQUIRE(hash.Valid());
    // Committed on Linux with Clang (-ffp-contract=off); a compiler change that moves the bits re-commits it.
    CHECK(hash.Value() == testing::PlatformHash(0xac45a6b75dd064beull, 0xfc3f7b88c68898b8ull, 0x27ca90661ecfd4f8ull));
}
