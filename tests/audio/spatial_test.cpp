// Sound in the world (Phase 26 Day 428): the distance models, the cone, and the system that places and ranks emitters.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/components.h"
#include "audio/spatial_audio.h"
#include "containers/vector.h"
#include "ecs/transform.h"
#include "lang/scalar.h"

using namespace vse;
using namespace vse::audio;
using Catch::Approx;

namespace
{

AudioClipConstPtr SpClip(u32 frames)
{
    Vector<f32> samples;
    for (u32 i = 0; i < frames; ++i)
    {
        samples.Push(0.5f);
    }
    ClipDesc desc;
    desc.format.sample_rate = 48000;
    desc.format.layout = ChannelLayout::Mono;
    desc.format.format = SampleFormat::F32;
    desc.frame_count = frames;
    AudioClipPtr clip;
    REQUIRE(AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "sp", &clip) ==
            ClipStatus::Ok);
    return clip;
}

ecs::WorldTransform At(f32 x, f32 y, f32 z)
{
    return ecs::WorldTransform::FromMatrix(Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(x, y, z)));
}

audio::AudioSource Source(const AudioClipConstPtr& clip)
{
    audio::AudioSource s;
    s.clip = clip;
    s.loop = true;
    return s;
}

struct Rig
{
    Mixer mixer;
    SpatialAudio audio;
    ecs::World world;
    Vector<f32> out;

    explicit Rig(u32 real_voices = 32, u32 pool = 64, u32 queue = 1024)
    {
        MixerConfig mc;
        mc.command_capacity = queue;
        mc.layout = ChannelLayout::Mono;
        mc.quality = ResamplerQuality::Linear;
        mc.max_voices = pool;
        mc.lead_blocks = 0;
        REQUIRE(mixer.Init(mc) == MixerStatus::Ok);
        SpatialAudioConfig config;
        config.max_real_voices = real_voices;
        audio.Init(&mixer, config);
        out.Resize(256);
    }
    void Block() { mixer.Render(out.Data(), 256); }
    ecs::Entity Listener(f32 x = 0, f32 y = 0, f32 z = 0) { return world.Create(AudioListener(), At(x, y, z)); }
};

}  // namespace

TEST_CASE("distance models: the documented gain at ten distances", "[audio][spatial]")
{
    Attenuation a;
    a.min_distance = 2.0f;
    a.max_distance = 50.0f;
    a.rolloff = 1.5f;
    const f32 distances[10] = {0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 8.0f, 12.0f, 20.0f, 35.0f, 50.0f};
    for (f32 d : distances)
    {
        const f32 c = d < 2.0f ? 2.0f : d;
        a.model = DistanceModel::Inverse;
        CHECK(DistanceGain(a, d) == Approx(2.0f / (2.0f + 1.5f * (c - 2.0f))));
        a.model = DistanceModel::Linear;
        CHECK(DistanceGain(a, d) ==
              Approx(1.0f - 1.5f * (c - 2.0f) / 48.0f > 0.0f ? 1.0f - 1.5f * (c - 2.0f) / 48.0f : 0.0f));
        a.model = DistanceModel::Exponential;
        CHECK(DistanceGain(a, d) == Approx(static_cast<f32>(Pow(static_cast<f64>(c / 2.0f), -1.5))));
    }
    a.model = DistanceModel::Inverse;
    CHECK(DistanceGain(a, 50.5f) == 0.0f);
    CHECK(DistanceGain(a, -1.0f) == 0.0f);
}

TEST_CASE("distance models: a custom curve interpolates and holds its ends", "[audio][spatial]")
{
    Attenuation a;
    a.model = DistanceModel::Curve;
    a.max_distance = 100.0f;
    a.curve.count = 3;
    a.curve.distance[0] = 10.0f;
    a.curve.distance[1] = 20.0f;
    a.curve.distance[2] = 40.0f;
    a.curve.gain[0] = 1.0f;
    a.curve.gain[1] = 0.5f;
    a.curve.gain[2] = 0.0f;
    CHECK(DistanceGain(a, 0.0f) == Approx(1.0f));
    CHECK(DistanceGain(a, 15.0f) == Approx(0.75f));
    CHECK(DistanceGain(a, 30.0f) == Approx(0.25f));
    CHECK(DistanceGain(a, 80.0f) == Approx(0.0f));
    a.curve.distance[1] = 5.0f;  // not rising
    CHECK_FALSE(IsValid(a));
    CHECK(DistanceGain(a, 15.0f) == 0.0f);
}

TEST_CASE("distance models: the audible radius is where the gain reaches the threshold", "[audio][spatial]")
{
    Attenuation a;
    a.min_distance = 1.0f;
    a.max_distance = 1000.0f;
    a.rolloff = 1.0f;
    for (DistanceModel m : {DistanceModel::Inverse, DistanceModel::Linear, DistanceModel::Exponential})
    {
        a.model = m;
        const f32 r = AudibleRadius(a, 0.05f);
        CHECK(r > 1.0f);
        CHECK(r <= 1000.0f);
        CHECK(DistanceGain(a, r) == Approx(0.05f).margin(0.002f));
        CHECK(DistanceGain(a, r * 1.01f) < 0.05f);
    }
}

TEST_CASE("cone: full inside the inner angle, outer gain outside, a blend between", "[audio][spatial]")
{
    Cone cone;
    cone.inner_angle = 1.0472f;  // 60 degrees
    cone.outer_angle = 2.0944f;  // 120 degrees
    cone.outer_gain = 0.2f;
    CHECK(ConeGain(cone, 1.0f) == 1.0f);
    CHECK(ConeGain(cone, Cos(0.5f)) == 1.0f);      // on the inner edge's inside
    CHECK(ConeGain(cone, -1.0f) == Approx(0.2f));  // behind
    const f32 mid = ConeGain(cone, 0.7f);
    CHECK(mid > 0.2f);
    CHECK(mid < 1.0f);
    CHECK(ConeGain(Cone(), -1.0f) == 1.0f);
}

TEST_CASE("spatial audio: an emitter gets a voice with the distance gain and a pan", "[audio][spatial]")
{
    Rig rig;
    rig.Listener();
    audio::AudioSource s = Source(SpClip(48000));
    s.attenuation.min_distance = 1.0f;
    s.attenuation.max_distance = 100.0f;
    const ecs::Entity e = rig.world.Create(s, At(4.0f, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.Report().audible == 1);
    CHECK(rig.audio.VoiceOf(e).IsValid());
    CHECK(rig.audio.IsAudible(e));
    rig.Block();
    CHECK(rig.mixer.Stats().voices_mixed == 1);
    // Out of range: it keeps its voice, silent and virtual.
    rig.world.Set(e, At(500.0f, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    CHECK_FALSE(rig.audio.IsAudible(e));
    rig.Block();
    rig.Block();
    CHECK(rig.mixer.Stats().voices_mixed == 0);
    CHECK(rig.mixer.Stats().voices_virtual == 1);
}

TEST_CASE("spatial audio: a listener and an emitter far from the origin hear each other as they do at the origin",
          "[audio][spatial][far]")
{
    // At 1e9 single-precision positions are 64 apart, so a 30 m distance would read as 0 or 64: the exact positions
    // keep it 30.
    const f64 far_x = 1.0e9;
    Rig rig;
    ecs::WorldTransform listener_at;
    listener_at.position = Vec3d(far_x, 3.0, 0.0);
    rig.world.Create(AudioListener(), listener_at);
    audio::AudioSource s = Source(SpClip(48000));
    s.attenuation.min_distance = 1.0f;
    s.attenuation.max_distance = 50.0f;
    ecs::WorldTransform near_at;
    near_at.position = Vec3d(far_x + 30.0, 3.0, 0.0);
    ecs::WorldTransform away_at;
    away_at.position = Vec3d(far_x + 70.0, 3.0, 0.0);
    const ecs::Entity near_e = rig.world.Create(s, near_at);
    const ecs::Entity away_e = rig.world.Create(s, away_at);
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.IsAudible(near_e));
    CHECK_FALSE(rig.audio.IsAudible(away_e));
}

TEST_CASE("spatial audio: no listener means no spatial sound, a flat source still plays", "[audio][spatial]")
{
    Rig rig;
    audio::AudioSource s = Source(SpClip(48000));
    rig.world.Create(s, At(1, 0, 0));
    audio::AudioSource flat = Source(SpClip(48000));
    flat.spatial = false;
    const ecs::Entity f = rig.world.Create(flat, At(1000, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.Report().audible == 1);
    CHECK(rig.audio.VoiceOf(f).IsValid());
}

TEST_CASE("spatial audio: moving one of 10,000 emitters costs one update", "[audio][spatial]")
{
    Rig rig(8, 64);
    rig.Listener();
    AudioClipConstPtr clip = SpClip(480);
    Vector<ecs::Entity> all;
    for (u32 i = 0; i < 10000; ++i)
    {
        audio::AudioSource s = Source(clip);
        s.attenuation.max_distance = 20.0f;
        s.play_on_start = false;
        all.Push(
            rig.world.Create(s, At(1000.0f + static_cast<f32>(i % 100) * 50.0f, 0, static_cast<f32>(i / 100) * 50.0f)));
    }
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.Report().emitters == 10000);
    CHECK(rig.audio.Report().moved == 10000);
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.Report().moved == 0);
    rig.world.Set(all[1234], At(1500.0f, 0, 20.0f));
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.Report().moved == 1);
    CHECK(rig.audio.Report().commands == 0);
}

TEST_CASE("spatial audio: more emitters than voices keep the loudest, the same on every run", "[audio][spatial]")
{
    for (u32 run = 0; run < 2; ++run)
    {
        Rig rig(4, 64);
        rig.Listener();
        AudioClipConstPtr clip = SpClip(48000);
        Vector<ecs::Entity> entities;
        for (u32 i = 0; i < 12; ++i)
        {
            audio::AudioSource s = Source(clip);
            s.attenuation.min_distance = 1.0f;
            // Two pairs tie on distance: priority then entity order break them.
            const f32 d = 2.0f + static_cast<f32>(i / 2) * 3.0f;
            s.priority = static_cast<i32>(i % 2);
            entities.Push(rig.world.Create(s, At(d, 0, 0)));
        }
        rig.audio.Update(rig.world, 0.016f);
        CHECK(rig.audio.Report().audible == 12);
        CHECK(rig.audio.Report().real == 4);
        rig.Block();
        CHECK(rig.mixer.Stats().voices_mixed == 4);
        CHECK(rig.mixer.Stats().voices_virtual == 8);
        // The two nearest pairs are the real ones, on every run.
        for (u32 i = 0; i < 12; ++i)
        {
            CHECK(rig.audio.IsReal(entities[i]) == (i < 4));
        }
    }
}

TEST_CASE("spatial audio: equal loudness is broken by priority, then by entity", "[audio][spatial]")
{
    Rig rig(1, 64);
    rig.Listener();
    AudioClipConstPtr clip = SpClip(48000);
    audio::AudioSource s = Source(clip);
    const ecs::Entity a = rig.world.Create(s, At(3, 0, 0));
    s.priority = 5;
    const ecs::Entity b = rig.world.Create(s, At(-3, 0, 0));
    const ecs::Entity c = rig.world.Create(s, At(0, 3, 0));
    rig.audio.Update(rig.world, 0.016f);
    CHECK_FALSE(rig.audio.IsReal(a));
    CHECK(rig.audio.IsReal(b));  // priority 5 and the lower entity id of the two
    CHECK_FALSE(rig.audio.IsReal(c));
}

TEST_CASE("spatial audio: a destroyed entity's voice fades out and frees", "[audio][spatial]")
{
    Rig rig;
    rig.Listener();
    audio::AudioSource s = Source(SpClip(48000));
    const ecs::Entity e = rig.world.Create(s, At(2, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    const VoiceId id = rig.audio.VoiceOf(e);
    REQUIRE(id.IsValid());
    rig.Block();
    CHECK(rig.mixer.IsPlaying(id));
    rig.world.Destroy(e);
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.Report().stopped == 1);
    CHECK(rig.audio.Report().emitters == 0);
    CHECK_FALSE(rig.audio.VoiceOf(e).IsValid());
    for (u32 i = 0; i < 4; ++i)
    {
        rig.Block();
    }
    CHECK(rig.mixer.Stats().voices_mixed == 0);
    CHECK_FALSE(rig.mixer.IsPlaying(id));
}

TEST_CASE("spatial audio: the listener's velocity comes from its movement", "[audio][spatial]")
{
    Rig rig;
    const ecs::Entity l = rig.Listener();
    rig.audio.Update(rig.world, 0.5f);
    rig.world.Set(l, At(3.0f, 0, 0));
    rig.audio.Update(rig.world, 0.5f);
    CHECK(rig.audio.Listener().velocity.x == Approx(6.0f));
    CHECK(rig.audio.Listener().position.x == Approx(3.0f));
}

TEST_CASE("spatial audio: a source that stops being spatial is one candidate, not two", "[audio][spatial]")
{
    Rig rig;
    rig.Listener();
    audio::AudioSource s = Source(SpClip(48000));
    const ecs::Entity e = rig.world.Create(s, At(3, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.Report().audible == 1);
    s.spatial = false;
    rig.world.Set(e, s);
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.Report().audible == 1);
    CHECK(rig.audio.Report().real == 1);
    s.spatial = true;
    rig.world.Set(e, s);
    rig.audio.Update(rig.world, 0.016f);
    CHECK(rig.audio.Report().audible == 1);
}

TEST_CASE("spatial audio: a command the mixer refused is sent again on the next update", "[audio][spatial]")
{
    Rig rig(32, 64, 8);
    rig.Listener();
    audio::AudioSource s = Source(SpClip(48000));
    s.attenuation.min_distance = 1.0f;
    s.attenuation.max_distance = 100.0f;
    const ecs::Entity e = rig.world.Create(s, At(2.0f, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    rig.Block();
    // Fill the queue so the next gain change is refused.
    const VoiceId id = rig.audio.VoiceOf(e);
    for (u32 i = 0; i < 16; ++i)
    {
        (void)rig.mixer.SetPitch(id, 1.0f);
    }
    rig.world.Set(e, At(10.0f, 0, 0));
    rig.audio.Update(rig.world, 0.016f);
    rig.Block();
    rig.Block();
    rig.audio.Update(rig.world, 0.016f);  // the refused gain goes out now
    rig.Block();
    rig.Block();
    rig.Block();
    // The clip is 0.5 constant: the level is the gain at distance 10 (inverse model: 1 / 10) at the centre of a mono
    // mix.
    CHECK(rig.out[200] == Approx(0.5f * 0.1f).margin(2e-3));
}
