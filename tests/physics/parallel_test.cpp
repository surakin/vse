// The parallel step (Phase 25 Day 389): the state hash is the same to the bit for every thread count, with or without a
// scheduler, and the step scales.
#include <catch2/catch_test_macros.hpp>

#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "os/os.h"
#include "physics/world.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

namespace
{

struct ParRng
{
    u64 state = 0x2545f4914f6cdd1dull;
    f32 Next()  // 0 to 1
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<f32>((state >> 40) & 0xffffff) / 16777216.0f;
    }
};

void ParStatic(PhysicsWorld& world, const Vec3& position, const Vec3& half)
{
    BodyDesc desc;
    desc.settings.type = BodyType::Static;
    desc.position = position;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeBox(half);
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
}

BodyId ParBody(PhysicsWorld& world, const Shape& shape, const Vec3& position)
{
    BodyDesc desc;
    desc.position = position;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = shape;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

void ParContainer(PhysicsWorld& world, f32 half)
{
    ParStatic(world, Vec3(0, -0.5f, 0), Vec3(half + 1, 0.5f, half + 1));
    ParStatic(world, Vec3(half + 0.5f, 5, 0), Vec3(0.5f, 5, half));
    ParStatic(world, Vec3(-half - 0.5f, 5, 0), Vec3(0.5f, 5, half));
    ParStatic(world, Vec3(0, 5, half + 0.5f), Vec3(half, 5, 0.5f));
    ParStatic(world, Vec3(0, 5, -half - 0.5f), Vec3(half, 5, 0.5f));
}

void ParStackField(PhysicsWorld& world)
{
    ParContainer(world, 12);
    for (u32 sx = 0; sx < 4; ++sx)
    {
        for (u32 sz = 0; sz < 4; ++sz)
        {
            for (u32 i = 0; i < 4; ++i)
            {
                ParBody(world, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)),
                        Vec3(-6.0f + 4.0f * static_cast<f32>(sx), 0.5f + static_cast<f32>(i),
                             -6.0f + 4.0f * static_cast<f32>(sz)));
            }
        }
    }
}

void ParPile(PhysicsWorld& world, u32 count)
{
    ParContainer(world, 6);
    ParRng rng;
    for (u32 i = 0; i < count; ++i)
    {
        const Vec3 at(-5.0f + 10.0f * rng.Next(), 1.0f + 0.7f * static_cast<f32>(i % 40) + rng.Next(),
                      -5.0f + 10.0f * rng.Next());
        switch (i % 3)
        {
        case 0: ParBody(world, Shape::MakeSphere(0.3f + 0.2f * rng.Next()), at); break;
        case 1: ParBody(world, Shape::MakeBox(Vec3(0.3f, 0.2f + 0.2f * rng.Next(), 0.3f)), at); break;
        default: ParBody(world, Shape::MakeCapsule(0.3f, 0.2f), at); break;
        }
    }
}

void ParHingeChains(PhysicsWorld& world)
{
    for (u32 c = 0; c < 4; ++c)
    {
        const f32 x = 3.0f * static_cast<f32>(c);
        BodyId previous;
        for (u32 i = 0; i < 12; ++i)
        {
            const Vec3 at(x + 0.5f + static_cast<f32>(i), 10.0f, 0);
            const BodyId link = ParBody(world, Shape::MakeBox(Vec3(0.5f, 0.1f, 0.1f)), at);
            world.CreateJoint(
                world.MakeHingeJoint(previous, link, Vec3(x + static_cast<f32>(i), 10.0f, 0), Vec3(0, 0, 1)));
            previous = link;
        }
    }
}

enum class Scene
{
    Stacks,
    Pile,
    Hinges
};

u64 ParRun(Scene scene, usize participants, u32 steps, u32 pile = 300)
{
    PhysicsWorld world;
    StepConfig config = world.GetStepConfig();
    config.threads = participants > 1 ? static_cast<u32>(participants) : 1;
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
    switch (scene)
    {
    case Scene::Stacks: ParStackField(world); break;
    case Scene::Pile: ParPile(world, pile); break;
    case Scene::Hinges: ParHingeChains(world); break;
    }
    jobs::Scheduler::Config sc;
    sc.participants = participants > 0 ? participants : 1;
    sc.pin = false;
    jobs::Scheduler scheduler(sc);
    if (participants > 0)
    {
        world.SetScheduler(&scheduler);
    }
    for (u32 i = 0; i < steps; ++i)
    {
        world.Step();
    }
    bool valid = false;
    const u64 hash = world.StateHash(&valid);
    REQUIRE(valid);
    return hash;
}

}  // namespace

TEST_CASE("Parallel: the state hash is the same for every thread count", "[physics][parallel]")
{
    const Scene scenes[] = {Scene::Stacks, Scene::Pile, Scene::Hinges};
    for (const Scene scene : scenes)
    {
        const u64 inline_hash = ParRun(scene, 0, 120);
        CHECK(inline_hash != 0);
        const usize counts[] = {1, 2, 4, 8, 32};
        for (const usize n : counts)
        {
            INFO("scene " << static_cast<int>(scene) << " participants " << n);
            CHECK(ParRun(scene, n, 120) == inline_hash);
        }
    }
}

TEST_CASE("Parallel: the scaling of a pile is recorded", "[physics][parallel][bench]")
{
    const usize counts[] = {0, 2, 4};
    for (const usize n : counts)
    {
        const u64 start = os::MonotonicTimeNanos();
        ParRun(Scene::Pile, n, 20, 1500);
        const f64 ms = static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6;
        WARN("1500 bodies, 20 steps, " << (n == 0 ? 1 : n) << " participants: " << ms << " ms");
    }
}
