// Islands and sleeping (Phase 25 Day 400): a resting stack falls asleep and costs nothing, wakes by the stated rules,
// and the island decomposition does not depend on the order the bodies were made in.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "lang/scalar.h"
#include "os/os.h"
#include "physics/world.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

namespace
{

BodyId SlStatic(PhysicsWorld& world, const Vec3& position, const Vec3& half)
{
    BodyDesc desc;
    desc.settings.type = BodyType::Static;
    desc.position = position;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeBox(half);
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

BodyId SlBox(PhysicsWorld& world, const Vec3& position, u32 flags = 0)
{
    BodyDesc desc;
    desc.position = position;
    desc.settings.flags = flags;
    const BodyId id = world.CreateBody(desc);
    REQUIRE(id.IsValid());
    Collider c;
    c.shape = Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

u32 SlAsleep(const PhysicsWorld& world, const Vector<BodyId>& bodies)
{
    u32 n = 0;
    for (usize i = 0; i < bodies.Size(); ++i)
    {
        n += world.IsSleeping(bodies[i]) ? 1 : 0;
    }
    return n;
}

// Steps until every body sleeps or `limit` steps passed; returns the steps taken.
u32 SlSettle(PhysicsWorld& world, const Vector<BodyId>& bodies, u32 limit)
{
    u32 steps = 0;
    while (steps < limit && SlAsleep(world, bodies) != bodies.Size())
    {
        world.Step();
        ++steps;
    }
    return steps;
}

Vector<BodyId> SlStack(PhysicsWorld& world, const Vec3& base, u32 count)
{
    Vector<BodyId> stack;
    for (u32 i = 0; i < count; ++i)
    {
        stack.Push(SlBox(world, base + Vec3(0, 0.5f + static_cast<f32>(i), 0)));
    }
    return stack;
}

}  // namespace

TEST_CASE("Sleep: a resting stack falls asleep after the set time and costs no constraints", "[physics][sleep]")
{
    PhysicsWorld world;
    SlStatic(world, Vec3(0, -0.5f, 0), Vec3(50, 0.5f, 50));
    const Vector<BodyId> stack = SlStack(world, Vec3(0, 0, 0), 5);
    for (u32 i = 0; i < 20; ++i)
    {
        world.Step();
    }
    CHECK(SlAsleep(world, stack) == 0);  // not before the set time
    CHECK(world.GetSolverStats().manifolds > 0);
    const u32 steps = 20 + SlSettle(world, stack, 600);
    CHECK(SlAsleep(world, stack) == 5);
    CHECK(steps >= 30);  // the half second of rest at 60 Hz
    CHECK(steps < 300);
    world.Step();
    CHECK(world.GetSolverStats().manifolds == 0);
    CHECK(world.GetSolverStats().points == 0);
    CHECK(world.GetStepStats().sleeping_bodies == 5);
    CHECK(world.GetStepStats().islands == 1);
    CHECK(world.GetStepStats().islands_slept == 1);
    // A sleeping body stands still, to the bit, for as long as it sleeps.
    const Vec3 at = world.Find(stack[2])->position;
    for (u32 i = 0; i < 120; ++i)
    {
        world.Step();
    }
    CHECK(world.Find(stack[2])->position.y == at.y);
    CHECK(world.Find(stack[2])->linear_velocity.LengthSquared() == 0.0f);
}

TEST_CASE("Sleep: allow_sleep off and the no-sleep flag keep bodies awake", "[physics][sleep]")
{
    {
        PhysicsWorld world;
        StepConfig config = world.GetStepConfig();
        config.allow_sleep = false;
        REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
        SlStatic(world, Vec3(0, -0.5f, 0), Vec3(50, 0.5f, 50));
        const Vector<BodyId> stack = SlStack(world, Vec3(0, 0, 0), 2);
        for (u32 i = 0; i < 300; ++i)
        {
            world.Step();
        }
        CHECK(SlAsleep(world, stack) == 0);
    }
    {
        // One body that never sleeps holds its whole island awake.
        PhysicsWorld world;
        SlStatic(world, Vec3(0, -0.5f, 0), Vec3(50, 0.5f, 50));
        Vector<BodyId> stack;
        stack.Push(SlBox(world, Vec3(0, 0.5f, 0)));
        stack.Push(SlBox(world, Vec3(0, 1.5f, 0), kBodyNoSleep));
        Vector<BodyId> apart;
        apart.Push(SlBox(world, Vec3(10, 0.5f, 0)));
        for (u32 i = 0; i < 300; ++i)
        {
            world.Step();
        }
        CHECK(SlAsleep(world, stack) == 0);
        CHECK(SlAsleep(world, apart) == 1);  // another island sleeps on
    }
}

TEST_CASE("Sleep: a thrown body that lands wakes a sleeping stack", "[physics][sleep]")
{
    PhysicsWorld world;
    SlStatic(world, Vec3(0, -0.5f, 0), Vec3(50, 0.5f, 50));
    const Vector<BodyId> stack = SlStack(world, Vec3(0, 0, 0), 4);
    REQUIRE(SlSettle(world, stack, 600) < 600);
    const BodyId thrown = SlBox(world, Vec3(0.1f, 6.0f, 0));
    bool woke = false;
    u32 steps = 0;
    while (steps < 60 && !woke)
    {
        world.Step();
        ++steps;
        woke = SlAsleep(world, stack) == 0;
    }
    CHECK(woke);
    CHECK(world.GetStepStats().bodies_woken >= 4);
    // It all comes to rest again.
    Vector<BodyId> all;
    for (usize i = 0; i < stack.Size(); ++i)
    {
        all.Push(stack[i]);
    }
    all.Push(thrown);
    CHECK(SlSettle(world, all, 1200) < 1200);
}

TEST_CASE("Sleep: removing the body under a sleeping stack wakes it and it falls", "[physics][sleep]")
{
    PhysicsWorld world;
    SlStatic(world, Vec3(0, -0.5f, 0), Vec3(50, 0.5f, 50));
    const BodyId platform = SlStatic(world, Vec3(0, 1.0f, 0), Vec3(2, 0.5f, 2));
    const Vector<BodyId> stack = SlStack(world, Vec3(0, 1.5f, 0), 3);
    REQUIRE(SlSettle(world, stack, 600) < 600);
    REQUIRE(SlAsleep(world, stack) == 3);
    REQUIRE(world.DestroyBody(platform) == BodyStatus::Ok);
    CHECK(SlAsleep(world, stack) == 0);
    for (u32 i = 0; i < 40; ++i)
    {
        world.Step();
    }
    CHECK(world.Find(stack[0])->position.y < 1.9f);  // it fell from the platform's height
}

TEST_CASE("Sleep: a body moved away from under a sleeping box wakes it", "[physics][sleep]")
{
    PhysicsWorld world;
    SlStatic(world, Vec3(0, -0.5f, 0), Vec3(50, 0.5f, 50));
    const BodyId platform = SlStatic(world, Vec3(0, 1.0f, 0), Vec3(2, 0.5f, 2));
    Vector<BodyId> box;
    box.Push(SlBox(world, Vec3(0, 2.5f, 0)));
    REQUIRE(SlSettle(world, box, 600) < 600);
    REQUIRE(world.SetTransform(platform, Vec3(20, 1.0f, 0), Quaternion::Identity()) == BodyStatus::Ok);
    CHECK(SlAsleep(world, box) == 0);
}

TEST_CASE("Sleep: a force, an impulse, a velocity, a teleport and a new collider wake the island", "[physics][sleep]")
{
    PhysicsWorld world;
    SlStatic(world, Vec3(0, -0.5f, 0), Vec3(50, 0.5f, 50));
    Vector<BodyId> stack = SlStack(world, Vec3(0, 0, 0), 2);
    const auto sleep_again = [&]
    {
        REQUIRE(SlSettle(world, stack, 900) < 900);
        REQUIRE(SlAsleep(world, stack) == 2);
    };
    sleep_again();
    REQUIRE(world.ApplyForce(stack[1], Vec3(0, 1, 0), world.Find(stack[1])->position) == BodyStatus::Ok);
    CHECK(SlAsleep(world, stack) == 0);  // the island, not only the body
    sleep_again();
    REQUIRE(world.ApplyImpulse(stack[0], Vec3(0, 0.1f, 0), world.Find(stack[0])->position) == BodyStatus::Ok);
    CHECK(SlAsleep(world, stack) == 0);
    sleep_again();
    Velocity v;
    v.linear = Vec3(0, 0.2f, 0);
    REQUIRE(world.SetVelocity(stack[1], v) == BodyStatus::Ok);
    CHECK(SlAsleep(world, stack) == 0);
    sleep_again();
    REQUIRE(world.SetTransform(stack[1], world.Find(stack[1])->position, Quaternion::Identity()) == BodyStatus::Ok);
    CHECK(SlAsleep(world, stack) == 0);
    sleep_again();
    Collider c;
    c.shape = Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
    REQUIRE(world.SetCollider(stack[0], c) == BodyStatus::Ok);
    CHECK(SlAsleep(world, stack) == 0);
    sleep_again();
    REQUIRE(world.Wake(stack[0]) == BodyStatus::Ok);
    CHECK(SlAsleep(world, stack) == 0);
}

TEST_CASE("Sleep: a moving kinematic platform wakes what rests on it and keeps it awake", "[physics][sleep]")
{
    PhysicsWorld world;
    BodyDesc desc;
    desc.settings.type = BodyType::Kinematic;
    desc.position = Vec3(0, -0.5f, 0);
    const BodyId platform = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeBox(Vec3(50, 0.5f, 50));
    REQUIRE(world.AddCollider(platform, c) == BodyStatus::Ok);
    Vector<BodyId> box;
    box.Push(SlBox(world, Vec3(0, 0.5f, 0)));
    REQUIRE(SlSettle(world, box, 600) < 600);
    // The platform creeps sideways, slower than the sleep threshold: the box still does not sleep.
    Velocity v;
    v.linear = Vec3(0.01f, 0, 0);
    REQUIRE(world.SetVelocity(platform, v) == BodyStatus::Ok);
    world.Step();
    CHECK(SlAsleep(world, box) == 0);
    for (u32 i = 0; i < 120; ++i)
    {
        world.Step();
    }
    CHECK(SlAsleep(world, box) == 0);
}

TEST_CASE("Sleep: a joint ties bodies into one island, and a joint to a moving body wakes the other",
          "[physics][sleep]")
{
    PhysicsWorld world;
    SlStatic(world, Vec3(0, -0.5f, 0), Vec3(50, 0.5f, 50));
    const BodyId a = SlBox(world, Vec3(0, 0.5f, 0));
    const BodyId b = SlBox(world, Vec3(3, 0.5f, 0));
    REQUIRE(world.CreateJoint(world.MakeFixedJoint(a, b, Vec3(1.5f, 0.5f, 0))).IsValid());
    Vector<BodyId> both;
    both.Push(a);
    both.Push(b);
    world.Step();
    u32 ia = 0;
    u32 ib = 0;
    REQUIRE(world.IslandOf(a, &ia) == BodyStatus::Ok);
    REQUIRE(world.IslandOf(b, &ib) == BodyStatus::Ok);
    CHECK(ia == ib);
    CHECK(ia != 0);
    REQUIRE(SlSettle(world, both, 900) < 900);
    // A joint made to a sleeping body wakes it.
    const BodyId c = SlBox(world, Vec3(6, 0.5f, 0));
    REQUIRE(world.CreateJoint(world.MakeFixedJoint(b, c, Vec3(4.5f, 0.5f, 0))).IsValid());
    CHECK(SlAsleep(world, both) == 0);
}

TEST_CASE("Sleep: island membership does not depend on the order the bodies were made in", "[physics][sleep]")
{
    struct Spec
    {
        Vec3 position;
        u32 group;
    };
    // Three stacks (groups 0, 1, 2), a pair tied by a joint (3) and a lone box (4).
    Vector<Spec> specs;
    for (u32 g = 0; g < 3; ++g)
    {
        for (u32 i = 0; i < 3; ++i)
        {
            specs.Push(Spec{Vec3(10.0f * static_cast<f32>(g), 0.5f + static_cast<f32>(i), 0), g});
        }
    }
    specs.Push(Spec{Vec3(40, 0.5f, 0), 3});
    specs.Push(Spec{Vec3(43, 0.5f, 0), 3});
    specs.Push(Spec{Vec3(60, 0.5f, 0), 4});
    const usize n = specs.Size();
    u64 seed = 12345;
    for (u32 run = 0; run < 4; ++run)
    {
        // A shuffled creation order (a fixed generator, Fisher-Yates).
        Vector<u32> order;
        for (u32 i = 0; i < n; ++i)
        {
            order.Push(i);
        }
        if (run > 0)
        {
            for (usize i = n - 1; i > 0; --i)
            {
                seed = seed * 6364136223846793005ull + 1442695040888963407ull;
                const usize j = static_cast<usize>((seed >> 33) % (i + 1));
                const u32 t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
        PhysicsWorld world;
        SlStatic(world, Vec3(0, -0.5f, 0), Vec3(100, 0.5f, 50));
        Vector<BodyId> ids;
        ids.Resize(n);
        for (usize k = 0; k < n; ++k)
        {
            ids[order[k]] = SlBox(world, specs[order[k]].position);
        }
        REQUIRE(world.CreateJoint(world.MakeFixedJoint(ids[9], ids[10], Vec3(41.5f, 0.5f, 0))).IsValid());
        world.Step();
        world.Step();
        for (u32 phase = 0; phase < 2; ++phase)
        {
            for (usize i = 0; i < n; ++i)
            {
                u32 isl_i = 0;
                REQUIRE(world.IslandOf(ids[i], &isl_i) == BodyStatus::Ok);
                for (usize j = 0; j < n; ++j)
                {
                    u32 isl_j = 0;
                    REQUIRE(world.IslandOf(ids[j], &isl_j) == BodyStatus::Ok);
                    CHECK((isl_i == isl_j) == (specs[i].group == specs[j].group));
                }
                // The island's number is its lowest body's slot plus one.
                u32 lowest = ids[i].index;
                for (usize j = 0; j < n; ++j)
                {
                    if (specs[j].group == specs[i].group && ids[j].index < lowest)
                    {
                        lowest = ids[j].index;
                    }
                }
                CHECK(isl_i == lowest + 1);
            }
            // The numbers survive sleep.
            for (u32 s = 0; s < 400; ++s)
            {
                world.Step();
            }
        }
        CHECK(world.GetStepStats().islands == 5);
        CHECK(world.GetStepStats().sleeping_bodies == n);
    }
}

TEST_CASE("Sleep: a scene of 10000 bodies mostly asleep steps at a fraction of the awake cost",
          "[physics][sleep][bench]")
{
    PhysicsWorld world;
    // A short rest time, so the awake scene need not be stepped for long (an awake step of 10000 bodies is the slow
    // part).
    StepConfig config = world.GetStepConfig();
    config.sleep_time = 0.02f;
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
    SlStatic(world, Vec3(0, -0.5f, 0), Vec3(500, 0.5f, 500));
    Vector<BodyId> boxes;
    for (u32 z = 0; z < 100; ++z)
    {
        for (u32 x = 0; x < 100; ++x)
        {
            boxes.Push(
                SlBox(world, Vec3(3.0f * static_cast<f32>(x) - 150.0f, 0.5f, 3.0f * static_cast<f32>(z) - 150.0f)));
        }
    }
    world.Step();  // makes the broad phase, which the next step reuses
    const u64 awake_start = os::MonotonicTimeNanos();
    world.Step();
    const u64 awake = os::MonotonicTimeNanos() - awake_start;
    CHECK(SlAsleep(world, boxes) == 0);
    REQUIRE(SlSettle(world, boxes, 30) < 30);
    // One box wakes: the rest stay asleep.
    REQUIRE(world.ApplyImpulse(boxes[5050], Vec3(0, 1, 0), world.Find(boxes[5050])->position) == BodyStatus::Ok);
    CHECK(SlAsleep(world, boxes) == boxes.Size() - 1);
    const u64 mixed_start = os::MonotonicTimeNanos();
    world.Step();
    const u64 mixed = os::MonotonicTimeNanos() - mixed_start;
    REQUIRE(SlSettle(world, boxes, 200) < 200);
    const u64 asleep_start = os::MonotonicTimeNanos();
    world.Step();
    const u64 asleep = os::MonotonicTimeNanos() - asleep_start;
    WARN("one step of 10000 bodies: awake " << awake / 1000 << " us, one awake " << mixed / 1000 << " us, asleep "
                                            << asleep / 1000 << " us");
    CHECK(asleep * 2 < awake);
    CHECK(world.GetSolverStats().manifolds == 0);
}

TEST_CASE("Sleep: puts a body and its island to sleep at once, and wakes like any sleeper", "[physics][sleep]")
{
    PhysicsWorld world;
    StepConfig config;
    config.gravity = Vec3(0, -9.81f, 0);
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
    SlStatic(world, Vec3(0, -0.5f, 0), Vec3(20, 0.5f, 20));
    const BodyId lower = SlBox(world, Vec3(0, 0.5f, 0));
    const BodyId upper = SlBox(world, Vec3(0, 1.5f, 0));
    const BodyId apart = SlBox(world, Vec3(10, 0.5f, 0));
    for (u32 i = 0; i < 3; ++i)
    {
        world.Step();
    }
    REQUIRE_FALSE(world.IsSleeping(lower));
    REQUIRE(world.Sleep(lower) == BodyStatus::Ok);
    CHECK(world.IsSleeping(lower));
    CHECK(world.IsSleeping(upper));  // the island of the stack goes with it
    CHECK_FALSE(world.IsSleeping(apart));
    CHECK(world.Find(lower)->linear_velocity.y == 0.0f);
    // Nothing moves while it sleeps.
    const Vec3 at = world.Find(upper)->position;
    world.Step();
    CHECK(world.Find(upper)->position.y == at.y);
    // A body that was never stepped has no island and sleeps alone.
    PhysicsWorld other;
    const BodyId lone = SlBox(other, Vec3(0, 3, 0));
    REQUIRE(other.Sleep(lone) == BodyStatus::Ok);
    CHECK(other.IsSleeping(lone));
    // An impulse wakes it again.
    REQUIRE(world.ApplyImpulse(upper, Vec3(0, 1, 0), world.Find(upper)->position) == BodyStatus::Ok);
    CHECK_FALSE(world.IsSleeping(upper));
    CHECK_FALSE(world.IsSleeping(lower));
    // Static, stale and kinematic are refused.
    BodyDesc kinematic;
    kinematic.settings.type = BodyType::Kinematic;
    const BodyId mover = world.CreateBody(kinematic);
    CHECK(world.Sleep(mover) == BodyStatus::WrongType);
    CHECK(world.Sleep(BodyId{}) == BodyStatus::StaleId);
    CHECK_FALSE(world.IsSleeping(apart));
}
