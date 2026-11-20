// The fixed step, the accumulator, interpolation, the state hash, settings and the ECS system (Phase 25 Day 390).
#include <catch2/catch_test_macros.hpp>

#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "lang/scalar.h"
#include "math/math_util.h"
#include "physics/physics_settings.h"
#include "physics/sync.h"
#include "physics/system.h"
#include "physics/world.h"
#include "test_values.h"

using namespace vse;
using namespace vse::physics;

namespace
{

BodyId MakeBall(PhysicsWorld& world, const Vec3& position, const Vec3& velocity, BodyType type = BodyType::Dynamic)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.position = position;
    desc.velocity.linear = velocity;
    const BodyId id = world.CreateBody(desc);
    REQUIRE(id.IsValid());
    Collider c;
    c.shape = Shape::MakeSphere(0.5f);
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

f32 StepSeconds()
{
    return static_cast<f32>(1.0 / 60.0);
}

}  // namespace

TEST_CASE("Step: free fall matches the same recurrence bit for bit and the closed form closely", "[physics][step]")
{
    PhysicsWorld world;
    const BodyId id = MakeBall(world, Vec3(0, 100, 0), Vec3(0, 0, 0));
    const f32 dt = StepSeconds();
    f32 y = 100.0f;
    f32 v = 0.0f;
    for (int i = 1; i <= 120; ++i)
    {
        world.Step();
        v = v + (-9.81f) * dt;
        v = v * (1.0f / (1.0f + 0.0f * dt));
        y = y + v * dt;
        REQUIRE(world.Find(id)->position.y == y);
        REQUIRE(world.Find(id)->linear_velocity.y == v);
    }
    // Semi-implicit Euler: y_n = y_0 - g dt^2 n (n + 1) / 2.
    const f32 closed = 100.0f - 9.81f * dt * dt * (120.0f * 121.0f / 2.0f);
    CHECK(Abs(y - closed) < 2e-3f);
    // The continuous solution is within g dt t / 2 of it.
    CHECK(Abs(y - (100.0f - 0.5f * 9.81f * 4.0f)) < 0.5f * 9.81f * dt * 2.0f + 1e-2f);
    CHECK(world.GetStepStats().steps == 120);
}

TEST_CASE("Step: forces, gravity scale, damping and the three body types", "[physics][step]")
{
    PhysicsWorld world;
    StepConfig config;
    config.gravity = Vec3(0, -10, 0);
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
    const f32 dt = StepSeconds();
    const BodyId pushed = MakeBall(world, Vec3(0, 0, 0), Vec3(0, 0, 0));
    const f32 mass = world.Find(pushed)->mass.mass;
    RigidBody settings = world.Find(pushed)->settings;
    settings.gravity_scale = 0.0f;
    world.SetSettings(pushed, settings);
    REQUIRE(world.ApplyForce(pushed, Vec3(mass, 0, 0), Vec3(0, 0, 0)) == BodyStatus::Ok);
    const BodyId floating = MakeBall(world, Vec3(0, 0, 10), Vec3(2, 0, 0));
    RigidBody damped = world.Find(floating)->settings;
    damped.gravity_scale = 0.0f;
    damped.linear_damping = 1.0f;
    world.SetSettings(floating, damped);
    const BodyId kinematic = MakeBall(world, Vec3(0, 0, 20), Vec3(0, 6, 0), BodyType::Kinematic);
    const BodyId wall = MakeBall(world, Vec3(0, 0, 30), Vec3(0, 0, 0), BodyType::Static);
    world.Step();
    // A force of m newtons accelerates by 1 m/s^2 for one step; it is cleared afterwards.
    CHECK(Abs(world.Find(pushed)->linear_velocity.x - dt) < 1e-6f);
    CHECK(world.Find(pushed)->linear_velocity.y == 0.0f);
    CHECK(world.Find(pushed)->force.x == 0.0f);
    world.Step();
    CHECK(Abs(world.Find(pushed)->linear_velocity.x - dt) < 1e-6f);
    // Damping: v / (1 + c dt).
    CHECK(Abs(world.Find(floating)->linear_velocity.x - 2.0f / (1.0f + dt) / (1.0f + dt)) < 1e-5f);
    // A kinematic body moves by its velocity and ignores gravity; a static one does not move.
    CHECK(Abs(world.Find(kinematic)->position.y - 2.0f * 6.0f * dt) < 1e-6f);
    CHECK(world.Find(kinematic)->linear_velocity.y == 6.0f);
    CHECK(world.Find(wall)->position.y == 0.0f);
    CHECK(world.Find(wall)->linear_velocity.y == 0.0f);
}

TEST_CASE("Step: a spinning body keeps a unit quaternion over 100000 steps and spins about its centre of mass",
          "[physics][step]")
{
    PhysicsWorld world;
    StepConfig config;
    config.gravity = Vec3(0, 0, 0);
    world.SetStepConfig(config);
    BodyDesc desc;
    desc.settings.angular_damping = 0.0f;
    desc.velocity.angular = Vec3(1.3f, 2.1f, -0.7f);
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
    c.offset = Vec3(0, 2, 0);  // the centre of mass is not the origin
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    const Vec3 center = world.WorldCenter(id);
    for (int i = 0; i < 100000; ++i)
    {
        world.Step();
    }
    const Quaternion q = world.Find(id)->orientation;
    CHECK(Abs(q.LengthSquared() - 1.0f) < 2e-6f);
    const Vec3 after = world.WorldCenter(id);
    CHECK(Abs(after.x - center.x) < 2e-3f);
    CHECK(Abs(after.y - center.y) < 2e-3f);
    CHECK(Abs(after.z - center.z) < 2e-3f);
}

TEST_CASE("Step: an absurd but finite spin does not abort the step", "[physics][step]")
{
    PhysicsWorld world;
    BodyDesc desc;
    desc.velocity.angular = Vec3(1e30f, 0, 0);
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeSphere(0.5f);
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    for (int i = 0; i < 10; ++i)
    {
        world.Step();
    }
    CHECK(Abs(world.Find(id)->orientation.LengthSquared() - 1.0f) < 1e-5f);
}

TEST_CASE("Update: the accumulator runs whole steps, caps them and drops the rest", "[physics][step]")
{
    PhysicsWorld world;
    // 10 ms frames: 60 of them are 0.6 s, which is 36 steps (give or take the float rounding of the sum).
    u32 total = 0;
    for (int i = 0; i < 60; ++i)
    {
        total += world.Update(0.01f);
    }
    CHECK(total >= 35);
    CHECK(total <= 36);
    CHECK(world.GetStepStats().dropped_updates == 0);
    // A 100 ms stall is 6 steps: 4 run, the rest is dropped and counted, and the next frame is not longer for it.
    PhysicsWorld stalled;
    CHECK(stalled.Update(0.1f) == 4);
    CHECK(stalled.GetStepStats().last_substeps == 4);
    CHECK(stalled.GetStepStats().dropped_updates == 1);
    CHECK(Abs(static_cast<f32>(stalled.GetStepStats().dropped_seconds) - (0.1f - 4.0f / 60.0f)) < 1e-4f);
    CHECK(stalled.Update(0.0f) == 0);
    CHECK(stalled.Update(StepSeconds() * 1.01f) == 1);
    // Bad times run nothing.
    CHECK(stalled.Update(-1.0f) == 0);
    CHECK(stalled.Update(testing::kNaN) == 0);
    CHECK(stalled.GetStepStats().steps == 5);
}

TEST_CASE("Update: the rate and the cap come from the config, which is checked", "[physics][step]")
{
    PhysicsWorld world;
    StepConfig config;
    config.step_hz = 120.0f;
    config.max_substeps = 2;
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
    CHECK(world.Update(1.0f / 30.0f + 1e-4f) == 2);
    StepConfig bad = config;
    bad.step_hz = 0.0f;
    CHECK(world.SetStepConfig(bad) == BodyStatus::Invalid);
    bad = config;
    bad.max_substeps = 0;
    CHECK(world.SetStepConfig(bad) == BodyStatus::Invalid);
    bad = config;
    bad.contact_margin = -0.1f;
    CHECK(world.SetStepConfig(bad) == BodyStatus::Invalid);
    bad = config;
    bad.gravity.y = testing::kNaN;
    CHECK(world.SetStepConfig(bad) == BodyStatus::Invalid);
    CHECK(world.GetStepConfig().step_hz == 120.0f);
}

TEST_CASE("Interpolation: the blend of the last two states, no blend after a teleport", "[physics][step]")
{
    PhysicsWorld world;
    StepConfig config;
    config.gravity = Vec3(0, 0, 0);
    world.SetStepConfig(config);
    const BodyId id = MakeBall(world, Vec3(0, 0, 0), Vec3(6, 0, 0));
    world.Step();  // 6 m/s for 1/60 s: 0.1 m
    Vec3 p;
    Quaternion q;
    REQUIRE(world.Interpolated(id, 0.0f, &p, &q));
    CHECK(p.x == 0.0f);
    REQUIRE(world.Interpolated(id, 0.5f, &p, &q));
    CHECK(Abs(p.x - 0.05f) < 1e-6f);
    REQUIRE(world.Interpolated(id, 1.0f, &p, &q));
    CHECK(Abs(p.x - 0.1f) < 1e-6f);
    // A teleport is not blended from the old place.
    world.SetTransform(id, Vec3(50, 0, 0), Quaternion::Identity());
    REQUIRE(world.Interpolated(id, 0.0f, &p, &q));
    CHECK(p.x == 50.0f);
    // The alpha follows the accumulator.
    PhysicsWorld timed;
    timed.Update(StepSeconds() * 1.25f);
    CHECK(Abs(timed.Alpha() - 0.25f) < 1e-3f);
    // Orientations blend along the short arc and stay unit.
    BodyDesc spin;
    spin.velocity.angular = Vec3(0, 60.0f, 0);
    spin.settings.angular_damping = 0.0f;
    const BodyId spinner = world.CreateBody(spin);
    world.Step();
    REQUIRE(world.Interpolated(spinner, 0.5f, &p, &q));
    CHECK(Abs(q.LengthSquared() - 1.0f) < 1e-5f);
    CHECK(q.y > 0.0f);
    // With interpolation off the latest state is returned.
    config.interpolate = false;
    world.SetStepConfig(config);
    REQUIRE(world.Interpolated(id, 0.0f, &p, &q));
    world.Step();
    REQUIRE(world.Interpolated(id, 0.0f, &p, &q));
    CHECK(p.x == world.Find(id)->position.x);
    CHECK(!world.Interpolated(BodyId(), 0.5f, &p, &q));
}

namespace
{

u64 RunScene(u32 steps)
{
    PhysicsWorld world;
    for (u32 i = 0; i < 100; ++i)
    {
        BodyDesc desc;
        desc.position = Vec3(static_cast<f32>(i % 10), 5.0f + static_cast<f32>(i / 10), static_cast<f32>((i * 7) % 11));
        desc.velocity.linear = Vec3(static_cast<f32>(i % 3) - 1.0f, 0.0f, static_cast<f32>(i % 5) * 0.25f);
        desc.velocity.angular = Vec3(static_cast<f32>(i % 4) * 0.5f, 1.0f, -static_cast<f32>(i % 7) * 0.1f);
        desc.settings.linear_damping = 0.1f;
        const BodyId id = world.CreateBody(desc);
        Collider c;
        c.shape = i % 2 == 0 ? Shape::MakeBox(Vec3(0.5f, 0.25f, 0.4f)) : Shape::MakeCapsule(0.3f, 0.2f);
        c.offset = Vec3(0.0f, static_cast<f32>(i % 3) * 0.1f, 0.0f);
        world.AddCollider(id, c);
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

TEST_CASE("Step: the state hash of a 600-step scene is stable and equals the committed constant",
          "[physics][step][hash]")
{
    const u64 first = RunScene(600);
    CHECK(first == RunScene(600));
    CHECK(first != RunScene(599));
    CHECK(first == 0x25109949efc555a6ull);
}

TEST_CASE("Hash: a state with a NaN is reported invalid", "[physics][step][hash]")
{
    PhysicsWorld world;
    BodyDesc desc;
    const BodyId id = world.CreateBody(desc);
    world.Find(id)->position.x = testing::kNaN;
    bool valid = true;
    world.StateHash(&valid);
    CHECK(!valid);
}

TEST_CASE("Settings: the step settings are declared and read back", "[physics][step][settings]")
{
    settings::Registry registry;
    RegisterPhysicsSettings(&registry);
    const StepConfig config = StepConfigFromSettings(registry);
    CHECK(config.step_hz == 60.0f);
    CHECK(config.max_substeps == 4);
    CHECK(config.gravity.y == -9.81f);
    CHECK(config.contact_margin == 0.02f);
    CHECK(config.interpolate);
    CHECK(config.velocity_iterations == 2);
    CHECK(config.substeps == 4);
    PhysicsWorld world;
    CHECK(world.SetStepConfig(config) == BodyStatus::Ok);
    StepConfig bad = config;
    bad.velocity_iterations = 0;
    CHECK(world.SetStepConfig(bad) == BodyStatus::Invalid);
    bad = config;
    bad.substeps = 17;
    CHECK(world.SetStepConfig(bad) == BodyStatus::Invalid);
    bad = config;
    bad.contact_slop = -0.1f;
    CHECK(world.SetStepConfig(bad) == BodyStatus::Invalid);
}

TEST_CASE("System: the schedule steps the world and writes back the dynamic transforms", "[physics][step][system]")
{
    ecs::World world;
    world.SetResource(PhysicsWorld());
    ecs::LocalTransform start;
    start.position = Vec3(0, 10, 0);
    start.scale = Vec3(2, 2, 2);
    Collider ball;
    ball.shape = Shape::MakeSphere(0.5f);
    const ecs::Entity falling = world.Create(RigidBody(), ball, start);
    RigidBody fixed;
    fixed.type = BodyType::Static;
    const ecs::Entity floor = world.Create(fixed, ball, start);
    SyncBodies(world, world.Resource<PhysicsWorld>());
    ecs::Schedule schedule;
    schedule.Add(MakePhysicsStepSystem());
    REQUIRE(schedule.Build().ok);
    // Two steps' time: the body is two steps down and the transform is the latest state; the static entity is
    // untouched.
    schedule.Run(world, 2.0f / 60.0f + 1e-5f);
    const Body* body = world.Resource<PhysicsWorld>().Find(world.Get<BodyLink>(falling)->id);
    REQUIRE(body != nullptr);
    CHECK(world.Resource<PhysicsWorld>().GetStepStats().steps == 2);
    const ecs::LocalTransform* t = world.Get<ecs::LocalTransform>(falling);
    CHECK(t->position.y < 10.0f);
    // The transform is the blend of the last two states by the accumulator's fraction (here almost the previous state).
    Vec3 blended;
    Quaternion unused;
    REQUIRE(world.Resource<PhysicsWorld>().Interpolated(body->id, world.Resource<PhysicsWorld>().Alpha(), &blended,
                                                        &unused));
    CHECK(t->position.y == blended.y);
    CHECK(t->position.y > body->position.y);
    CHECK(t->position.y <= body->previous_position.y);
    CHECK(t->scale.x == 2.0f);
    CHECK(world.Get<ecs::LocalTransform>(floor)->position.y == 10.0f);
    CHECK(schedule.Stats().access_violations == 0);
    CHECK(WriteBackTransforms(world, world.Resource<PhysicsWorld>(), 1.0f) == 1);
}
