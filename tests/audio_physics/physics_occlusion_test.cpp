// The physics occlusion query (Phase 26 Day 431): open paths, thin and thick walls, layers, and the sound's level
// through the spatial audio system over a real physics world.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/components.h"
#include "audio/spatial_audio.h"
#include "audio_physics/physics_occlusion.h"
#include "ecs/transform.h"
#include "lang/scalar.h"

using namespace engine_placeholder;
using namespace engine_placeholder::audio_physics;
using Catch::Approx;

namespace
{

// A wall: a box whose thickness along x is `thickness`, centred at x = `x`, on collision layer `layer`.
void Wall(physics::PhysicsWorld& world, f32 x, f32 thickness, u8 layer = 0)
{
    physics::BodyDesc desc;
    desc.settings.type = physics::BodyType::Static;
    desc.position = Vec3(x, 0, 0);
    const physics::BodyId id = world.CreateBody(desc);
    physics::Collider c;
    c.shape = physics::Shape::MakeBox(Vec3(thickness * 0.5f, 5.0f, 5.0f));
    c.layer = layer;
    REQUIRE(world.AddCollider(id, c) == physics::BodyStatus::Ok);
}

}  // namespace

TEST_CASE("physics occlusion: open, thin wall, thick wall", "[audio_physics]")
{
    physics::PhysicsWorld world;
    PhysicsOcclusion query(&world);
    u32 crossed = 99;
    CHECK(query.Occlusion(Vec3(10, 0, 0), Vec3(0, 0, 0), &crossed) == 0.0f);
    CHECK(crossed == 0);
    Wall(world, 5.0f, 0.1f);
    const f32 thin = query.Occlusion(Vec3(10, 0, 0), Vec3(0, 0, 0), &crossed);
    CHECK(crossed == 1);
    CHECK(thin == Approx(1.0f - static_cast<f32>(Exp(-0.3))).margin(1e-3));
    physics::PhysicsWorld thick_world;
    PhysicsOcclusion thick_query(&thick_world);
    Wall(thick_world, 5.0f, 1.0f);
    const f32 thick = thick_query.Occlusion(Vec3(10, 0, 0), Vec3(0, 0, 0), nullptr);
    CHECK(thick == Approx(1.0f - static_cast<f32>(Exp(-3.0))).margin(1e-3));
    CHECK(thick > thin);
    // Both directions agree, and a path that stops before the wall is open.
    CHECK(query.Occlusion(Vec3(0, 0, 0), Vec3(10, 0, 0), nullptr) == Approx(thin).margin(1e-4));
    CHECK(query.Occlusion(Vec3(10, 0, 0), Vec3(8, 0, 0), nullptr) == 0.0f);
    // Degenerate input is open.
    CHECK(query.Occlusion(Vec3(1, 1, 1), Vec3(1, 1, 1), nullptr) == 0.0f);
}

TEST_CASE("physics occlusion: two walls add their depth, and the layer mask and absorptions apply", "[audio_physics]")
{
    physics::PhysicsWorld world;
    Wall(world, 3.0f, 0.2f, 1);
    Wall(world, 7.0f, 0.2f, 2);
    PhysicsOcclusionConfig config;
    PhysicsOcclusion both(&world, config);
    u32 crossed = 0;
    CHECK(both.Occlusion(Vec3(10, 0, 0), Vec3(0, 0, 0), &crossed) ==
          Approx(1.0f - static_cast<f32>(Exp(-3.0 * 0.4))).margin(1e-3));
    CHECK(crossed == 2);
    config.layer_mask = 1u << 1;  // only the first wall blocks
    PhysicsOcclusion one(&world, config);
    CHECK(one.Occlusion(Vec3(10, 0, 0), Vec3(0, 0, 0), &crossed) ==
          Approx(1.0f - static_cast<f32>(Exp(-3.0 * 0.2))).margin(1e-3));
    CHECK(crossed == 1);
    config.layer_mask = 0xFFFFFFFFu;
    config.absorption_per_metre[2] = 0.0f;  // glass: the second wall absorbs nothing
    PhysicsOcclusion glass(&world, config);
    CHECK(glass.Occlusion(Vec3(10, 0, 0), Vec3(0, 0, 0), nullptr) ==
          Approx(1.0f - static_cast<f32>(Exp(-3.0 * 0.2))).margin(1e-3));
    PhysicsOcclusion none(nullptr);
    CHECK(none.Occlusion(Vec3(10, 0, 0), Vec3(0, 0, 0), nullptr) == 0.0f);
}

TEST_CASE("physics occlusion: behind a thick wall the voice is quieter than behind a thin one", "[audio_physics]")
{
    f32 levels[2];
    for (u32 variant = 0; variant < 2; ++variant)
    {
        physics::PhysicsWorld world;
        Wall(world, 5.0f, variant == 0 ? 0.1f : 1.0f);
        PhysicsOcclusion query(&world);
        audio::Mixer mixer;
        audio::MixerConfig mc;
        mc.layout = audio::ChannelLayout::Mono;
        mc.quality = audio::ResamplerQuality::Linear;
        mc.max_voices = 4;
        mc.lead_blocks = 0;
        REQUIRE(mixer.Init(mc) == audio::MixerStatus::Ok);
        audio::SpatialAudio sound;
        sound.Init(&mixer, audio::SpatialAudioConfig());
        sound.SetOcclusion(&query);
        ecs::World ecs_world;
        ecs::WorldTransform at_origin;
        ecs_world.Create(audio::AudioListener(), at_origin);
        Vector<f32> samples;
        for (u32 i = 0; i < 48000; ++i)
        {
            samples.Push(0.5f);
        }
        audio::ClipDesc desc;
        desc.format.sample_rate = 48000;
        desc.format.layout = audio::ChannelLayout::Mono;
        desc.format.format = audio::SampleFormat::F32;
        desc.frame_count = 48000;
        audio::AudioClipPtr clip;
        REQUIRE(audio::AudioClip::CreateCopy(desc, Span<const f32>(samples.Data(), samples.Size()), "wall", &clip) ==
                audio::ClipStatus::Ok);
        audio::AudioSource source;
        source.clip = clip;
        source.loop = true;
        source.attenuation.min_distance = 100.0f;
        source.attenuation.max_distance = 1000.0f;
        ecs::WorldTransform there;
        there.matrix = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(10, 0, 0));
        const ecs::Entity e = ecs_world.Create(source, there);
        for (u32 i = 0; i < 40; ++i)
        {
            sound.Update(ecs_world, 0.016f);
        }
        Vector<f32> out;
        out.Resize(256);
        for (u32 i = 0; i < 8; ++i)
        {
            mixer.Render(out.Data(), 256);
        }
        levels[variant] = out[255];
        CHECK(sound.OcclusionOf(e) > 0.2f);
    }
    CHECK(levels[1] < levels[0]);
    CHECK(levels[0] < 0.5f);
}
