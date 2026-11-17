// Events, joints as components, debug drawing and the transform write-back (Phase 25 Day 405).
#include <catch2/catch_test_macros.hpp>

#include "alloc_counter.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "lang/scalar.h"
#include "physics/debug_draw.h"
#include "physics/joint_sync.h"
#include "physics/sync.h"
#include "physics/system.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

namespace
{

BodyId EvBody(PhysicsWorld& world, BodyType type, const Vec3& position, const Shape& shape, bool trigger = false,
              f32 restitution = 0.0f)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.position = position;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = shape;
    c.is_trigger = trigger;
    c.material.restitution = restitution;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

BodyId EvFloor(PhysicsWorld& world)
{
    return EvBody(world, BodyType::Static, Vec3(0, -0.5f, 0), Shape::MakeBox(Vec3(20, 0.5f, 20)), false, 0.8f);
}

}  // namespace

TEST_CASE("Events: a ball that bounces three times begins and ends three contacts in order", "[physics][events]")
{
    PhysicsWorld world;
    EvFloor(world);
    EvBody(world, BodyType::Dynamic, Vec3(0, 1.5f, 0), Shape::MakeSphere(0.5f), false, 0.8f);
    Vector<ContactEventKind> kinds;
    for (u32 i = 0; i < 240 && kinds.Size() < 6; ++i)
    {
        world.Step();
        for (usize k = 0; k < world.Events().contacts.Size(); ++k)
        {
            const ContactEvent& e = world.Events().contacts[k];
            kinds.Push(e.kind);
            if (e.kind == ContactEventKind::Begin)
            {
                CHECK(e.points >= 1);
                CHECK(Abs(e.normal.y) > 0.99f);
                CHECK(e.normal_impulse > 0.0f);
                CHECK(e.body_a.IsValid());
                CHECK(e.body_b.IsValid());
            }
            else
            {
                CHECK(e.points == 0);
            }
        }
    }
    REQUIRE(kinds.Size() == 6);
    for (usize i = 0; i < 6; ++i)
    {
        CHECK(kinds[i] == (i % 2 == 0 ? ContactEventKind::Begin : ContactEventKind::End));
    }
}

TEST_CASE("Events: stay events are reported only when asked for", "[physics][events]")
{
    for (const bool stay : {false, true})
    {
        PhysicsWorld world;
        StepConfig config = world.GetStepConfig();
        config.stay_events = stay;
        config.allow_sleep = false;
        REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
        EvFloor(world);
        EvBody(world, BodyType::Dynamic, Vec3(0, 0.5f, 0), Shape::MakeSphere(0.5f));
        u32 begins = 0;
        u32 stays = 0;
        for (u32 i = 0; i < 30; ++i)
        {
            world.Step();
            for (usize k = 0; k < world.Events().contacts.Size(); ++k)
            {
                begins += world.Events().contacts[k].kind == ContactEventKind::Begin ? 1 : 0;
                stays += world.Events().contacts[k].kind == ContactEventKind::Stay ? 1 : 0;
            }
        }
        CHECK(begins == 1);
        CHECK((stays > 20) == stay);
        CHECK(world.Contacts().Size() == 1);
    }
}

TEST_CASE("Events: a resting pair that falls asleep does not end", "[physics][events]")
{
    PhysicsWorld world;
    StepConfig config = world.GetStepConfig();
    config.sleep_time = 0.05f;
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
    EvFloor(world);
    const BodyId ball = EvBody(world, BodyType::Dynamic, Vec3(0, 0.5f, 0), Shape::MakeSphere(0.5f));
    u32 ends = 0;
    for (u32 i = 0; i < 90; ++i)
    {
        world.Step();
        for (usize k = 0; k < world.Events().contacts.Size(); ++k)
        {
            ends += world.Events().contacts[k].kind == ContactEventKind::End ? 1 : 0;
        }
    }
    CHECK(world.IsSleeping(ball));
    CHECK(ends == 0);
    CHECK(world.Contacts().Size() == 1);
}

TEST_CASE("Events: a trigger reports enter and exit and does not collide", "[physics][events]")
{
    PhysicsWorld world;
    BodyDesc desc;
    desc.settings.type = BodyType::Static;
    desc.position = Vec3(0, 5, 0);
    desc.entity = ecs::Entity{7, 1};
    const BodyId volume = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeBox(Vec3(2, 1, 2));
    c.is_trigger = true;
    REQUIRE(world.AddCollider(volume, c) == BodyStatus::Ok);
    const BodyId ball = EvBody(world, BodyType::Dynamic, Vec3(0, 8, 0), Shape::MakeSphere(0.25f));
    Vector<bool> sequence;
    for (u32 i = 0; i < 240; ++i)
    {
        world.Step();
        for (usize k = 0; k < world.Events().triggers.Size(); ++k)
        {
            const TriggerEvent& e = world.Events().triggers[k];
            sequence.Push(e.enter);
            CHECK(e.body_trigger == volume);
            CHECK(e.body_other == ball);
            CHECK(e.entity_trigger.index == 7);
        }
    }
    REQUIRE(sequence.Size() == 2);
    CHECK(sequence[0]);
    CHECK(!sequence[1]);
    CHECK(world.Find(ball)->position.y < 4.0f);  // it fell through
}

TEST_CASE("Events: a joint that carries too much breaks once and says so", "[physics][events]")
{
    PhysicsWorld world;
    const BodyId anchor = EvBody(world, BodyType::Static, Vec3(0, 10, 0), Shape::MakeSphere(0.1f));
    const BodyId weight = EvBody(world, BodyType::Dynamic, Vec3(0, 8, 0), Shape::MakeSphere(0.5f));
    JointDesc joint = world.MakeBallJoint(anchor, weight, Vec3(0, 10, 0));
    joint.break_force = 1.0f;  // far below the weight of the ball
    const JointId id = world.CreateJoint(joint);
    REQUIRE(id.IsValid());
    u32 breaks = 0;
    for (u32 i = 0; i < 30; ++i)
    {
        world.Step();
        for (usize k = 0; k < world.Events().joint_breaks.Size(); ++k)
        {
            const JointBreakEvent& e = world.Events().joint_breaks[k];
            ++breaks;
            CHECK(e.joint == id);
            CHECK(e.body_a == anchor);
            CHECK(e.body_b == weight);
            CHECK(e.force.LengthSquared() > 1.0f);
        }
    }
    CHECK(breaks == 1);
    CHECK(world.JointCount() == 0);
}

TEST_CASE("Events: destroying a body cancels the events that name it", "[physics][events]")
{
    PhysicsWorld world;
    EvFloor(world);
    const BodyId ball = EvBody(world, BodyType::Dynamic, Vec3(0, 0.5f, 0), Shape::MakeSphere(0.5f));
    world.Step();
    REQUIRE(world.Events().contacts.Size() == 1);
    REQUIRE(world.Contacts().Size() == 1);
    CHECK(world.DestroyBody(ball) == BodyStatus::Ok);
    CHECK(world.Events().contacts.Size() == 0);
    CHECK(world.Contacts().Size() == 0);
    world.Step();
    CHECK(world.Events().Count() == 0);  // no end event for a body that is gone
}

TEST_CASE("Events: Update keeps the events of all its steps", "[physics][events]")
{
    PhysicsWorld world;
    EvFloor(world);
    EvBody(world, BodyType::Dynamic, Vec3(0, 0.5f, 0), Shape::MakeSphere(0.5f));
    const u32 ran = world.Update(3.5f / 60.0f);
    CHECK(ran == 3);
    CHECK(world.Events().contacts.Size() == 1);  // begun in the first step, kept through the others
    world.Update(1.5f / 60.0f);
    CHECK(world.Events().contacts.Size() == 0);  // a new update starts with an empty list
    const PhysicsStats stats = world.AllStats();
    CHECK(stats.contacts == 1);
    CHECK(stats.world.bodies == 2);
    CHECK(stats.step.steps == 5);
}

TEST_CASE("Joint component: entities are tied, and a joint that breaks is not made again", "[physics][events][ecs]")
{
    ecs::World world;
    PhysicsWorld physics;
    RigidBody fixed;
    fixed.type = BodyType::Static;
    Collider ball;
    ball.shape = Shape::MakeSphere(0.3f);
    ecs::LocalTransform top;
    top.position = Vec3(0, 10, 0);
    ecs::LocalTransform low;
    low.position = Vec3(0, 8, 0);
    const ecs::Entity a = world.Create(fixed, ball, top);
    const ecs::Entity b = world.Create(RigidBody(), ball, low);
    Joint joint;
    joint.entity_a = a;
    joint.entity_b = b;
    joint.type = JointType::Ball;
    joint.point_a = Vec3(0, 10, 0);
    joint.break_force = 1.0f;
    const ecs::Entity owner = world.Create(joint);
    // The bodies do not exist yet: the joint waits.
    JointSyncReport report = SyncJoints(world, physics);
    CHECK(report.pending == 1);
    CHECK(report.created == 0);
    SyncBodies(world, physics);
    report = SyncJoints(world, physics);
    CHECK(report.created == 1);
    CHECK(physics.JointCount() == 1);
    REQUIRE(world.Has<JointLink>(owner));
    report = SyncJoints(world, physics);
    CHECK(report.created == 0);
    CHECK(report.destroyed == 0);
    for (u32 i = 0; i < 30; ++i)
    {
        physics.Step();
    }
    CHECK(physics.JointCount() == 0);  // broke
    report = SyncJoints(world, physics);
    CHECK(report.broken == 1);
    CHECK(!world.Has<Joint>(owner));
    CHECK(!world.Has<JointLink>(owner));
    CHECK(SyncJoints(world, physics).created == 0);
}

TEST_CASE("Joint component: destroying an entity removes its body and the joint", "[physics][events][ecs]")
{
    ecs::World world;
    PhysicsWorld physics;
    RigidBody fixed;
    fixed.type = BodyType::Static;
    Collider ball;
    ball.shape = Shape::MakeSphere(0.3f);
    ecs::LocalTransform top;
    top.position = Vec3(0, 10, 0);
    ecs::LocalTransform low;
    low.position = Vec3(0, 9, 0);
    const ecs::Entity a = world.Create(fixed, ball, top);
    const ecs::Entity b = world.Create(RigidBody(), ball, low);
    Joint joint;
    joint.entity_a = a;
    joint.entity_b = b;
    joint.type = JointType::Distance;
    joint.point_a = Vec3(0, 10, 0);
    joint.point_b = Vec3(0, 9, 0);
    const ecs::Entity owner = world.Create(joint);
    SyncBodies(world, physics);
    REQUIRE(SyncJoints(world, physics).created == 1);
    world.Destroy(b);
    CHECK(SyncBodies(world, physics).destroyed == 1);
    CHECK(physics.JointCount() == 0);  // the body took its joint with it
    const JointSyncReport report = SyncJoints(world, physics);
    CHECK(report.broken == 1);  // the link's joint is gone and the component is dropped
    CHECK(!world.Has<JointLink>(owner));
}

TEST_CASE("Debug draw: a known scene has the documented line counts and draws without allocating",
          "[physics][events][debug]")
{
    PhysicsWorld world;
    EvBody(world, BodyType::Static, Vec3(0, 0, 0), Shape::MakeSphere(1.0f));
    EvBody(world, BodyType::Static, Vec3(5, 0, 0), Shape::MakeBox(Vec3(1, 1, 1)));
    EvBody(world, BodyType::Static, Vec3(10, 0, 0), Shape::MakeCapsule(0.5f, 0.25f));
    Vector<spatial::DebugLine> lines;
    DebugDrawOptions options;
    options.contacts = false;
    options.joints = false;
    CHECK(DebugDraw(world, options, &lines) == 48 + 12 + 68);
    options.boxes = true;
    lines.Clear();
    CHECK(DebugDraw(world, options, &lines) == 48 + 12 + 68 + 36);
    // A resting ball adds its contact: one point, one line.
    EvFloor(world);
    EvBody(world, BodyType::Dynamic, Vec3(20, 0.5f, 0), Shape::MakeSphere(0.5f));
    world.Step();
    options.boxes = false;
    options.shapes = false;
    options.contacts = true;
    lines.Clear();
    CHECK(DebugDraw(world, options, &lines) == world.Contacts()[0].manifold.count);
    // A joint adds two lines for a hinge (anchors and axis), one for a ball joint.
    const BodyId anchor = EvBody(world, BodyType::Static, Vec3(0, 20, 0), Shape::MakeSphere(0.1f));
    const BodyId arm = EvBody(world, BodyType::Dynamic, Vec3(1, 20, 0), Shape::MakeSphere(0.1f));
    REQUIRE(world.CreateJoint(world.MakeHingeJoint(anchor, arm, Vec3(0, 20, 0), Vec3(0, 0, 1))).IsValid());
    options.contacts = false;
    options.joints = true;
    lines.Clear();
    CHECK(DebugDraw(world, options, &lines) == 2);
    // After the first frame the vector has its capacity: drawing again allocates nothing.
    options = DebugDrawOptions();
    options.boxes = true;
    lines.Clear();
    DebugDraw(world, options, &lines);
    lines.Clear();
    if (EcsTestCountsAllocations())
    {
        const unsigned long before = EcsTestHeapAllocations();
        const u32 drawn = DebugDraw(world, options, &lines);
        CHECK(drawn > 0);
        CHECK(EcsTestHeapAllocations() == before);
    }
}

TEST_CASE("Write-back: a body at rest leaves its transform unchanged and does not stamp the chunk",
          "[physics][events][ecs]")
{
    ecs::World world;
    world.SetResource(PhysicsWorld());
    PhysicsWorld& physics = world.Resource<PhysicsWorld>();
    StepConfig config = physics.GetStepConfig();
    config.sleep_time = 0.05f;
    REQUIRE(physics.SetStepConfig(config) == BodyStatus::Ok);
    RigidBody fixed;
    fixed.type = BodyType::Static;
    Collider floor;
    floor.shape = Shape::MakeBox(Vec3(20, 0.5f, 20));
    ecs::LocalTransform floor_at;
    floor_at.position = Vec3(0, -0.5f, 0);
    world.Create(fixed, floor, floor_at);
    Collider ball;
    ball.shape = Shape::MakeSphere(0.5f);
    ecs::LocalTransform start;
    start.position = Vec3(0, 0.5f, 0);
    const ecs::Entity e = world.Create(RigidBody(), ball, start, Velocity());  // its own archetype: its own chunk
    SyncBodies(world, physics);
    for (u32 i = 0; i < 90; ++i)
    {
        physics.Step();
    }
    REQUIRE(physics.IsSleeping(world.Get<BodyLink>(e)->id));
    WriteBackTransforms(world, physics, 1.0f);  // the transform takes the body's rest pose once
    auto changed = world.Query<ecs::Changed<ecs::LocalTransform>, ecs::Read<ecs::LocalTransform>>();
    u32 seen = 0;
    changed.ForEach([&](const ecs::LocalTransform&) { ++seen; });  // the first run sees everything
    CHECK(seen > 0);
    changed.Finish();
    CHECK(WriteBackTransforms(world, physics, 1.0f) == 0);
    seen = 0;
    changed.ForEach([&](const ecs::LocalTransform&) { ++seen; });
    CHECK(seen == 0);
    // Wake it and let it move: the transform is written and the change is seen.
    physics.ApplyImpulse(world.Get<BodyLink>(e)->id, Vec3(2, 0, 0), physics.WorldCenter(world.Get<BodyLink>(e)->id));
    physics.Step();
    CHECK(WriteBackTransforms(world, physics, 1.0f) == 1);
    seen = 0;
    changed.ForEach([&](const ecs::LocalTransform&) { ++seen; });
    CHECK(seen == 1);
    changed.Finish();
}

TEST_CASE("Kinematic drive: moving the entity carries the body and what stands on it", "[physics][events][ecs]")
{
    ecs::World world;
    PhysicsWorld physics;
    RigidBody kinematic;
    kinematic.type = BodyType::Kinematic;
    Collider slab;
    slab.shape = Shape::MakeBox(Vec3(3, 0.25f, 3));
    ecs::LocalTransform at;
    at.position = Vec3(0, -0.25f, 0);
    const ecs::Entity platform = world.Create(kinematic, slab, at);
    Collider ball;
    ball.shape = Shape::MakeSphere(0.5f);
    ecs::LocalTransform ball_at;
    ball_at.position = Vec3(0, 0.5f, 0);
    const ecs::Entity rider = world.Create(RigidBody(), ball, ball_at, Velocity());
    SyncBodies(world, physics);
    const f32 dt = 1.0f / 60.0f;
    CHECK(DriveKinematicBodies(world, physics, dt) == 0);  // already where its entity is
    for (u32 i = 0; i < 60; ++i)
    {
        world.Get<ecs::LocalTransform>(platform)->position.x += 0.05f;  // 3 m/s
        const u32 set = DriveKinematicBodies(world, physics, dt);
        CHECK(set <= 1u);
        if (i == 0)
        {
            CHECK(set == 1u);  // later steps may keep the speed it already has
        }
        physics.Step();
    }
    const Body* body = physics.Find(world.Get<BodyLink>(platform)->id);
    CHECK(body->position.x > 2.99f);
    CHECK(body->position.x < 3.01f);
    const Body* carried = physics.Find(world.Get<BodyLink>(rider)->id);
    CHECK(carried->position.x > 0.3f);  // carried along by friction (it rolls on the way)
    // The entity stops: the body stops.
    DriveKinematicBodies(world, physics, dt);
    physics.Step();
    CHECK(physics.Find(world.Get<BodyLink>(platform)->id)->linear_velocity.x < 0.01f);  // only the rounding left
    CHECK(DriveKinematicBodies(world, physics, 0.0f) == 0);
}

TEST_CASE("Joint component: a body lost while its entity lives does not break the joint component",
          "[physics][events][ecs]")
{
    ecs::World world;
    PhysicsWorld physics;
    RigidBody fixed;
    fixed.type = BodyType::Static;
    Collider ball;
    ball.shape = Shape::MakeSphere(0.3f);
    ecs::LocalTransform top;
    top.position = Vec3(0, 10, 0);
    ecs::LocalTransform low;
    low.position = Vec3(0, 9, 0);
    const ecs::Entity a = world.Create(fixed, ball, top);
    const ecs::Entity b = world.Create(RigidBody(), ball, low);
    Joint joint;
    joint.entity_a = a;
    joint.entity_b = b;
    joint.type = JointType::Distance;
    joint.point_a = Vec3(0, 10, 0);
    joint.point_b = Vec3(0, 9, 0);
    const ecs::Entity owner = world.Create(joint);
    SyncBodies(world, physics);
    REQUIRE(SyncJoints(world, physics).created == 1);
    world.Remove<Collider>(b);  // the entity lives; its body (and with it the joint) goes
    CHECK(SyncBodies(world, physics).destroyed == 1);
    CHECK(physics.JointCount() == 0);
    JointSyncReport report = SyncJoints(world, physics);
    CHECK(report.broken == 0);
    CHECK(world.Has<Joint>(owner));  // not dropped as broken
    CHECK(!world.Has<JointLink>(owner));
    world.Add(b, ball);  // the collider comes back: the body and the joint are made again
    SyncBodies(world, physics);
    report = SyncJoints(world, physics);
    CHECK(report.created == 1);
    CHECK(physics.JointCount() == 1);
}
