// Stress and robustness (Phase 25 Day 400): many random bodies in a box, a huge mass ratio, a tall stack, degenerate
// shapes, bodies made inside each other, a body sent a million metres away, and a long run of one spinning body. Each
// scene must finish without NaN, explosion or abort, with every body inside the world's bounds.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "lang/scalar.h"
#include "physics/world.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

namespace stress
{

struct StressRng
{
    u64 state = 0x9e3779b97f4a7c15ull;
    f32 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<f32>((state >> 40) & 0xffffff) / 16777216.0f;
    }
};

BodyId StressAdd(PhysicsWorld& world, BodyType type, const Shape& shape, const Vec3& at, f32 mass = 0.0f)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.settings.mass = mass;
    desc.position = at;
    const BodyId id = world.CreateBody(desc);
    REQUIRE(id.IsValid());
    Collider c;
    c.shape = shape;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

void StressFloor(PhysicsWorld& world, f32 half = 50.0f)
{
    StressAdd(world, BodyType::Static, Shape::MakeBox(Vec3(half, 0.5f, half)), Vec3(0, -0.5f, 0));
}

void StressWalls(PhysicsWorld& world, f32 half)
{
    StressAdd(world, BodyType::Static, Shape::MakeBox(Vec3(0.5f, 20, half)), Vec3(half + 0.5f, 10, 0));
    StressAdd(world, BodyType::Static, Shape::MakeBox(Vec3(0.5f, 20, half)), Vec3(-half - 0.5f, 10, 0));
    StressAdd(world, BodyType::Static, Shape::MakeBox(Vec3(half, 20, 0.5f)), Vec3(0, 10, half + 0.5f));
    StressAdd(world, BodyType::Static, Shape::MakeBox(Vec3(half, 20, 0.5f)), Vec3(0, 10, -half - 0.5f));
}

// Whether every body is finite and within `bound` of the origin, and the state hashes.
bool StressSane(const PhysicsWorld& world, const Vector<BodyId>& bodies, f32 bound)
{
    for (usize i = 0; i < bodies.Size(); ++i)
    {
        const Body* b = world.Find(bodies[i]);
        if (b == nullptr || !IsFinite(b->position.x) || !IsFinite(b->position.y) || !IsFinite(b->position.z) ||
            !IsFinite(b->linear_velocity.x) || !IsFinite(b->linear_velocity.y) || !IsFinite(b->linear_velocity.z) ||
            Abs(b->position.x) > bound || Abs(b->position.y) > bound || Abs(b->position.z) > bound)
        {
            return false;
        }
        const Quaternion& q = b->orientation;
        if (Abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.0f) > 1e-3f)
        {
            return false;
        }
    }
    bool valid = false;
    world.StateHash(&valid);
    return valid;
}

}  // namespace stress

using namespace stress;

TEST_CASE("Stress: random bodies dropped in a box do not explode", "[physics][stress]")
{
    PhysicsWorld world;
    StressFloor(world);
    StressWalls(world, 12.0f);
    StressRng rng;
    Vector<BodyId> bodies;
    for (u32 i = 0; i < 400; ++i)
    {
        const f32 rx = rng.Next();
        const f32 ry = rng.Next();
        const f32 rz = rng.Next();
        const Vec3 at(-11.0f + 22.0f * rx, 1.0f + 25.0f * ry, -11.0f + 22.0f * rz);
        switch (i % 3)
        {
        case 0:
            bodies.Push(StressAdd(world, BodyType::Dynamic, Shape::MakeSphere(0.3f + 0.2f * rng.Next()), at));
            break;
        case 1: bodies.Push(StressAdd(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.3f, 0.25f, 0.4f)), at)); break;
        default: bodies.Push(StressAdd(world, BodyType::Dynamic, Shape::MakeCapsule(0.3f, 0.2f), at)); break;
        }
    }
    f32 start_energy = 0.0f;
    for (usize i = 0; i < bodies.Size(); ++i)
    {
        const Body* b = world.Find(bodies[i]);
        start_energy += b->mass.mass * 9.81f * b->position.y;
    }
    for (u32 step = 0; step < 250; ++step)
    {
        world.Step();
        if (step % 50 == 0)
        {
            REQUIRE(StressSane(world, bodies, 60.0f));
        }
    }
    REQUIRE(StressSane(world, bodies, 60.0f));
    // The energy never grows beyond what the bodies started with (the solver may only lose it).
    f32 end_energy = 0.0f;
    for (usize i = 0; i < bodies.Size(); ++i)
    {
        const Body* b = world.Find(bodies[i]);
        const Vec3& v = b->linear_velocity;
        end_energy += b->mass.mass * (0.5f * (v.x * v.x + v.y * v.y + v.z * v.z) + 9.81f * b->position.y);
    }
    REQUIRE(end_energy < start_energy * 1.02f);
}

TEST_CASE("Stress: a mass ratio of ten thousand", "[physics][stress]")
{
    PhysicsWorld world;
    StressFloor(world);
    Vector<BodyId> bodies;
    for (u32 i = 0; i < 6; ++i)
    {
        bodies.Push(StressAdd(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)),
                              Vec3(0, 0.5f + static_cast<f32>(i), 0), 1.0f));
    }
    bodies.Push(
        StressAdd(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), Vec3(0, 7.0f, 0), 10000.0f));
    for (u32 step = 0; step < 600; ++step)
    {
        world.Step();
    }
    // At a ratio this large the light boxes are squeezed out from under the heavy one and thrown aside (a limit of the
    // solver, in the guide's "Limits"): nothing becomes NaN, nothing leaves the neighbourhood, and the heavy box stays
    // above the floor.
    REQUIRE(StressSane(world, bodies, 100.0f));
    REQUIRE(world.Find(bodies[6])->position.y > 0.3f);
}

TEST_CASE("Stress: stacks thirty and fifty high", "[physics][stress]")
{
    PhysicsWorld world;
    StressFloor(world);
    Vector<BodyId> bodies;
    for (u32 i = 0; i < 50; ++i)
    {
        bodies.Push(StressAdd(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)),
                              Vec3(0, 0.5f + static_cast<f32>(i), 0)));
    }
    for (u32 step = 0; step < 600; ++step)
    {
        world.Step();
    }
    // Fifty boxes high the stack topples (a limit, in the guide's "Limits"): the pile it makes is sane. The first
    // thirty keep their places in a stack of thirty (the next case).
    REQUIRE(StressSane(world, bodies, 60.0f));
}

TEST_CASE("Stress: a stack thirty high stands", "[physics][stress]")
{
    PhysicsWorld world;
    StressFloor(world);
    Vector<BodyId> bodies;
    for (u32 i = 0; i < 30; ++i)
    {
        bodies.Push(StressAdd(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)),
                              Vec3(0, 0.5f + static_cast<f32>(i), 0)));
    }
    for (u32 step = 0; step < 600; ++step)
    {
        world.Step();
    }
    REQUIRE(StressSane(world, bodies, 60.0f));
    REQUIRE(world.Find(bodies[29])->position.y > 27.0f);
}

TEST_CASE("Stress: degenerate shapes are refused or survive", "[physics][stress]")
{
    PhysicsWorld world;
    StressFloor(world);
    Vector<BodyId> bodies;
    const Shape shapes[] = {Shape::MakeSphere(1.0e-4f), Shape::MakeBox(Vec3(1.0e-3f, 1.0e-3f, 1.0e-3f)),
                            Shape::MakeBox(Vec3(2000.0f, 0.001f, 2000.0f)), Shape::MakeCapsule(0.0f, 0.01f),
                            Shape::MakeSphere(1000.0f)};
    for (u32 i = 0; i < 5; ++i)
    {
        BodyDesc desc;
        desc.position = Vec3(static_cast<f32>(i) * 5.0f, 40.0f, 0);
        const BodyId id = world.CreateBody(desc);
        Collider c;
        c.shape = shapes[i];
        if (world.AddCollider(id, c) == BodyStatus::Ok)
        {
            bodies.Push(id);
        }
        else
        {
            world.DestroyBody(id);
        }
    }
    for (u32 step = 0; step < 240; ++step)
    {
        world.Step();
    }
    REQUIRE(StressSane(world, bodies, 1.0e5f));
}

TEST_CASE("Stress: two hundred bodies made on the same spot", "[physics][stress]")
{
    PhysicsWorld world;
    StressFloor(world);
    Vector<BodyId> bodies;
    for (u32 i = 0; i < 200; ++i)
    {
        bodies.Push(StressAdd(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), Vec3(0, 3.0f, 0)));
    }
    for (u32 step = 0; step < 300; ++step)
    {
        world.Step();
    }
    REQUIRE(StressSane(world, bodies, 200.0f));
}

TEST_CASE("Stress: a body sent a million metres away and back", "[physics][stress]")
{
    PhysicsWorld world;
    StressFloor(world);
    const BodyId ball = StressAdd(world, BodyType::Dynamic, Shape::MakeSphere(0.5f), Vec3(0, 3, 0));
    Vector<BodyId> bodies;
    bodies.Push(ball);
    for (u32 step = 0; step < 30; ++step)
    {
        world.Step();
    }
    REQUIRE(world.SetTransform(ball, Vec3(1.0e6f, 1.0e6f, -1.0e6f), Quaternion::Identity()) == BodyStatus::Ok);
    for (u32 step = 0; step < 60; ++step)
    {
        world.Step();
    }
    REQUIRE(StressSane(world, bodies, 1.0e7f));
    // Past the world's bounds is refused, not simulated.
    REQUIRE(world.SetTransform(ball, Vec3(1.0e9f, 0, 0), Quaternion::Identity()) != BodyStatus::Ok);
    REQUIRE(world.SetTransform(ball, Vec3(0, 3, 0), Quaternion::Identity()) == BodyStatus::Ok);
    for (u32 step = 0; step < 120; ++step)
    {
        world.Step();
    }
    REQUIRE(StressSane(world, bodies, 100.0f));
    REQUIRE(world.Find(ball)->position.y < 1.0f);
}

TEST_CASE("Stress: a spinning body keeps a unit quaternion over a long run", "[physics][stress]")
{
    PhysicsWorld world;
    StepConfig config = world.GetStepConfig();
    config.gravity = Vec3(0, 0, 0);
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
    const BodyId id = StressAdd(world, BodyType::Dynamic, Shape::MakeBox(Vec3(0.5f, 0.3f, 0.2f)), Vec3(0, 0, 0));
    Velocity v;
    v.angular = Vec3(3.0f, 1.0f, 2.0f);
    REQUIRE(world.SetVelocity(id, v) == BodyStatus::Ok);
    Vector<BodyId> bodies;
    bodies.Push(id);
    for (u32 step = 0; step < 100000; ++step)
    {
        world.Step();
    }
    REQUIRE(StressSane(world, bodies, 10.0f));
}
