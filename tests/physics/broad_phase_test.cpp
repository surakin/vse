// The broad phase (Phase 25 Day 377): the pair work list against a brute-force oracle, hysteresis, filters, triggers,
// type changes, swept bounds and the static tree's rebuild.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "math/geometry.h"
#include "physics/broad_phase.h"
#include "test_values.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;
using engine_placeholder::physics::testing::Rng;

namespace
{

Collider ShapeCollider(Rng& rng, u8 layer)
{
    Collider c;
    switch (rng.Next() % 3)
    {
    case 0: c.shape = Shape::MakeSphere(rng.Range(0.2f, 0.6f)); break;
    case 1: c.shape = Shape::MakeBox(Vec3(rng.Range(0.2f, 0.6f), rng.Range(0.2f, 0.6f), rng.Range(0.2f, 0.6f))); break;
    default: c.shape = Shape::MakeCapsule(rng.Range(0.1f, 0.5f), rng.Range(0.15f, 0.4f)); break;
    }
    c.layer = layer;
    return c;
}

BodyId Add(PhysicsWorld& world, BodyType type, const Vec3& position, const Collider& collider, u32 flags = 0)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.settings.flags = flags;
    desc.position = position;
    const BodyId id = world.CreateBody(desc);
    REQUIRE(id.IsValid());
    REQUIRE(world.AddCollider(id, collider) == BodyStatus::Ok);
    return id;
}

Aabb BoxOf(const PhysicsWorld& world, ColliderId id)
{
    const ColliderRecord* record = world.FindCollider(id);
    const Body* body = world.Find(record->owner);
    const Vec3 origin = body->position + body->orientation.ToMat3() * record->value.offset;
    return record->value.shape.Bounds(origin, body->orientation * record->value.rotation);
}

// Every pair of colliders whose boxes overlap, by brute force, in key order: different bodies, not both static, layers
// that meet.
Vector<ColliderPair> Oracle(const PhysicsWorld& world, const CollisionFilter& filter)
{
    Vector<ColliderId> ids;
    for (u32 slot = 0; slot < world.Stats().collider_slots; ++slot)
    {
        if (world.ColliderAtSlot(slot).IsValid())
        {
            ids.Push(world.ColliderAtSlot(slot));
        }
    }
    Vector<ColliderPair> out;
    for (usize i = 0; i < ids.Size(); ++i)
    {
        for (usize j = i + 1; j < ids.Size(); ++j)
        {
            const ColliderRecord* a = world.FindCollider(ids[i]);
            const ColliderRecord* b = world.FindCollider(ids[j]);
            if (a->owner == b->owner || !filter.Meets(a->value.layer, b->value.layer))
            {
                continue;
            }
            const Body* ba = world.Find(a->owner);
            const Body* bb = world.Find(b->owner);
            if (ba->settings.type == BodyType::Static && bb->settings.type == BodyType::Static)
            {
                continue;
            }
            if (Overlaps(BoxOf(world, ids[i]), BoxOf(world, ids[j])))
            {
                out.Push(ColliderPair{ids[i], ids[j], false});
            }
        }
    }
    return out;
}

bool SamePairs(const Vector<ColliderPair>& a, const Vector<ColliderPair>& b)
{
    if (a.Size() != b.Size())
    {
        return false;
    }
    for (usize i = 0; i < a.Size(); ++i)
    {
        if (!(a[i].a == b[i].a) || !(a[i].b == b[i].b) || a[i].trigger != b[i].trigger)
        {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("BroadPhase: the pair list equals a brute-force search on random scenes, also after bodies move",
          "[physics][broad]")
{
    for (u64 scene = 0; scene < 20; ++scene)
    {
        Rng rng(scene + 1);
        PhysicsWorld world;
        BroadPhaseConfig config;
        config.pair_margin = 0.0f;  // with a margin of 0 the pairs are exactly the overlaps
        config.filter.SetMeets(1, 2, false);
        BroadPhase bp(config);
        Vector<BodyId> bodies;
        for (int i = 0; i < 1000; ++i)
        {
            const BodyType type = rng.Next() % 10 < 3 ? BodyType::Static : BodyType::Dynamic;
            bodies.Push(Add(world, type, Vec3(rng.Range(0, 30), rng.Range(0, 30), rng.Range(0, 30)),
                            ShapeCollider(rng, static_cast<u8>(rng.Next() % 4))));
        }
        for (int round = 0; round < 4; ++round)
        {
            bp.Update(world);
            const Vector<ColliderPair> expected = Oracle(world, config.filter);
            REQUIRE(SamePairs(bp.Pairs(), expected));
            CHECK(bp.Stats().pending_static == 0);
            // Move 30 percent of the dynamic bodies, teleport a few static ones, change a few types.
            for (usize i = 0; i < bodies.Size(); ++i)
            {
                const u32 roll = rng.Next() % 100;
                const Body* body = world.Find(bodies[i]);
                if (roll < 30 && body->settings.type == BodyType::Dynamic)
                {
                    world.SetTransform(bodies[i],
                                       body->position + Vec3(rng.Range(-1, 1), rng.Range(-1, 1), rng.Range(-1, 1)),
                                       Quaternion::Identity());
                }
                else if (roll < 33)
                {
                    world.SetTransform(bodies[i], Vec3(rng.Range(0, 30), rng.Range(0, 30), rng.Range(0, 30)),
                                       Quaternion::Identity());
                }
                else if (roll < 35)
                {
                    RigidBody settings = body->settings;
                    settings.type = settings.type == BodyType::Static ? BodyType::Dynamic : BodyType::Static;
                    world.SetSettings(bodies[i], settings);
                }
            }
        }
    }
}

TEST_CASE("BroadPhase: destroyed bodies leave the pairs and their slots are reused", "[physics][broad]")
{
    PhysicsWorld world;
    BroadPhase bp;
    Collider ball;
    ball.shape = Shape::MakeSphere(0.5f);
    const BodyId a = Add(world, BodyType::Dynamic, Vec3(0, 0, 0), ball);
    const BodyId b = Add(world, BodyType::Dynamic, Vec3(0.5f, 0, 0), ball);
    bp.Update(world);
    REQUIRE(bp.Pairs().Size() == 1);
    CHECK(bp.Stats().begun == 1);
    world.DestroyBody(b);
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 0);
    CHECK(bp.Stats().ended == 1);
    CHECK(bp.Stats().removed == 1);
    // A new body takes the slot with a new generation and pairs again with the first.
    const BodyId c = Add(world, BodyType::Dynamic, Vec3(0.4f, 0, 0), ball);
    bp.Update(world);
    REQUIRE(bp.Pairs().Size() == 1);
    CHECK(world.FindCollider(bp.Pairs()[0].a) != nullptr);
    CHECK(world.FindCollider(bp.Pairs()[0].b) != nullptr);
    CHECK(world.Find(a) != nullptr);
    CHECK(world.Find(c) != nullptr);
}

TEST_CASE("BroadPhase: a pair at the edge of contact does not begin and end every step", "[physics][broad]")
{
    PhysicsWorld world;
    BroadPhase bp;  // the default margin of 0.1
    Collider box;
    box.shape = Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
    Add(world, BodyType::Static, Vec3(0, 0, 0), box);
    const BodyId mover = Add(world, BodyType::Dynamic, Vec3(0.98f, 0, 0), box);
    u32 begun = 0;
    u32 ended = 0;
    for (int step = 0; step < 120; ++step)
    {
        // The two boxes touch for half of the steps and are 0.02 apart in the others.
        world.SetTransform(mover, Vec3(step % 2 == 0 ? 0.98f : 1.02f, 0, 0), Quaternion::Identity());
        bp.Update(world);
        begun += bp.Stats().begun;
        ended += bp.Stats().ended;
    }
    CHECK(begun == 1);
    CHECK(ended == 0);
    CHECK(bp.Pairs().Size() == 1);
    // Far away it does end.
    world.SetTransform(mover, Vec3(5, 0, 0), Quaternion::Identity());
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 0);
}

TEST_CASE("BroadPhase: layers, body types and triggers decide what reaches the work list", "[physics][broad]")
{
    PhysicsWorld world;
    BroadPhaseConfig config;
    config.filter.SetMeets(3, 4, false);
    BroadPhase bp(config);
    Collider c;
    c.shape = Shape::MakeBox(Vec3(1, 1, 1));
    // Two static and a kinematic body all overlapping at the origin: no static-static, no kinematic-static pair.
    Add(world, BodyType::Static, Vec3(0, 0, 0), c);
    Add(world, BodyType::Static, Vec3(0.2f, 0, 0), c);
    Add(world, BodyType::Kinematic, Vec3(0.1f, 0, 0), c);
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 0);
    // A dynamic body pairs with all three; the ones on layers that do not meet are left out.
    Collider layer3 = c;
    layer3.layer = 3;
    Collider layer4 = c;
    layer4.layer = 4;
    Add(world, BodyType::Dynamic, Vec3(0, 0, 0), c);
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 3);
    Add(world, BodyType::Static, Vec3(0, 0, 0.1f), layer3);
    Add(world, BodyType::Dynamic, Vec3(0, 0, 0.1f), layer4);
    bp.Update(world);
    for (usize i = 0; i < bp.Pairs().Size(); ++i)
    {
        const u8 la = world.FindCollider(bp.Pairs()[i].a)->value.layer;
        const u8 lb = world.FindCollider(bp.Pairs()[i].b)->value.layer;
        CHECK(!((la == 3 && lb == 4) || (la == 4 && lb == 3)));
    }
    // A trigger is flagged, and a kinematic body entering a static trigger is listed.
    Collider trigger = c;
    trigger.is_trigger = true;
    Add(world, BodyType::Static, Vec3(10, 0, 0), trigger);
    Add(world, BodyType::Kinematic, Vec3(10, 0, 0), c);
    bp.Update(world);
    u32 flagged = 0;
    for (usize i = 0; i < bp.Pairs().Size(); ++i)
    {
        flagged += bp.Pairs()[i].trigger ? 1u : 0u;
    }
    CHECK(flagged == 1);
}

TEST_CASE("BroadPhase: a moved static body and a changed type are followed", "[physics][broad]")
{
    PhysicsWorld world;
    BroadPhase bp;
    Collider c;
    c.shape = Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
    const BodyId wall = Add(world, BodyType::Static, Vec3(0, 0, 0), c);
    Add(world, BodyType::Dynamic, Vec3(0.5f, 0, 0), c);
    bp.Update(world);
    REQUIRE(bp.Pairs().Size() == 1);
    world.SetTransform(wall, Vec3(20, 0, 0), Quaternion::Identity());
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 0);
    CHECK(bp.Stats().pending_static == 0);
    world.SetTransform(wall, Vec3(0, 0.5f, 0), Quaternion::Identity());
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 1);
    RigidBody settings = world.Find(wall)->settings;
    settings.type = BodyType::Dynamic;
    world.SetSettings(wall, settings);
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 1);
    settings.type = BodyType::Static;
    world.SetSettings(wall, settings);
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 1);
}

TEST_CASE("BroadPhase: a continuous body's box covers its motion over one step", "[physics][broad]")
{
    PhysicsWorld world;
    BroadPhase bp;
    Collider ball;
    ball.shape = Shape::MakeSphere(0.1f);
    Collider wall;
    wall.shape = Shape::MakeBox(Vec3(0.05f, 2.0f, 2.0f));
    Add(world, BodyType::Static, Vec3(5, 0, 0), wall);
    BodyDesc desc;
    desc.settings.flags = kBodyContinuous;
    desc.velocity.linear = Vec3(300, 0, 0);  // 5 m in one step
    const BodyId fast = world.CreateBody(desc);
    REQUIRE(world.AddCollider(fast, ball) == BodyStatus::Ok);
    BodyDesc slow_desc;
    slow_desc.position = Vec3(0, 0, 1);
    slow_desc.velocity.linear = Vec3(300, 0, 0);
    const BodyId slow = world.CreateBody(slow_desc);
    REQUIRE(world.AddCollider(slow, ball) == BodyStatus::Ok);
    bp.Update(world);
    // The continuous body's box reaches the wall; the other body, at the same speed, would tunnel through it unseen.
    REQUIRE(bp.Pairs().Size() == 1);
    CHECK(world.FindCollider(bp.Pairs()[0].b)->owner == fast);  // the wall's collider has the smaller key
}

TEST_CASE("BroadPhase: two broad phases fed the same calls give the same lists", "[physics][broad]")
{
    PhysicsWorld wa;
    PhysicsWorld wb;
    BroadPhase pa;
    BroadPhase pb;
    Rng ra(7);
    Rng rb(7);
    for (int i = 0; i < 300; ++i)
    {
        const Vec3 pa_pos(ra.Range(0, 10), ra.Range(0, 10), ra.Range(0, 10));
        Add(wa, i % 4 == 0 ? BodyType::Static : BodyType::Dynamic, pa_pos, ShapeCollider(ra, 0));
        const Vec3 pb_pos(rb.Range(0, 10), rb.Range(0, 10), rb.Range(0, 10));
        Add(wb, i % 4 == 0 ? BodyType::Static : BodyType::Dynamic, pb_pos, ShapeCollider(rb, 0));
    }
    pa.Update(wa);
    pb.Update(wb);
    CHECK(SamePairs(pa.Pairs(), pb.Pairs()));
    CHECK(SamePairs(pa.Begun(), pb.Begun()));
    CHECK(pa.Pairs().Size() > 100);
}

TEST_CASE("BroadPhase: the static tree is rebuilt before the pairs are updated", "[physics][broad]")
{
    // The stat the test relies on: a static item added to a set waits outside the tree until `RebuildStatic`.
    spatial::SpatialSet bare;
    bare.AddStatic(Aabb{Vec3(0, 0, 0), Vec3(1, 1, 1)}, 1, 0);
    CHECK(bare.Stats().pending_static == 1);
    bare.RebuildStatic();
    CHECK(bare.Stats().pending_static == 0);
    // The broad phase leaves none pending after any update: adding, moving and removing statics.
    PhysicsWorld world;
    BroadPhase bp;
    Collider c;
    c.shape = Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f));
    Vector<BodyId> walls;
    for (int i = 0; i < 50; ++i)
    {
        walls.Push(Add(world, BodyType::Static, Vec3(static_cast<f32>(i) * 2.0f, 0, 0), c));
    }
    bp.Update(world);
    CHECK(bp.Stats().pending_static == 0);
    const u64 rebuilds = bp.Stats().static_rebuilds;
    world.SetTransform(walls[3], Vec3(0, 9, 0), Quaternion::Identity());
    world.DestroyBody(walls[4]);
    Add(world, BodyType::Static, Vec3(0, 20, 0), c);
    bp.Update(world);
    CHECK(bp.Stats().pending_static == 0);
    CHECK(bp.Stats().static_rebuilds == rebuilds + 1);
    // An update in which nothing changed does not rebuild.
    bp.Update(world);
    CHECK(bp.Stats().static_rebuilds == rebuilds + 1);
    CHECK(bp.Stats().added == 0);
    CHECK(bp.Stats().moved == 0);
}

TEST_CASE("BroadPhase: a changed layer keeps the pairs that still meet and drops the ones that no longer do",
          "[physics][broad]")
{
    PhysicsWorld world;
    BroadPhaseConfig config;
    config.filter.SetMeets(2, 3, false);
    BroadPhase bp(config);
    Collider c;
    c.shape = Shape::MakeBox(Vec3(1, 1, 1));
    const BodyId a = Add(world, BodyType::Dynamic, Vec3(0, 0, 0), c);
    const BodyId b = Add(world, BodyType::Dynamic, Vec3(0.5f, 0, 0), c);
    bp.Update(world);
    REQUIRE(bp.Pairs().Size() == 1);
    // Layer 0 to 1: both still meet; the pair must survive the removal and re-adding of the collider.
    Collider moved = c;
    moved.layer = 1;
    REQUIRE(world.SetCollider(a, moved) == BodyStatus::Ok);
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 1);
    CHECK(bp.Stats().ended == 0);
    // Layers 2 and 3 do not meet: move this one to 3, then the partner to 2; the pair must end.
    moved.layer = 3;
    REQUIRE(world.SetCollider(a, moved) == BodyStatus::Ok);
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 1);
    Collider partner = c;
    partner.layer = 2;
    REQUIRE(world.SetCollider(b, partner) == BodyStatus::Ok);
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 0);
}
