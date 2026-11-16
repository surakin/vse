// The physics world (Phase 25 Day 376): ids, generations, types, impulses, teleports and a create/destroy stress.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "lang/scalar.h"
#include "math/math_util.h"
#include "physics/world.h"
#include "test_values.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;
using engine_placeholder::physics::testing::kNaN;

namespace
{

BodyDesc Dynamic(const Vec3& position)
{
    BodyDesc desc;
    desc.settings.type = BodyType::Dynamic;
    desc.position = position;
    return desc;
}

Collider UnitBox()
{
    Collider c;
    c.shape = Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
    c.material.density = 1000.0f;
    return c;
}

}  // namespace

TEST_CASE("World: ids are handed out in call order and reused last-freed-first", "[physics][world]")
{
    PhysicsWorld a;
    PhysicsWorld b;
    Vector<BodyId> ids_a;
    Vector<BodyId> ids_b;
    for (PhysicsWorld* w : {&a, &b})
    {
        Vector<BodyId>& ids = w == &a ? ids_a : ids_b;
        for (int i = 0; i < 10; ++i)
        {
            ids.Push(w->CreateBody(Dynamic(Vec3(static_cast<f32>(i), 0, 0))));
        }
        w->DestroyBody(ids[3]);
        w->DestroyBody(ids[7]);
        ids.Push(w->CreateBody(Dynamic(Vec3(0, 0, 0))));  // takes slot 7
        ids.Push(w->CreateBody(Dynamic(Vec3(0, 0, 0))));  // takes slot 3
    }
    REQUIRE(ids_a.Size() == ids_b.Size());
    for (usize i = 0; i < ids_a.Size(); ++i)
    {
        CHECK(ids_a[i] == ids_b[i]);
    }
    CHECK(ids_a[10].index == 7);
    CHECK(ids_a[11].index == 3);
    CHECK(ids_a[10].generation == 2);
}

TEST_CASE("World: a stale id is refused everywhere", "[physics][world]")
{
    PhysicsWorld world;
    const BodyId id = world.CreateBody(Dynamic(Vec3(0, 1, 0)));
    REQUIRE(id.IsValid());
    CHECK(world.DestroyBody(id) == BodyStatus::Ok);
    CHECK(world.DestroyBody(id) == BodyStatus::StaleId);
    CHECK(world.Find(id) == nullptr);
    CHECK(world.SetVelocity(id, Velocity()) == BodyStatus::StaleId);
    CHECK(world.ApplyImpulse(id, Vec3(1, 0, 0), Vec3(0, 0, 0)) == BodyStatus::StaleId);
    CHECK(world.SetTransform(id, Vec3(0, 0, 0), Quaternion::Identity()) == BodyStatus::StaleId);
    CHECK(world.AddCollider(id, UnitBox()) == BodyStatus::StaleId);
    CHECK(world.Find(BodyId()) == nullptr);
    // The slot is reused with a newer generation: the old id still does not reach the new body.
    const BodyId again = world.CreateBody(Dynamic(Vec3(0, 0, 0)));
    CHECK(again.index == id.index);
    CHECK(again.generation != id.generation);
    CHECK(world.Find(id) == nullptr);
    CHECK(world.Find(again) != nullptr);
}

TEST_CASE("World: a body is refused for bad input", "[physics][world]")
{
    PhysicsWorld world;
    BodyStatus why = BodyStatus::Ok;
    BodyDesc desc = Dynamic(Vec3(kNaN, 0, 0));
    CHECK(!world.CreateBody(desc, &why).IsValid());
    CHECK(why == BodyStatus::Invalid);
    desc = Dynamic(Vec3(0, 0, 0));
    desc.orientation = Quaternion(0, 0, 0, 3);
    CHECK(!world.CreateBody(desc, &why).IsValid());
    desc = Dynamic(Vec3(0, 0, 0));
    desc.settings.mass = -2.0f;
    CHECK(!world.CreateBody(desc, &why).IsValid());
    desc = Dynamic(Vec3(0, 0, 0));
    desc.velocity.linear.y = kNaN;
    CHECK(!world.CreateBody(desc, &why).IsValid());
    CHECK(world.Stats().bodies == 0);
    const BodyId id = world.CreateBody(Dynamic(Vec3(0, 0, 0)));
    CHECK(world.AddCollider(id,
                            []
                            {
                                Collider c;
                                c.shape = Shape::MakeSphere(-1.0f);
                                return c;
                            }()) == BodyStatus::Invalid);
    CHECK(world.SetVelocity(id,
                            []
                            {
                                Velocity v;
                                v.linear.x = kNaN;
                                return v;
                            }()) == BodyStatus::Invalid);
    CHECK(world.ApplyImpulse(id, Vec3(kNaN, 0, 0), Vec3(0, 0, 0)) == BodyStatus::Invalid);
    CHECK(world.SetTransform(id, Vec3(0, 0, 0), Quaternion(0, 0, 0, 0)) == BodyStatus::Invalid);
}

TEST_CASE("World: the collider gives the body its mass", "[physics][world]")
{
    PhysicsWorld world;
    const BodyId id = world.CreateBody(Dynamic(Vec3(0, 0, 0)));
    CHECK(world.Find(id)->inverse_mass == 1.0f);  // a point mass of 1 kg until it has a shape
    REQUIRE(world.AddCollider(id, UnitBox()) == BodyStatus::Ok);
    const Body* body = world.Find(id);
    CHECK(body->mass.mass == 1000.0f);
    CHECK(body->inverse_mass == 0.001f);
    CHECK(world.AddCollider(id, UnitBox()) == BodyStatus::Unsupported);
    // A mass override scales the inertia with it.
    RigidBody settings = body->settings;
    settings.mass = 10.0f;
    REQUIRE(world.SetSettings(id, settings) == BodyStatus::Ok);
    CHECK(world.Find(id)->mass.mass == 10.0f);
    CHECK(world.Find(id)->mass.inertia.x < 10.0f);
    // Locked rotation: no inverse inertia.
    settings.flags = kBodyLockRotation;
    REQUIRE(world.SetSettings(id, settings) == BodyStatus::Ok);
    CHECK(world.Find(id)->inverse_inertia_world.col[0].x == 0.0f);
}

TEST_CASE("World: changing the body type at run time", "[physics][world]")
{
    PhysicsWorld world;
    BodyDesc desc = Dynamic(Vec3(0, 0, 0));
    desc.velocity.linear = Vec3(1, 2, 3);
    const BodyId id = world.CreateBody(desc);
    REQUIRE(world.AddCollider(id, UnitBox()) == BodyStatus::Ok);
    RigidBody settings = world.Find(id)->settings;
    settings.type = BodyType::Static;
    REQUIRE(world.SetSettings(id, settings) == BodyStatus::Ok);
    CHECK(world.Find(id)->inverse_mass == 0.0f);
    CHECK(world.Find(id)->linear_velocity.y == 0.0f);
    CHECK(world.SetVelocity(id, Velocity()) == BodyStatus::WrongType);
    CHECK(world.ApplyImpulse(id, Vec3(1, 0, 0), Vec3(0, 0, 0)) == BodyStatus::WrongType);
    settings.type = BodyType::Kinematic;
    REQUIRE(world.SetSettings(id, settings) == BodyStatus::Ok);
    Velocity v;
    v.linear = Vec3(0, 1, 0);
    CHECK(world.SetVelocity(id, v) == BodyStatus::Ok);
    CHECK(world.Find(id)->inverse_mass == 0.0f);
    CHECK(world.ApplyForce(id, Vec3(1, 0, 0), Vec3(0, 0, 0)) == BodyStatus::WrongType);
    settings.type = BodyType::Dynamic;
    REQUIRE(world.SetSettings(id, settings) == BodyStatus::Ok);
    CHECK(world.Find(id)->inverse_mass == 0.001f);
}

TEST_CASE("World: impulses and forces act through the centre of mass", "[physics][world]")
{
    PhysicsWorld world;
    const BodyId id = world.CreateBody(Dynamic(Vec3(0, 0, 0)));
    REQUIRE(world.AddCollider(id, UnitBox()) == BodyStatus::Ok);
    const f32 inverse_mass = world.Find(id)->inverse_mass;
    // An impulse through the centre does not spin the body.
    REQUIRE(world.ApplyImpulse(id, Vec3(10, 0, 0), Vec3(0, 0, 0)) == BodyStatus::Ok);
    CHECK(world.Find(id)->linear_velocity.x == 10.0f * inverse_mass);
    CHECK(world.Find(id)->angular_velocity.z == 0.0f);
    // One off the centre does: a push along +x at +y turns the body about -z.
    REQUIRE(world.ApplyImpulse(id, Vec3(10, 0, 0), Vec3(0, 0.5f, 0)) == BodyStatus::Ok);
    CHECK(world.Find(id)->angular_velocity.z < 0.0f);
    // A force is kept until the step takes it.
    REQUIRE(world.ApplyForce(id, Vec3(0, 5, 0), Vec3(0.5f, 0, 0)) == BodyStatus::Ok);
    REQUIRE(world.ApplyForce(id, Vec3(0, 5, 0), Vec3(0.5f, 0, 0)) == BodyStatus::Ok);
    CHECK(world.Find(id)->force.y == 10.0f);
    CHECK(world.Find(id)->torque.z > 0.0f);
}

TEST_CASE("World: an offset collider moves the centre of mass", "[physics][world]")
{
    PhysicsWorld world;
    const BodyId id = world.CreateBody(Dynamic(Vec3(1, 0, 0)));
    Collider c = UnitBox();
    c.offset = Vec3(0, 2, 0);
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    const Vec3 center = world.WorldCenter(id);
    CHECK(center.x == 1.0f);
    CHECK(center.y == 2.0f);
    // The same push through the origin now turns the body, because the origin is not the centre.
    REQUIRE(world.ApplyImpulse(id, Vec3(0, 0, 10), Vec3(1, 0, 0)) == BodyStatus::Ok);
    CHECK(Abs(world.Find(id)->angular_velocity.x) > 0.0f);
}

TEST_CASE("World: teleport sets the place, keeps the speed and counts", "[physics][world]")
{
    PhysicsWorld world;
    BodyDesc desc = Dynamic(Vec3(0, 0, 0));
    desc.velocity.linear = Vec3(1, 0, 0);
    const BodyId id = world.CreateBody(desc);
    REQUIRE(world.AddCollider(id, UnitBox()) == BodyStatus::Ok);
    const Quaternion q = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.5f);
    REQUIRE(world.SetTransform(id, Vec3(5, 6, 7), q) == BodyStatus::Ok);
    const Body* body = world.Find(id);
    CHECK(body->position.y == 6.0f);
    CHECK(body->linear_velocity.x == 1.0f);
    CHECK(body->teleports == 1);
    // The world inertia follows the new orientation (a quarter turn about Z swaps x and y of a box with equal sides:
    // unchanged for a cube).
    CHECK(Abs(body->inverse_inertia_world.col[0].x - body->inverse_inertia_world.col[1].y) < 1e-6f);
}

TEST_CASE("World: destroying a body frees its collider", "[physics][world]")
{
    PhysicsWorld world;
    const BodyId id = world.CreateBody(Dynamic(Vec3(0, 0, 0)));
    ColliderId cid;
    REQUIRE(world.AddCollider(id, UnitBox(), &cid) == BodyStatus::Ok);
    CHECK(world.FindCollider(cid) != nullptr);
    CHECK(world.FindCollider(cid)->owner == id);
    CHECK(world.Stats().colliders == 1);
    world.DestroyBody(id);
    CHECK(world.FindCollider(cid) == nullptr);
    CHECK(world.Stats().colliders == 0);
}

TEST_CASE("World: pointers to bodies stay valid while others are made", "[physics][world]")
{
    PhysicsWorld world;
    const BodyId first = world.CreateBody(Dynamic(Vec3(1, 2, 3)));
    const Body* before = world.Find(first);
    for (int i = 0; i < 3000; ++i)
    {
        world.CreateBody(Dynamic(Vec3(0, 0, 0)));
    }
    CHECK(world.Find(first) == before);
    CHECK(before->position.z == 3.0f);
}

TEST_CASE("World: 100000 create and destroy cycles do not grow the world", "[physics][world]")
{
    PhysicsWorld world;
    Vector<BodyId> live;
    for (int i = 0; i < 100; ++i)
    {
        const BodyId id = world.CreateBody(Dynamic(Vec3(0, 0, 0)));
        world.AddCollider(id, UnitBox());
        live.Push(id);
    }
    const PhysicsWorldStats start = world.Stats();
    for (int cycle = 0; cycle < 100000; ++cycle)
    {
        const usize pick = static_cast<usize>((static_cast<u32>(cycle) * 2654435761u) % 100u);
        REQUIRE(world.DestroyBody(live[pick]) == BodyStatus::Ok);
        const BodyId id = world.CreateBody(Dynamic(Vec3(0, 0, 0)));
        REQUIRE(world.AddCollider(id, UnitBox()) == BodyStatus::Ok);
        live[pick] = id;
    }
    const PhysicsWorldStats end = world.Stats();
    CHECK(end.bodies == 100);
    CHECK(end.body_slots == start.body_slots);
    CHECK(end.collider_slots == start.collider_slots);
    CHECK(end.colliders == 100);
}
