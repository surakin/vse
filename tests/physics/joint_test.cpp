// Joints: ball, distance and fixed (Phase 25 Day 398).
#include <catch2/catch_test_macros.hpp>

#include "lang/limits.h"
#include "lang/scalar.h"
#include "physics/world.h"

using namespace vse;
using namespace vse::physics;

namespace
{

constexpr f32 kPiJ = 3.14159265f;

bool JNear(f32 a, f32 b, f32 relative)
{
    return Abs(a - b) <= relative * FMax(Abs(b), 1e-3f);
}

BodyId MakeBall(PhysicsWorld& world, const Vec3& position, f32 radius, f32 mass = 0.0f,
                BodyType type = BodyType::Dynamic)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.settings.mass = mass;
    desc.settings.linear_damping = 0.0f;
    desc.settings.angular_damping = 0.0f;
    desc.position = position;
    const BodyId id = world.CreateBody(desc);
    REQUIRE(id.IsValid());
    Collider c;
    c.shape = Shape::MakeSphere(radius);
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

BodyId MakeSlab(PhysicsWorld& world, const Vec3& position, const Vec3& half, f32 mass = 0.0f,
                BodyType type = BodyType::Dynamic)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.settings.mass = mass;
    desc.settings.linear_damping = 0.0f;
    desc.settings.angular_damping = 0.0f;
    desc.position = position;
    const BodyId id = world.CreateBody(desc);
    REQUIRE(id.IsValid());
    Collider c;
    c.shape = Shape::MakeBox(half);
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

JointId Join(PhysicsWorld& world, const JointDesc& desc)
{
    BodyStatus status = BodyStatus::Ok;
    const JointId id = world.CreateJoint(desc, &status);
    REQUIRE(status == BodyStatus::Ok);
    REQUIRE(id.IsValid());
    return id;
}

}  // namespace

TEST_CASE("Joint: a pendulum has the period of a physical pendulum and keeps its energy", "[physics][joint]")
{
    PhysicsWorld world;
    const f32 length = 2.0f;
    const f32 radius = 0.1f;
    const f32 angle = 0.1f;
    const Vec3 pivot(0, 10, 0);
    const BodyId bob = MakeBall(world, pivot + Vec3(length * Sin(angle), -length * Cos(angle), 0), radius, 1.0f);
    Join(world, world.MakeBallJoint(bob, BodyId(), pivot));
    // A solid sphere hung from a point at distance L: T = 2 pi sqrt((I/m + L^2) / (g L)), I/m = 2/5 r^2.
    const f32 expected = 2.0f * kPiJ * Sqrt((0.4f * radius * radius + length * length) / (9.81f * length));
    const f32 mass = world.Find(bob)->mass.mass;
    const f32 inertia = 0.4f * mass * radius * radius;
    const auto energy = [&]()
    {
        const Body* b = world.Find(bob);
        return 0.5f * mass * b->linear_velocity.LengthSquared() + 0.5f * inertia * b->angular_velocity.LengthSquared() +
               mass * 9.81f * static_cast<f32>(b->position.y);
    };
    const f32 e0 = energy();
    f32 worst = 0.0f;
    Vector<f32> crossings;
    f32 previous = static_cast<f32>(world.Find(bob)->position.x) - pivot.x;
    for (int i = 1; i <= 600; ++i)
    {
        world.Step();
        const f32 x = static_cast<f32>(world.Find(bob)->position.x) - pivot.x;
        if (previous > 0.0f && x <= 0.0f)
        {
            // The time of the downward crossing, by linear interpolation inside the step.
            crossings.Push((static_cast<f32>(i - 1) + previous / (previous - x)) / 60.0f);
        }
        previous = x;
        worst = FMax(worst, Abs(energy() - e0));
    }
    REQUIRE(crossings.Size() >= 4);
    const f32 period = (crossings[crossings.Size() - 1] - crossings[0]) / static_cast<f32>(crossings.Size() - 1);
    INFO("period " << period << " expected " << expected << " energy drift "
                   << worst / (mass * 9.81f * length * (1.0f - Cos(angle))));
    CHECK(JNear(period, expected, 0.02f));
    // The swing's energy is mass * g * the height of the release; the drift is measured against it (1.2 % in 10 s: the
    // soft constraint is slightly dissipative).
    CHECK(worst < 0.02f * mass * 9.81f * length * (1.0f - Cos(angle)));
}

TEST_CASE("Joint: a pendulum at rest carries its weight", "[physics][joint]")
{
    PhysicsWorld world;
    const Vec3 pivot(0, 10, 0);
    const BodyId bob = MakeBall(world, pivot + Vec3(0, -2, 0), 0.1f, 3.0f);
    const JointId joint = Join(world, world.MakeBallJoint(bob, BodyId(), pivot));
    for (int i = 0; i < 240; ++i)
    {
        world.Step();
    }
    Vec3 force;
    Vec3 torque;
    REQUIRE(world.JointReaction(joint, &force, &torque) == BodyStatus::Ok);
    // The force on body B (the world) is what the bob pulls with: its weight, downward.
    INFO("force " << force.x << " " << force.y << " " << force.z);
    CHECK(JNear(Abs(force.y), 3.0f * 9.81f, 0.03f));
    CHECK(Abs(force.x) < 0.5f);
    CHECK((world.Find(bob)->position - (pivot + Vec3(0, -2, 0))).Length() < 0.02f);
}

TEST_CASE("Joint: a chain of ten links hangs straight and settles", "[physics][joint]")
{
    PhysicsWorld world;
    const Vec3 top(0, 10, 0);
    Vector<BodyId> links;
    for (u32 i = 0; i < 10; ++i)
    {
        links.Push(
            MakeSlab(world, top + Vec3(0, -0.25f - 0.5f * static_cast<f32>(i), 0), Vec3(0.05f, 0.25f, 0.05f), 1.0f));
    }
    Join(world, world.MakeBallJoint(links[0], BodyId(), top));
    for (u32 i = 1; i < 10; ++i)
    {
        Join(world, world.MakeBallJoint(links[i - 1], links[i], top + Vec3(0, -0.5f * static_cast<f32>(i), 0)));
    }
    // A sideways push, then the chain swings back and hangs.
    world.Find(links[9])->linear_velocity = Vec3(1.0f, 0, 0);
    for (int i = 0; i < 1800; ++i)
    {
        world.Step();
    }
    f32 sideways = 0.0f;
    f32 stretch = 0.0f;
    for (u32 i = 0; i < 10; ++i)
    {
        const Body* b = world.Find(links[i]);
        sideways = FMax(sideways, Abs(static_cast<f32>(b->position.x)));
        stretch = FMax(stretch, Abs(static_cast<f32>(b->position.y) - (top.y - 0.25f - 0.5f * static_cast<f32>(i))));
    }
    INFO("sideways " << sideways << " stretch " << stretch);
    CHECK(sideways < 0.5f);
    CHECK(stretch < 0.1f);
    f32 speed = 0.0f;
    for (u32 i = 0; i < 10; ++i)
    {
        speed = FMax(speed, world.Find(links[i])->linear_velocity.Length());
    }
    CHECK(speed < 1.0f);
}

TEST_CASE("Joint: a rigid distance joint holds its length under load", "[physics][joint]")
{
    PhysicsWorld world;
    const Vec3 anchor(0, 10, 0);
    const BodyId ball = MakeBall(world, anchor + Vec3(0, -3, 0), 0.2f, 20.0f);
    Join(world, world.MakeDistanceJoint(ball, BodyId(), anchor + Vec3(0, -3, 0), anchor));
    for (int i = 0; i < 300; ++i)
    {
        world.Step();
    }
    const f32 length = (world.Find(ball)->position - anchor).Length();
    CHECK(Abs(length - 3.0f) < 0.02f);
}

TEST_CASE("Joint: a spring joint oscillates at its frequency", "[physics][joint]")
{
    PhysicsWorld world;
    StepConfig config = world.GetStepConfig();
    config.gravity = Vec3(0, 0, 0);
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
    const Vec3 anchor(0, 10, 0);
    const f32 rest = 2.0f;
    const BodyId ball = MakeBall(world, anchor + Vec3(0, -rest - 0.3f, 0), 0.2f, 2.0f);
    JointDesc desc = world.MakeDistanceJoint(ball, BodyId(), anchor + Vec3(0, -rest - 0.3f, 0), anchor);
    desc.length = rest;
    desc.min_length = rest;
    desc.max_length = rest;
    desc.hertz = 1.5f;
    desc.damping_ratio = 0.0f;
    Join(world, desc);
    Vector<f32> crossings;
    f32 previous = world.Find(ball)->position.y - (anchor.y - rest);
    for (int i = 1; i <= 900; ++i)
    {
        world.Step();
        const f32 y = world.Find(ball)->position.y - (anchor.y - rest);
        if (previous < 0.0f && y >= 0.0f)
        {
            crossings.Push((static_cast<f32>(i - 1) + (-previous) / (y - previous)) / 60.0f);
        }
        previous = y;
    }
    REQUIRE(crossings.Size() >= 3);
    const f32 period = (crossings[crossings.Size() - 1] - crossings[0]) / static_cast<f32>(crossings.Size() - 1);
    INFO("period " << period << " expected " << 1.0f / 1.5f);
    CHECK(JNear(period, 1.0f / 1.5f, 0.03f));
}

TEST_CASE("Joint: a rope stops a falling body at its longest", "[physics][joint]")
{
    PhysicsWorld world;
    const Vec3 anchor(0, 10, 0);
    const BodyId ball = MakeBall(world, anchor + Vec3(0, -0.5f, 0), 0.2f, 5.0f);
    JointDesc desc = world.MakeDistanceJoint(ball, BodyId(), anchor + Vec3(0, -0.5f, 0), anchor);
    desc.length = 0.5f;
    desc.min_length = 0.1f;
    desc.max_length = 3.0f;
    Join(world, desc);
    for (int i = 0; i < 600; ++i)
    {
        world.Step();
    }
    const f32 length = (world.Find(ball)->position - anchor).Length();
    INFO("length " << length);
    CHECK(Abs(length - 3.0f) < 0.05f);
}

TEST_CASE("Joint: a fixed joint holds a body rigid and breaks over its threshold", "[physics][joint]")
{
    for (int variant = 0; variant < 2; ++variant)
    {
        PhysicsWorld world;
        const BodyId wall = MakeSlab(world, Vec3(0, 5, 0), Vec3(0.5f, 0.5f, 0.5f), 0.0f, BodyType::Static);
        const BodyId arm = MakeSlab(world, Vec3(1.5f, 5, 0), Vec3(1.0f, 0.1f, 0.1f), 2.0f);
        JointDesc desc = world.MakeFixedJoint(wall, arm, Vec3(0.5f, 5, 0));
        const f32 weight = world.Find(arm)->mass.mass * 9.81f;
        desc.break_force = variant == 0 ? 0.0f : 0.5f * weight;
        const JointId joint = Join(world, desc);
        for (int i = 0; i < 240; ++i)
        {
            world.Step();
        }
        if (variant == 0)
        {
            const Body* b = world.Find(arm);
            INFO("position " << b->position.x << " " << b->position.y << " " << b->position.z);
            CHECK((b->position - Vec3(1.5f, 5, 0)).Length() < 0.02f);
            Vec3 force;
            Vec3 torque;
            REQUIRE(world.JointReaction(joint, &force, &torque) == BodyStatus::Ok);
            CHECK(JNear(Abs(force.y), weight, 0.03f));
            // The weight acts 1 m from the joint, so the joint carries a torque of weight * 1.
            CHECK(JNear(Abs(torque.z), weight * 1.0f, 0.05f));
            CHECK(world.GetStepStats().joints_broken == 0);
        }
        else
        {
            CHECK(world.GetStepStats().joints_broken == 1);
            CHECK(world.JointCount() == 0);
            CHECK(world.Find(arm)->position.y < 4.0f);
        }
    }
}

TEST_CASE("Joint: connected bodies collide only when asked to", "[physics][joint]")
{
    for (int variant = 0; variant < 2; ++variant)
    {
        PhysicsWorld world;
        StepConfig config = world.GetStepConfig();
        config.gravity = Vec3(0, 0, 0);
        REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
        const BodyId a = MakeBall(world, Vec3(-0.3f, 0, 0), 0.5f, 1.0f);
        const BodyId b = MakeBall(world, Vec3(0.3f, 0, 0), 0.5f, 1.0f);
        JointDesc desc = world.MakeBallJoint(a, b, Vec3(0, 0, 0));
        desc.collide_connected = variant == 1;
        Join(world, desc);
        for (int i = 0; i < 60; ++i)
        {
            world.Step();
        }
        const f32 gap = world.Find(b)->position.x - world.Find(a)->position.x;
        INFO("variant " << variant << " gap " << gap);
        if (variant == 0)
        {
            CHECK(Abs(gap - 0.6f) < 0.02f);
        }
        else
        {
            // The ball joint holds the anchors together, but the spheres push their bodies apart about it.
            CHECK(gap > 0.6f);
        }
    }
}

TEST_CASE("Joint: creating and destroying", "[physics][joint]")
{
    PhysicsWorld world;
    const BodyId a = MakeBall(world, Vec3(0, 0, 0), 0.5f, 1.0f);
    const BodyId b = MakeBall(world, Vec3(2, 0, 0), 0.5f, 1.0f);
    const BodyId floor = MakeSlab(world, Vec3(0, -5, 0), Vec3(1, 1, 1), 0.0f, BodyType::Static);
    BodyStatus status = BodyStatus::Ok;
    // Two bodies that cannot move.
    CHECK(!world.CreateJoint(world.MakeBallJoint(floor, BodyId(), Vec3(0, -5, 0)), &status).IsValid());
    CHECK(status == BodyStatus::Invalid);
    // Neither body: a joint needs one.
    JointDesc none = world.MakeBallJoint(a, b, Vec3(1, 0, 0));
    none.body_a = BodyId();
    none.body_b = BodyId();
    CHECK(!world.CreateJoint(none, &status).IsValid());
    CHECK(status == BodyStatus::Invalid);
    // A stale body.
    JointDesc stale = world.MakeBallJoint(a, b, Vec3(1, 0, 0));
    stale.body_b.generation += 5;
    CHECK(!world.CreateJoint(stale, &status).IsValid());
    CHECK(status == BodyStatus::StaleId);
    // Numbers that are not finite, a length that is not positive.
    JointDesc bad = world.MakeBallJoint(a, b, Vec3(1, 0, 0));
    bad.anchor_a.x = Limits<f32>::Infinity();
    CHECK(!world.CreateJoint(bad, &status).IsValid());
    CHECK(status == BodyStatus::Invalid);
    JointDesc distance = world.MakeDistanceJoint(a, b, Vec3(0, 0, 0), Vec3(2, 0, 0));
    distance.length = 0.0f;
    CHECK(!world.CreateJoint(distance, &status).IsValid());
    // A joint of a body goes with it.
    const JointId joint = Join(world, world.MakeBallJoint(a, b, Vec3(1, 0, 0)));
    CHECK(world.JointCount() == 1);
    REQUIRE(world.DestroyBody(a) == BodyStatus::Ok);
    CHECK(world.JointCount() == 0);
    CHECK(world.FindJoint(joint) == nullptr);
    CHECK(world.DestroyJoint(joint) == BodyStatus::StaleId);
}

TEST_CASE("Joint: the state hash of a joint scene equals the committed constant", "[physics][joint][hash]")
{
    const auto run = [](int steps)
    {
        PhysicsWorld world;
        const Vec3 top(0, 10, 0);
        Vector<BodyId> links;
        for (u32 i = 0; i < 6; ++i)
        {
            links.Push(MakeSlab(world, top + Vec3(0, -0.25f - 0.5f * static_cast<f32>(i), 0), Vec3(0.05f, 0.25f, 0.05f),
                                1.0f));
        }
        Join(world, world.MakeBallJoint(links[0], BodyId(), top));
        for (u32 i = 1; i < 6; ++i)
        {
            Join(world, world.MakeBallJoint(links[i - 1], links[i], top + Vec3(0, -0.5f * static_cast<f32>(i), 0)));
        }
        const BodyId arm = MakeSlab(world, Vec3(3.5f, 5, 0), Vec3(1.0f, 0.1f, 0.1f), 2.0f);
        const BodyId wall = MakeSlab(world, Vec3(2, 5, 0), Vec3(0.5f, 0.5f, 0.5f), 0.0f, BodyType::Static);
        Join(world, world.MakeFixedJoint(wall, arm, Vec3(2.5f, 5, 0)));
        const BodyId ball = MakeBall(world, Vec3(-3, 8, 0), 0.2f, 1.0f);
        JointDesc rope = world.MakeDistanceJoint(ball, BodyId(), Vec3(-3, 8, 0), Vec3(-3, 11, 0));
        rope.min_length = 0.5f;
        rope.max_length = 3.0f;
        Join(world, rope);
        world.Find(links[5])->linear_velocity = Vec3(1.5f, 0, 0.5f);
        for (int i = 0; i < steps; ++i)
        {
            world.Step();
        }
        bool valid = false;
        const u64 hash = world.StateHash(&valid);
        REQUIRE(valid);
        return hash;
    };
    const u64 first = run(600);
    CHECK(first == run(600));
    CHECK(first != run(599));
    CHECK(first == 0xad0fffc8778b814full);
}
