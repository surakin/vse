// The hinge joint: limits, motor, spring, full turns and the loaded arm (Phase 25 Day 399).
#include <catch2/catch_test_macros.hpp>

#include "lang/scalar.h"
#include "physics/world.h"

using namespace vse;
using namespace vse::physics;

namespace
{

bool HingeNear(f32 a, f32 b, f32 relative)
{
    return Abs(a - b) <= relative * FMax(Abs(b), 1e-3f);
}

BodyId HingePlank(PhysicsWorld& world, const Vec3& position, const Vec3& half, f32 mass,
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

JointId MakeHinge(PhysicsWorld& world, const JointDesc& desc)
{
    BodyStatus status = BodyStatus::Ok;
    const JointId id = world.CreateJoint(desc, &status);
    REQUIRE(status == BodyStatus::Ok);
    REQUIRE(id.IsValid());
    return id;
}

void HingeNoGravity(PhysicsWorld& world)
{
    StepConfig config = world.GetStepConfig();
    config.gravity = Vec3(0, 0, 0);
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
}

}  // namespace

TEST_CASE("Hinge: a door swings under gravity and stops at its limit", "[physics][joint][hinge]")
{
    PhysicsWorld world;
    const BodyId door = HingePlank(world, Vec3(0.5f, 5, 0), Vec3(0.5f, 0.05f, 0.05f), 4.0f);
    JointDesc desc = world.MakeHingeJoint(BodyId(), door, Vec3(0, 5, 0), Vec3(0, 0, 1));
    desc.enable_limit = true;
    desc.lower_angle = -1.0f;
    desc.upper_angle = 0.3f;
    const JointId joint = MakeHinge(world, desc);
    f32 lowest = 0.0f;
    for (int i = 0; i < 600; ++i)
    {
        world.Step();
        f32 angle = 0.0f;
        REQUIRE(world.JointAngle(joint, &angle) == BodyStatus::Ok);
        lowest = FMin(lowest, angle);
    }
    f32 angle = 0.0f;
    REQUIRE(world.JointAngle(joint, &angle) == BodyStatus::Ok);
    INFO("lowest " << lowest << " final " << angle);
    CHECK(lowest > -1.05f);
    CHECK(Abs(angle + 1.0f) < 0.02f);
    CHECK(world.Find(door)->angular_velocity.Length() < 0.05f);
}

TEST_CASE("Hinge: a door pushed against its limit does not creep", "[physics][joint][hinge]")
{
    PhysicsWorld world;
    const BodyId door = HingePlank(world, Vec3(0.5f, 5, 0), Vec3(0.5f, 0.05f, 0.05f), 4.0f);
    JointDesc desc = world.MakeHingeJoint(BodyId(), door, Vec3(0, 5, 0), Vec3(0, 0, 1));
    desc.enable_limit = true;
    desc.lower_angle = -0.5f;
    desc.upper_angle = 0.5f;
    const JointId joint = MakeHinge(world, desc);
    f32 at_300 = 0.0f;
    for (int i = 0; i < 900; ++i)
    {
        world.Step();
        if (i == 299)
        {
            REQUIRE(world.JointAngle(joint, &at_300) == BodyStatus::Ok);
        }
    }
    f32 angle = 0.0f;
    REQUIRE(world.JointAngle(joint, &angle) == BodyStatus::Ok);
    INFO("angle at 300 " << at_300 << " at 900 " << angle);
    CHECK(Abs(angle - at_300) < 0.002f);
}

TEST_CASE("Hinge: a motor spins a wheel to its speed and stalls at its torque cap", "[physics][joint][hinge]")
{
    for (int variant = 0; variant < 2; ++variant)
    {
        PhysicsWorld world;
        HingeNoGravity(world);
        const BodyId wheel = HingePlank(world, Vec3(0, 5, 0), Vec3(0.5f, 0.5f, 0.1f), 3.0f);
        JointDesc desc = world.MakeHingeJoint(BodyId(), wheel, Vec3(0, 5, 0), Vec3(0, 0, 1));
        desc.enable_motor = true;
        desc.motor_speed = variant == 0 ? 5.0f : 50.0f;
        desc.max_motor_torque = variant == 0 ? 1000.0f : 2.0f;
        MakeHinge(world, desc);
        for (int i = 0; i < 60; ++i)
        {
            world.Step();
        }
        const f32 spin = world.Find(wheel)->angular_velocity.z;
        if (variant == 0)
        {
            CHECK(HingeNear(spin, 5.0f, 0.02f));
        }
        else
        {
            // The torque is 2 N m on the inertia about the axis: omega = T t / I.
            const Vec3 inertia = world.Find(wheel)->mass.inertia;
            const f32 expected = 2.0f * 1.0f / inertia.z;
            INFO("spin " << spin << " expected " << expected);
            CHECK(HingeNear(spin, expected, 0.03f));
        }
    }
}

TEST_CASE("Hinge: the angle is tracked across ten full turns", "[physics][joint][hinge]")
{
    PhysicsWorld world;
    HingeNoGravity(world);
    const BodyId wheel = HingePlank(world, Vec3(0, 5, 0), Vec3(0.5f, 0.5f, 0.1f), 3.0f);
    JointDesc desc = world.MakeHingeJoint(BodyId(), wheel, Vec3(0, 5, 0), Vec3(0, 0, 1));
    desc.enable_motor = true;
    desc.motor_speed = 5.0f;
    desc.max_motor_torque = 1000.0f;
    const JointId joint = MakeHinge(world, desc);
    for (int i = 0; i < 780; ++i)
    {
        world.Step();
    }
    f32 angle = 0.0f;
    REQUIRE(world.JointAngle(joint, &angle) == BodyStatus::Ok);
    INFO("angle " << angle);
    CHECK(angle > 10.0f * 6.2831853f);
    CHECK(HingeNear(angle, 5.0f * 13.0f, 0.01f));
}

TEST_CASE("Hinge: a hinge limit beyond a half turn works", "[physics][joint][hinge]")
{
    PhysicsWorld world;
    HingeNoGravity(world);
    const BodyId wheel = HingePlank(world, Vec3(0, 5, 0), Vec3(0.5f, 0.5f, 0.1f), 3.0f);
    JointDesc desc = world.MakeHingeJoint(BodyId(), wheel, Vec3(0, 5, 0), Vec3(0, 0, 1));
    desc.enable_limit = true;
    desc.lower_angle = -1.0f;
    desc.upper_angle = 8.0f;  // beyond pi: more than a turn
    desc.enable_motor = true;
    desc.motor_speed = 4.0f;
    desc.max_motor_torque = 500.0f;
    const JointId joint = MakeHinge(world, desc);
    for (int i = 0; i < 300; ++i)
    {
        world.Step();
    }
    f32 angle = 0.0f;
    REQUIRE(world.JointAngle(joint, &angle) == BodyStatus::Ok);
    INFO("angle " << angle);
    CHECK(Abs(angle - 8.0f) < 0.05f);
}

TEST_CASE("Hinge: a locked arm carries its weight as force and torque", "[physics][joint][hinge]")
{
    PhysicsWorld world;
    const BodyId arm = HingePlank(world, Vec3(0.5f, 5, 0), Vec3(0.5f, 0.05f, 0.05f), 4.0f);
    JointDesc desc = world.MakeHingeJoint(BodyId(), arm, Vec3(0, 5, 0), Vec3(0, 0, 1));
    desc.enable_limit = true;
    desc.lower_angle = 0.0f;
    desc.upper_angle = 0.0f;
    const JointId joint = MakeHinge(world, desc);
    for (int i = 0; i < 240; ++i)
    {
        world.Step();
    }
    Vec3 force;
    Vec3 torque;
    REQUIRE(world.JointReaction(joint, &force, &torque) == BodyStatus::Ok);
    INFO("force " << force.y << " torque " << torque.z);
    // The force and torque are on body B (the world): the arm's weight pulls it down, and the arm's weight acts 0.5 m
    // out.
    CHECK(HingeNear(Abs(force.y), 4.0f * 9.81f, 0.03f));
    CHECK(HingeNear(Abs(torque.z), 4.0f * 9.81f * 0.5f, 0.05f));
    CHECK(world.Find(arm)->position.y > 4.98f);
}

TEST_CASE("Hinge: the axes stay together when the body is twisted", "[physics][joint][hinge]")
{
    PhysicsWorld world;
    HingeNoGravity(world);
    const BodyId plank = HingePlank(world, Vec3(0.5f, 5, 0), Vec3(0.5f, 0.05f, 0.05f), 4.0f);
    MakeHinge(world, world.MakeHingeJoint(BodyId(), plank, Vec3(0, 5, 0), Vec3(0, 0, 1)));
    world.Find(plank)->angular_velocity = Vec3(2.0f, 1.0f, 0.0f);
    for (int i = 0; i < 120; ++i)
    {
        world.Step();
    }
    const Body* b = world.Find(plank);
    // The hinge axis is the world Z; the plank's own Z axis must still point along it.
    const Vec3 axis = b->orientation.RotateVector(Vec3(0, 0, 1));
    INFO("axis " << axis.x << " " << axis.y << " " << axis.z);
    CHECK(axis.z > 0.999f);
    CHECK(Abs(b->angular_velocity.x) < 0.05f);
    CHECK(Abs(b->angular_velocity.y) < 0.05f);
}

TEST_CASE("Hinge: an angle spring oscillates at its frequency", "[physics][joint][hinge]")
{
    PhysicsWorld world;
    HingeNoGravity(world);
    const BodyId wheel = HingePlank(world, Vec3(0, 5, 0), Vec3(0.5f, 0.5f, 0.1f), 3.0f);
    JointDesc desc = world.MakeHingeJoint(BodyId(), wheel, Vec3(0, 5, 0), Vec3(0, 0, 1));
    desc.hertz = 1.0f;
    desc.damping_ratio = 0.0f;
    const JointId joint = MakeHinge(world, desc);
    world.Find(wheel)->angular_velocity = Vec3(0, 0, 1.0f);
    Vector<f32> crossings;
    f32 previous = 0.0f;
    for (int i = 1; i <= 480; ++i)
    {
        world.Step();
        f32 angle = 0.0f;
        REQUIRE(world.JointAngle(joint, &angle) == BodyStatus::Ok);
        if (previous < 0.0f && angle >= 0.0f)
        {
            crossings.Push((static_cast<f32>(i - 1) + (-previous) / (angle - previous)) / 60.0f);
        }
        previous = angle;
    }
    REQUIRE(crossings.Size() >= 3);
    const f32 period = (crossings[crossings.Size() - 1] - crossings[0]) / static_cast<f32>(crossings.Size() - 1);
    INFO("period " << period);
    CHECK(HingeNear(period, 1.0f, 0.03f));
}

TEST_CASE("Hinge: a bridge of hinged planks holds a load", "[physics][joint][hinge]")
{
    PhysicsWorld world;
    const u32 count = 8;
    const f32 plank_length = 0.6f;  // 8 planks, 4.8 m of chain between supports 4.4 m apart
    const BodyId left = HingePlank(world, Vec3(0, 10, 0), Vec3(0.2f, 0.5f, 0.5f), 0.0f, BodyType::Static);
    const BodyId right = HingePlank(world, Vec3(4.4f, 10, 0), Vec3(0.2f, 0.5f, 0.5f), 0.0f, BodyType::Static);
    Vector<BodyId> planks;
    for (u32 i = 0; i < count; ++i)
    {
        planks.Push(HingePlank(world, Vec3(0.2f + plank_length * (static_cast<f32>(i) + 0.5f) * 0.9167f, 10, 0),
                               Vec3(plank_length * 0.5f * 0.9167f, 0.04f, 0.4f), 5.0f));
    }
    const f32 pitch = plank_length * 0.9167f;
    MakeHinge(world, world.MakeHingeJoint(left, planks[0], Vec3(0.2f, 10, 0), Vec3(0, 0, 1)));
    for (u32 i = 1; i < count; ++i)
    {
        MakeHinge(world, world.MakeHingeJoint(planks[i - 1], planks[i], Vec3(0.2f + pitch * static_cast<f32>(i), 10, 0),
                                              Vec3(0, 0, 1)));
    }
    MakeHinge(world, world.MakeHingeJoint(planks[count - 1], right, Vec3(0.2f + pitch * static_cast<f32>(count), 10, 0),
                                          Vec3(0, 0, 1)));
    const BodyId load = HingePlank(world, Vec3(2.2f, 10.5f, 0), Vec3(0.3f, 0.3f, 0.3f), 40.0f);
    static_cast<void>(load);
    for (int i = 0; i < 1200; ++i)
    {
        world.Step();
    }
    f32 lowest = 10.0f;
    f32 speed = 0.0f;
    for (u32 i = 0; i < count; ++i)
    {
        lowest = FMin(lowest, static_cast<f32>(world.Find(planks[i])->position.y));
        speed = FMax(speed, world.Find(planks[i])->linear_velocity.Length());
    }
    INFO("lowest " << lowest << " speed " << speed << " load y " << world.Find(load)->position.y);
    CHECK(lowest > 7.0f);
    CHECK(lowest < 10.0f);
    CHECK(speed < 0.2f);
    CHECK(world.Find(load)->position.y > lowest);
}

TEST_CASE("Hinge: the state hash of a hinge scene equals the committed constant", "[physics][joint][hinge][hash]")
{
    const auto run = [](int steps)
    {
        PhysicsWorld world;
        const BodyId door = HingePlank(world, Vec3(0.5f, 5, 0), Vec3(0.5f, 0.05f, 0.05f), 4.0f);
        JointDesc desc = world.MakeHingeJoint(BodyId(), door, Vec3(0, 5, 0), Vec3(0, 0, 1));
        desc.enable_limit = true;
        desc.lower_angle = -1.0f;
        desc.upper_angle = 0.3f;
        MakeHinge(world, desc);
        const BodyId wheel = HingePlank(world, Vec3(4, 5, 0), Vec3(0.5f, 0.5f, 0.1f), 3.0f);
        JointDesc motor = world.MakeHingeJoint(BodyId(), wheel, Vec3(4, 5, 0), Vec3(0, 0, 1));
        motor.enable_motor = true;
        motor.motor_speed = 3.0f;
        motor.max_motor_torque = 20.0f;
        MakeHinge(world, motor);
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
    CHECK(first == 0xebde560bdeb9a442ull);
}

TEST_CASE("Hinge: an axis against the frame's Z turns the frame by an exact half turn (no libm)",
          "[physics][joint][hinge]")
{
    // Day 411: the half turn was FromAxisAngle(x, pi), whose cosf(pi / 2) is a libm's last ulp, not the exact zero.
    PhysicsWorld world;
    const BodyId door = HingePlank(world, Vec3(0.5f, 5, 0), Vec3(0.5f, 0.05f, 0.05f), 4.0f);
    const JointDesc desc = world.MakeHingeJoint(BodyId(), door, Vec3(0, 5, 0), Vec3(0, 0, -1));
    CHECK(desc.frame_a.x == 1.0f);
    CHECK(desc.frame_a.y == 0.0f);
    CHECK(desc.frame_a.z == 0.0f);
    CHECK(desc.frame_a.w == 0.0f);
}
