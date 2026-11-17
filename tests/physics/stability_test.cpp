// The tuning scenes of the contact solver (Phase 25 Day 397): stacks, a pyramid, a column of spheres, dominoes, a heavy
// box on light ones and a stack of hulls hold for an hour of simulated time (3600 steps).
#include <catch2/catch_test_macros.hpp>

#include "lang/scalar.h"
#include "physics/world.h"
#include "test_hulls.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

namespace
{

BodyId Floor(PhysicsWorld& world)
{
    BodyDesc desc;
    desc.settings.type = BodyType::Static;
    desc.position = Vec3(0, -0.5f, 0);
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeBox(Vec3(50, 0.5f, 50));
    c.material.friction = 0.6f;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

BodyId Add(PhysicsWorld& world, const Shape& shape, const Vec3& position, f32 mass = 0.0f, f32 friction = 0.6f)
{
    BodyDesc desc;
    desc.position = position;
    desc.settings.mass = mass;
    const BodyId id = world.CreateBody(desc);
    REQUIRE(id.IsValid());
    Collider c;
    c.shape = shape;
    c.material.friction = friction;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

struct Settle
{
    f32 max_drift = 0.0f;  // the largest sideways distance any body ended from where it started
    f32 max_sag = 0.0f;    // the largest distance any body ended below where it started
    f32 max_speed = 0.0f;  // the largest speed at the end
    f32 max_depth = 0.0f;  // the deepest penetration seen
};

Settle Run(PhysicsWorld& world, const Vector<BodyId>& bodies, u32 steps)
{
    Vector<Vec3> start;
    for (usize i = 0; i < bodies.Size(); ++i)
    {
        start.Push(world.Find(bodies[i])->position);
    }
    Settle s;
    for (u32 i = 0; i < steps; ++i)
    {
        world.Step();
        s.max_depth = FMax(s.max_depth, world.GetSolverStats().max_penetration);
    }
    for (usize i = 0; i < bodies.Size(); ++i)
    {
        const Body* b = world.Find(bodies[i]);
        const Vec3 moved = b->position - start[i];
        s.max_drift = FMax(s.max_drift, Sqrt(moved.x * moved.x + moved.z * moved.z));
        s.max_sag = FMax(s.max_sag, -moved.y);
        s.max_speed = FMax(s.max_speed, b->linear_velocity.Length());
    }
    return s;
}

}  // namespace

TEST_CASE("Stability: a stack of 10 boxes holds for 3600 steps", "[physics][solver][stability]")
{
    PhysicsWorld world;
    Floor(world);
    Vector<BodyId> boxes;
    for (u32 i = 0; i < 10; ++i)
    {
        boxes.Push(Add(world, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), Vec3(0, 0.5f + static_cast<f32>(i), 0)));
    }
    const Settle s = Run(world, boxes, 3600);
    INFO("drift " << s.max_drift << " sag " << s.max_sag << " speed " << s.max_speed << " depth " << s.max_depth);
    CHECK(s.max_drift < 0.01f);
    CHECK(s.max_sag < 0.05f);
    CHECK(s.max_speed < 0.01f);
}

TEST_CASE("Stability: a pyramid of 5 rows holds", "[physics][solver][stability]")
{
    PhysicsWorld world;
    Floor(world);
    Vector<BodyId> boxes;
    for (u32 row = 0; row < 5; ++row)
    {
        for (u32 i = 0; i < 5 - row; ++i)
        {
            const f32 x = (static_cast<f32>(i) - 0.5f * static_cast<f32>(4 - row)) * 1.0f;
            boxes.Push(Add(world, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), Vec3(x, 0.5f + static_cast<f32>(row), 0)));
        }
    }
    const Settle s = Run(world, boxes, 3600);
    INFO("drift " << s.max_drift << " sag " << s.max_sag << " speed " << s.max_speed << " depth " << s.max_depth);
    CHECK(s.max_drift < 0.01f);
    CHECK(s.max_sag < 0.02f);
    CHECK(s.max_speed < 0.01f);
}

TEST_CASE("Stability: a column of 20 spheres holds", "[physics][solver][stability]")
{
    PhysicsWorld world;
    Floor(world);
    Vector<BodyId> balls;
    for (u32 i = 0; i < 20; ++i)
    {
        balls.Push(Add(world, Shape::MakeSphere(0.5f), Vec3(0, 0.5f + static_cast<f32>(i), 0)));
    }
    const Settle s = Run(world, balls, 3600);
    INFO("drift " << s.max_drift << " sag " << s.max_sag << " speed " << s.max_speed << " depth " << s.max_depth);
    CHECK(s.max_drift < 0.01f);
    CHECK(s.max_sag < 0.15f);
    CHECK(s.max_speed < 0.01f);
}

TEST_CASE("Stability: dominoes fall in order", "[physics][solver][stability]")
{
    PhysicsWorld world;
    Floor(world);
    Vector<BodyId> dominoes;
    for (u32 i = 0; i < 10; ++i)
    {
        dominoes.Push(Add(world, Shape::MakeBox(Vec3(0.05f, 0.5f, 0.25f)), Vec3(0.4f * static_cast<f32>(i), 0.5f, 0)));
    }
    world.Find(dominoes[0])->angular_velocity = Vec3(0, 0, -1.5f);  // tips the first one toward +x
    Vector<i32> fell;
    for (usize i = 0; i < dominoes.Size(); ++i)
    {
        fell.Push(-1);
    }
    for (u32 step = 0; step < 900; ++step)
    {
        world.Step();
        for (usize i = 0; i < dominoes.Size(); ++i)
        {
            const Body* b = world.Find(dominoes[i]);
            if (fell[i] < 0 && b->position.y < 0.35f)
            {
                fell[i] = static_cast<i32>(step);
            }
        }
    }
    for (usize i = 0; i < dominoes.Size(); ++i)
    {
        INFO("domino " << i << " fell at step " << fell[i]);
        CHECK(fell[i] >= 0);
        if (i > 0)
        {
            CHECK(fell[i] >= fell[i - 1]);
        }
    }
}

TEST_CASE("Stability: a box ten times heavier than the two boxes under it holds", "[physics][solver][stability]")
{
    PhysicsWorld world;
    Floor(world);
    Vector<BodyId> boxes;
    boxes.Push(Add(world, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), Vec3(0, 0.5f, 0), 1.0f));
    boxes.Push(Add(world, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), Vec3(0, 1.5f, 0), 1.0f));
    boxes.Push(Add(world, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), Vec3(0, 2.5f, 0), 10.0f));
    const Settle s = Run(world, boxes, 3600);
    INFO("drift " << s.max_drift << " sag " << s.max_sag << " speed " << s.max_speed << " depth " << s.max_depth);
    CHECK(s.max_drift < 0.01f);
    CHECK(s.max_sag < 0.03f);
    CHECK(s.max_speed < 0.01f);
}

TEST_CASE("Stability: a stack of hulls holds", "[physics][solver][stability]")
{
    PhysicsWorld world;
    Floor(world);
    const ConvexHullConstPtr hull = testing::BoxHull(Vec3(0.5f, 0.5f, 0.5f));
    const u32 id = world.Hulls().Add(hull);
    Vector<BodyId> bodies;
    for (u32 i = 0; i < 5; ++i)
    {
        bodies.Push(Add(world, Shape::MakeHull(id, hull->Extent()), Vec3(0, 0.5f + static_cast<f32>(i), 0)));
    }
    const Settle s = Run(world, bodies, 3600);
    INFO("drift " << s.max_drift << " sag " << s.max_sag << " speed " << s.max_speed << " depth " << s.max_depth);
    CHECK(s.max_drift < 0.01f);
    CHECK(s.max_sag < 0.02f);
    CHECK(s.max_speed < 0.01f);
}

TEST_CASE("Stability: a body squeezed between two planes does not explode", "[physics][solver][stability]")
{
    PhysicsWorld world;
    for (u32 i = 0; i < 2; ++i)
    {
        BodyDesc desc;
        desc.settings.type = BodyType::Static;
        desc.position = Vec3(i == 0 ? -0.9f : 0.9f, 0, 0);
        const BodyId id = world.CreateBody(desc);
        Collider c;
        c.shape = Shape::MakeBox(Vec3(0.5f, 5, 5));
        REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    }
    // 0.8 wide between the planes' faces at +-0.4... the box is 1.0 wide: it cannot fit.
    const BodyId box = Add(world, Shape::MakeBox(Vec3(0.5f, 0.5f, 0.5f)), Vec3(0, 0, 0));
    world.Find(box)->settings.gravity_scale = 0.0f;
    f32 fastest = 0.0f;
    for (u32 i = 0; i < 600; ++i)
    {
        world.Step();
        fastest = FMax(fastest, world.Find(box)->linear_velocity.Length());
    }
    CHECK(fastest <= world.GetStepConfig().max_velocity * 1.0001f);
    CHECK(world.Find(box)->position.Length() < 5.0f);
}
