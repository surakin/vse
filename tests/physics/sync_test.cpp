// The ECS to physics sync (Phase 25 Day 388).
#include <catch2/catch_test_macros.hpp>

#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "lang/scalar.h"
#include "physics/sync.h"

using namespace vse;
using namespace vse::physics;

namespace
{

Collider Ball(f32 radius)
{
    Collider c;
    c.shape = Shape::MakeSphere(radius);
    return c;
}

ecs::LocalTransform At(f32 x, f32 y, f32 z)
{
    ecs::LocalTransform t;
    t.position = Vec3(x, y, z);
    return t;
}

}  // namespace

TEST_CASE("Sync: an entity with the three components gets a body at its transform", "[physics][sync]")
{
    ecs::World world;
    PhysicsWorld physics;
    Velocity v;
    v.linear = Vec3(0, 3, 0);
    const ecs::Entity e = world.Create(RigidBody(), Ball(0.5f), At(1, 2, 3), v);
    const ecs::Entity incomplete = world.Create(RigidBody(), At(0, 0, 0));
    const SyncReport report = SyncBodies(world, physics);
    CHECK(report.created == 1);
    CHECK(report.refused == 0);
    REQUIRE(world.Has<BodyLink>(e));
    CHECK(!world.Has<BodyLink>(incomplete));
    const Body* body = physics.Find(world.Get<BodyLink>(e)->id);
    REQUIRE(body != nullptr);
    CHECK(body->position.y == 2.0f);
    CHECK(body->linear_velocity.y == 3.0f);
    CHECK(body->entity.ToU64() == e.ToU64());
    CHECK(body->collider_count == 1);
    // A second call changes nothing.
    const SyncReport again = SyncBodies(world, physics);
    CHECK(again.created == 0);
    CHECK(again.updated == 0);
    CHECK(again.destroyed == 0);
}

TEST_CASE("Sync: body ids follow entity order, not the order of archetypes", "[physics][sync]")
{
    // Two worlds hold the same entities in different archetypes (a tag on every other one): the body ids still come out
    // in entity order.
    struct Tag
    {
        u32 value = 0;
    };
    ecs::World a;
    ecs::World b;
    ecs::Entity ea[8];
    ecs::Entity eb[8];
    for (int i = 0; i < 8; ++i)
    {
        ea[i] = a.Create(RigidBody(), Ball(0.5f), At(static_cast<f32>(i), 0, 0));
        eb[i] = (i % 2 == 0) ? b.Create(RigidBody(), Ball(0.5f), At(static_cast<f32>(i), 0, 0), Tag{})
                             : b.Create(RigidBody(), Ball(0.5f), At(static_cast<f32>(i), 0, 0));
    }
    PhysicsWorld pa;
    PhysicsWorld pb;
    SyncBodies(a, pa);
    SyncBodies(b, pb);
    for (int i = 0; i < 8; ++i)
    {
        CHECK(a.Get<BodyLink>(ea[i])->id == b.Get<BodyLink>(eb[i])->id);
        CHECK(a.Get<BodyLink>(ea[i])->id.index == static_cast<u32>(i));
    }
}

TEST_CASE("Sync: a destroyed entity or a removed component destroys the body", "[physics][sync]")
{
    ecs::World world;
    PhysicsWorld physics;
    const ecs::Entity dies = world.Create(RigidBody(), Ball(0.5f), At(0, 0, 0));
    const ecs::Entity loses = world.Create(RigidBody(), Ball(0.5f), At(0, 0, 0));
    const ecs::Entity stays = world.Create(RigidBody(), Ball(0.5f), At(0, 0, 0));
    SyncBodies(world, physics);
    REQUIRE(physics.Stats().bodies == 3);
    const BodyId lost_id = world.Get<BodyLink>(loses)->id;
    world.Destroy(dies);
    world.Remove<Collider>(loses);
    const SyncReport report = SyncBodies(world, physics);
    CHECK(report.destroyed == 2);
    CHECK(physics.Stats().bodies == 1);
    CHECK(physics.Find(lost_id) == nullptr);
    CHECK(!world.Has<BodyLink>(loses));
    CHECK(world.Has<BodyLink>(stays));
    // Giving the collider back makes a new body.
    world.Add(loses, Ball(0.25f));
    CHECK(SyncBodies(world, physics).created == 1);
    CHECK(physics.Stats().bodies == 2);
}

TEST_CASE("Sync: a changed component updates the body", "[physics][sync]")
{
    ecs::World world;
    PhysicsWorld physics;
    const ecs::Entity e = world.Create(RigidBody(), Ball(0.5f), At(0, 0, 0));
    SyncBodies(world, physics);
    const BodyId id = world.Get<BodyLink>(e)->id;
    const f32 mass_before = physics.Find(id)->mass.mass;
    world.Get<Collider>(e)->shape = Shape::MakeSphere(1.0f);
    CHECK(SyncBodies(world, physics).updated == 1);
    CHECK(physics.Find(id)->mass.mass > mass_before * 7.0f);
    world.Get<RigidBody>(e)->type = BodyType::Static;
    CHECK(SyncBodies(world, physics).updated == 1);
    CHECK(physics.Find(id)->inverse_mass == 0.0f);
    CHECK(physics.Find(id) != nullptr);
    CHECK(world.Get<BodyLink>(e)->id == id);
}

TEST_CASE("Sync: a changed group or mask reaches the body", "[physics][sync]")
{
    ecs::World world;
    PhysicsWorld physics;
    Collider c = Ball(0.5f);
    const ecs::Entity e = world.Create(RigidBody(), c, At(0, 0, 0));
    SyncBodies(world, physics);
    const BodyId id = world.Get<BodyLink>(e)->id;
    const ColliderRecord* record = physics.FindCollider(physics.Find(id)->first_collider);
    REQUIRE(record != nullptr);
    CHECK(record->value.mask == 0xFFFFFFFFu);
    world.Get<Collider>(e)->group = 4;
    world.Get<Collider>(e)->mask = 0x6u;
    CHECK(SyncBodies(world, physics).updated == 1);
    record = physics.FindCollider(physics.Find(id)->first_collider);
    CHECK(record->value.group == 4);
    CHECK(record->value.mask == 0x6u);
}

TEST_CASE("Sync: an entity with invalid components is counted and skipped", "[physics][sync]")
{
    ecs::World world;
    PhysicsWorld physics;
    world.Create(RigidBody(), Ball(-1.0f), At(0, 0, 0));
    const ecs::Entity good = world.Create(RigidBody(), Ball(0.5f), At(0, 0, 0));
    const SyncReport report = SyncBodies(world, physics);
    CHECK(report.refused == 1);
    CHECK(report.created == 1);
    CHECK(world.Has<BodyLink>(good));
    CHECK(physics.Stats().bodies == 1);
    CHECK(physics.Stats().colliders == 1);
    // The refused entity is reported again, and no body was left behind.
    CHECK(SyncBodies(world, physics).refused == 1);
    CHECK(physics.Stats().bodies == 1);
}

TEST_CASE("Sync: an entity with a parent is not simulated", "[physics][sync]")
{
    ecs::World world;
    PhysicsWorld physics;
    const ecs::Entity parent = world.Create(At(0, 0, 0));
    const ecs::Entity child = world.Create(RigidBody(), Ball(0.5f), At(0, 0, 0));
    world.Add(child, ecs::Parent{parent});
    const SyncReport report = SyncBodies(world, physics);
    CHECK(report.created == 0);
    CHECK(physics.Stats().bodies == 0);
}

TEST_CASE("Sync: an update to an invalid component is refused and the body keeps its last state", "[physics][sync]")
{
    ecs::World world;
    PhysicsWorld physics;
    const ecs::Entity e = world.Create(RigidBody(), Ball(0.5f), At(0, 0, 0));
    SyncBodies(world, physics);
    const BodyId id = world.Get<BodyLink>(e)->id;
    const f32 mass = physics.Find(id)->mass.mass;
    world.Get<Collider>(e)->shape = Shape::MakeSphere(-3.0f);
    const SyncReport report = SyncBodies(world, physics);
    CHECK(report.refused == 1);
    CHECK(report.updated == 0);
    CHECK(physics.Find(id)->mass.mass == mass);
}

TEST_CASE("Sync: the physics world can live as an ECS resource", "[physics][sync]")
{
    ecs::World world;
    world.SetResource(PhysicsWorld());
    world.Create(RigidBody(), Ball(0.5f), At(0, 0, 0));
    SyncBodies(world, world.Resource<PhysicsWorld>());
    CHECK(world.Resource<PhysicsWorld>().Stats().bodies == 1);
}

// glTF colliders Day 1: a static body with a parent is placed by its world transform and follows it.
namespace
{

struct TransformRig
{
    ecs::World world;
    ecs::Schedule schedule;
    PhysicsWorld physics;
    TransformRig()
    {
        world.SetResource(ecs::TransformReport{});
        schedule.Add(ecs::MakeTransformSystem("transforms"));
        REQUIRE(schedule.Build().ok);
    }
    SyncReport Frame()
    {
        schedule.Run(world, 0.016f);
        return SyncBodies(world, physics);
    }
};

RigidBody Static()
{
    RigidBody b;
    b.type = BodyType::Static;
    return b;
}

Collider Slab()
{
    Collider c;
    c.shape = Shape::MakeBox(Vec3(2, 0.5f, 2));
    return c;
}

bool Near(f32 a, f32 b)
{
    return Abs(a - b) < 1e-5f;
}

}  // namespace

TEST_CASE("Sync: a static body with a parent is made at its world transform", "[physics][sync]")
{
    TransformRig rig;
    ecs::LocalTransform parent_at = At(10, 0, 0);
    parent_at.rotation = Quaternion::FromAxisAngle(Vec3(0, 1, 0), 1.5707963f);  // 90 degrees about Y
    const ecs::Entity parent = ecs::CreateTransformEntity(rig.world, parent_at);
    const ecs::Entity child = ecs::CreateTransformEntity(rig.world, At(1, 0, 0));
    REQUIRE(ecs::SetParent(rig.world, child, parent) == ecs::HierarchyStatus::Ok);
    rig.world.Add(child, Static());
    rig.world.Add(child, Slab());
    const SyncReport report = rig.Frame();
    CHECK(report.created == 1);
    REQUIRE(rig.world.Has<BodyLink>(child));
    const Body* body = rig.physics.Find(rig.world.Get<BodyLink>(child)->id);
    REQUIRE(body != nullptr);
    CHECK(Near(static_cast<f32>(body->position.x), 10.0f));  // (1, 0, 0) turned 90 degrees about Y is (0, 0, -1)
    CHECK(Near(static_cast<f32>(body->position.z), -1.0f));
    CHECK(Near(Abs(body->orientation.y), Abs(parent_at.rotation.y)));
    CHECK(Near(Abs(body->orientation.w), Abs(parent_at.rotation.w)));
}

TEST_CASE("Sync: a kinematic or dynamic body with a parent stays unsimulated", "[physics][sync]")
{
    TransformRig rig;
    const ecs::Entity parent = ecs::CreateTransformEntity(rig.world, At(0, 0, 0));
    const ecs::Entity dynamic = ecs::CreateTransformEntity(rig.world, At(1, 0, 0));
    const ecs::Entity kinematic = ecs::CreateTransformEntity(rig.world, At(2, 0, 0));
    REQUIRE(ecs::SetParent(rig.world, dynamic, parent) == ecs::HierarchyStatus::Ok);
    REQUIRE(ecs::SetParent(rig.world, kinematic, parent) == ecs::HierarchyStatus::Ok);
    rig.world.Add(dynamic, RigidBody());
    rig.world.Add(dynamic, Ball(0.5f));
    RigidBody k;
    k.type = BodyType::Kinematic;
    rig.world.Add(kinematic, k);
    rig.world.Add(kinematic, Ball(0.5f));
    const SyncReport report = rig.Frame();
    CHECK(report.created == 0);
    CHECK(rig.physics.Stats().bodies == 0);
}

TEST_CASE("Sync: a parented static body follows its parent and wakes what rests on it", "[physics][sync]")
{
    TransformRig rig;
    const ecs::Entity parent = ecs::CreateTransformEntity(rig.world, At(0, 0, 0));
    const ecs::Entity floor = ecs::CreateTransformEntity(rig.world, At(0, 0, 0));
    REQUIRE(ecs::SetParent(rig.world, floor, parent) == ecs::HierarchyStatus::Ok);
    rig.world.Add(floor, Static());
    rig.world.Add(floor, Slab());
    const ecs::Entity ball = rig.world.Create(RigidBody(), Ball(0.25f), At(0, 0.75f, 0));
    rig.Frame();
    rig.physics.Step();  // the query index and the contacts exist after a step
    const BodyId ball_id = rig.world.Get<BodyLink>(ball)->id;
    REQUIRE(rig.physics.Sleep(ball_id) == BodyStatus::Ok);
    REQUIRE(rig.physics.Find(ball_id)->sleeping);
    // Nothing moved: nothing is re-placed.
    CHECK(rig.Frame().replaced == 0);
    ecs::LocalTransform moved = At(0, 0.1f, 0);
    rig.world.Set(parent, moved);
    const SyncReport report = rig.Frame();
    CHECK(report.replaced == 1);
    const Body* body = rig.physics.Find(rig.world.Get<BodyLink>(floor)->id);
    REQUIRE(body != nullptr);
    CHECK(Near(static_cast<f32>(body->position.y), 0.1f));
    CHECK(!rig.physics.Find(ball_id)->sleeping);
}

TEST_CASE("Sync: the scale of the world transform does not reach the body", "[physics][sync]")
{
    TransformRig rig;
    ecs::LocalTransform big = At(0, 0, 0);
    big.scale = Vec3(2, 2, 2);
    const ecs::Entity parent = ecs::CreateTransformEntity(rig.world, big);
    const ecs::Entity child = ecs::CreateTransformEntity(rig.world, At(1, 0, 0));
    REQUIRE(ecs::SetParent(rig.world, child, parent) == ecs::HierarchyStatus::Ok);
    rig.world.Add(child, Static());
    rig.world.Add(child, Slab());
    rig.Frame();
    const Body* body = rig.physics.Find(rig.world.Get<BodyLink>(child)->id);
    REQUIRE(body != nullptr);
    CHECK(Near(static_cast<f32>(body->position.x), 2.0f));  // the position is the world one
    CHECK(Near(body->orientation.w, 1.0f));                 // the rotation is a unit one, no scale in it
    const ColliderRecord* record = rig.physics.FindCollider(body->first_collider);
    REQUIRE(record != nullptr);
    CHECK(record->value.shape.half_extents.x == 2.0f);  // the shape as given: the caller scales shapes
}

TEST_CASE("Sync: two worlds with parented and unparented bodies get the same body ids", "[physics][sync]")
{
    auto build = [](TransformRig& rig, Vector<u64>* ids)
    {
        const ecs::Entity root = ecs::CreateTransformEntity(rig.world, At(0, 0, 0));
        Vector<ecs::Entity> made;
        for (u32 i = 0; i < 12; ++i)
        {
            const ecs::Entity e = ecs::CreateTransformEntity(rig.world, At(static_cast<f32>(i) * 3.0f, 0, 0));
            if (i % 2 == 0)
            {
                REQUIRE(ecs::SetParent(rig.world, e, root) == ecs::HierarchyStatus::Ok);
                rig.world.Add(e, Static());
                rig.world.Add(e, Slab());
            }
            else
            {
                rig.world.Add(e, RigidBody());
                rig.world.Add(e, Ball(0.5f));
            }
            made.Push(e);
        }
        rig.Frame();
        for (usize i = 0; i < made.Size(); ++i)
        {
            ids->Push(rig.world.Get<BodyLink>(made[i])->id.ToU64());
        }
    };
    TransformRig a;
    TransformRig b;
    Vector<u64> ids_a;
    Vector<u64> ids_b;
    build(a, &ids_a);
    build(b, &ids_b);
    REQUIRE(ids_a.Size() == 12);
    for (usize i = 0; i < ids_a.Size(); ++i)
    {
        CHECK(ids_a[i] == ids_b[i]);
    }
}

TEST_CASE("Sync: a mirrored parent gives a proper rotation", "[physics][sync]")
{
    TransformRig rig;
    ecs::LocalTransform mirror = At(0, 0, 0);
    mirror.scale = Vec3(1, 1, -1);
    const ecs::Entity parent = ecs::CreateTransformEntity(rig.world, mirror);
    const ecs::Entity child = ecs::CreateTransformEntity(rig.world, At(0, 0, 0));
    REQUIRE(ecs::SetParent(rig.world, child, parent) == ecs::HierarchyStatus::Ok);
    rig.world.Add(child, Static());
    rig.world.Add(child, Slab());
    CHECK(rig.Frame().created == 1);
    const Body* body = rig.physics.Find(rig.world.Get<BodyLink>(child)->id);
    REQUIRE(body != nullptr);
    CHECK(Near(Abs(body->orientation.w), 1.0f));
}

TEST_CASE("Sync: a collider that differs only in a library id is applied again", "[physics][sync]")
{
    ecs::World world;
    PhysicsWorld physics;
    const ecs::Entity e = world.Create(RigidBody(), Ball(0.5f), At(0, 1, 0));
    CHECK(SyncBodies(world, physics).created == 1);
    Collider* c = world.Get<Collider>(e);
    REQUIRE(c != nullptr);
    c->shape.height_field = 2;  // the same extents and kind, another library entry
    const SyncReport again = SyncBodies(world, physics);
    CHECK(again.updated + again.refused == 1);  // it was compared as different, so SetCollider ran
}
