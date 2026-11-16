// The ECS to physics sync (Phase 25 Day 376).
#include <catch2/catch_test_macros.hpp>

#include "ecs/transform.h"
#include "ecs/world.h"
#include "physics/sync.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

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
