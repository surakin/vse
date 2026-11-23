// Continuous collision (Phase 25 Day 402): fast bodies do not pass through thin static geometry.
#include <catch2/catch_test_macros.hpp>

#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "physics/triangle_mesh.h"
#include "physics/world.h"

using namespace vse;
using namespace vse::physics;

namespace
{

BodyId CcdStatic(PhysicsWorld& world, const Vec3& position, const Vec3& half)
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

BodyId CcdShot(PhysicsWorld& world, const Shape& shape, const Vec3& position, const Vec3& velocity, bool flagged)
{
    BodyDesc desc;
    desc.position = position;
    desc.velocity.linear = velocity;
    desc.settings.gravity_scale = 0.0f;
    desc.settings.flags = flagged ? static_cast<u32>(kBodyContinuous) : 0u;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = shape;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

void CcdFast(PhysicsWorld& world, f32 threshold = 0.0f)
{
    StepConfig config = world.GetStepConfig();
    config.max_velocity = 2000.0f;
    config.ccd_threshold = threshold;
    REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
}

}  // namespace

TEST_CASE("Ccd: a bullet passes a 1 cm wall without the flag and stops at it with it", "[physics][ccd]")
{
    for (const bool flagged : {false, true})
    {
        PhysicsWorld world;
        CcdFast(world);
        CcdStatic(world, Vec3(10, 0, 0), Vec3(0.005f, 5, 5));
        const BodyId bullet = CcdShot(world, Shape::MakeSphere(0.05f), Vec3(0, 0, 0), Vec3(500, 0, 0), flagged);
        for (u32 i = 0; i < 30; ++i)
        {
            world.Step();
        }
        const f32 x = static_cast<f32>(world.Find(bullet)->position.x);
        if (flagged)
        {
            CHECK(x < 10.0f);  // never beyond the wall's face
            CHECK(x > 9.9f);
            CHECK(world.GetStepStats().ccd_hits > 0);
        }
        else
        {
            CHECK(x > 11.0f);  // tunnelled
            CHECK(world.GetStepStats().ccd_queries == 0);
        }
    }
}

TEST_CASE("Ccd: the travel threshold sweeps a body that has no flag", "[physics][ccd]")
{
    PhysicsWorld world;
    CcdFast(world, 0.5f);
    CcdStatic(world, Vec3(10, 0, 0), Vec3(0.005f, 5, 5));
    const BodyId bullet = CcdShot(world, Shape::MakeSphere(0.05f), Vec3(0, 0, 0), Vec3(500, 0, 0), false);
    for (u32 i = 0; i < 30; ++i)
    {
        world.Step();
    }
    CHECK(world.Find(bullet)->position.x < 10.0f);
    // A slow body is not swept.
    PhysicsWorld slow;
    CcdFast(slow, 0.5f);
    CcdStatic(slow, Vec3(10, 0, 0), Vec3(0.5f, 5, 5));
    CcdShot(slow, Shape::MakeSphere(0.5f), Vec3(0, 0, 0), Vec3(5, 0, 0), false);
    for (u32 i = 0; i < 30; ++i)
    {
        slow.Step();
    }
    CHECK(slow.GetStepStats().ccd_queries == 0);
}

TEST_CASE("Ccd: a fast box does not pass a thin static plane", "[physics][ccd]")
{
    PhysicsWorld world;
    CcdFast(world);
    CcdStatic(world, Vec3(0, 0, 0), Vec3(50, 0.005f, 50));
    const BodyId box = CcdShot(world, Shape::MakeBox(Vec3(0.2f, 0.2f, 0.2f)), Vec3(0, 20, 0), Vec3(0, -300, 0), true);
    for (u32 i = 0; i < 60; ++i)
    {
        world.Step();
    }
    CHECK(world.Find(box)->position.y > 0.0f);
}

TEST_CASE("Ccd: a car-sized capsule at speed stops at a mesh fence", "[physics][ccd]")
{
    PhysicsWorld world;
    CcdFast(world);
    Vector<Vec3> v;
    v.Push(Vec3(10, 0, -5));
    v.Push(Vec3(10, 4, -5));
    v.Push(Vec3(10, 4, 5));
    v.Push(Vec3(10, 0, 5));
    Vector<u32> idx;
    for (const u32 i : {0u, 1u, 2u, 0u, 2u, 3u})
    {
        idx.Push(i);
    }
    TriangleMeshSpec spec;
    spec.positions = Span<const Vec3>(v.Data(), v.Size());
    spec.indices = Span<const u32>(idx.Data(), idx.Size());
    Validation r = Validation::Ok;
    const TriangleMeshConstPtr mesh = TriangleMesh::Create(spec, &r);
    REQUIRE(r == Validation::Ok);
    BodyDesc fence;
    fence.settings.type = BodyType::Static;
    const BodyId fence_body = world.CreateBody(fence);
    Collider fc;
    fc.shape = Shape::MakeMesh(world.Meshes().Add(mesh), mesh->Extent());
    REQUIRE(world.AddCollider(fence_body, fc) == BodyStatus::Ok);
    const BodyId car = CcdShot(world, Shape::MakeCapsule(1.0f, 0.5f), Vec3(0, 2, 0), Vec3(300, 0, 0), true);
    for (u32 i = 0; i < 40; ++i)
    {
        world.Step();
    }
    CHECK(world.Find(car)->position.x < 10.0f);
    CHECK(world.GetStepStats().ccd_hits > 0);
}

TEST_CASE("Ccd: a ball rolling over the seam of two static boxes is not disturbed", "[physics][ccd]")
{
    f32 end[2] = {0, 0};
    for (u32 run = 0; run < 2; ++run)
    {
        PhysicsWorld world;
        CcdStatic(world, Vec3(-5, -0.5f, 0), Vec3(5, 0.5f, 5));
        CcdStatic(world, Vec3(5, -0.5f, 0), Vec3(5, 0.5f, 5));
        BodyDesc desc;
        desc.position = Vec3(-8, 0.5f, 0);
        desc.velocity.linear = Vec3(5, 0, 0);
        desc.settings.flags = run == 1 ? static_cast<u32>(kBodyContinuous) : 0u;
        const BodyId ball = world.CreateBody(desc);
        Collider c;
        c.shape = Shape::MakeSphere(0.5f);
        REQUIRE(world.AddCollider(ball, c) == BodyStatus::Ok);
        for (u32 i = 0; i < 180; ++i)
        {
            world.Step();
        }
        end[run] = static_cast<f32>(world.Find(ball)->position.x);
        CHECK(world.Find(ball)->position.y > 0.4f);
    }
    CHECK(Abs(end[0] - end[1]) < 0.05f);
    CHECK(end[1] > 2.0f);  // it slides and stops by friction, the same with and without the flag
}

TEST_CASE("Ccd: the state hash does not depend on the thread count", "[physics][ccd]")
{
    u64 hashes[2] = {0, 0};
    for (u32 run = 0; run < 2; ++run)
    {
        PhysicsWorld world;
        CcdFast(world);
        StepConfig config = world.GetStepConfig();
        config.threads = run == 0 ? 1 : 4;
        REQUIRE(world.SetStepConfig(config) == BodyStatus::Ok);
        jobs::Scheduler::Config sc;
        sc.participants = 4;
        sc.pin = false;
        jobs::Scheduler scheduler(sc);
        world.SetScheduler(&scheduler);
        CcdStatic(world, Vec3(10, 0, 0), Vec3(0.005f, 5, 5));
        for (u32 k = 0; k < 6; ++k)
        {
            CcdShot(world, Shape::MakeSphere(0.05f), Vec3(0, static_cast<f32>(k), 0), Vec3(500, 0, 0), true);
        }
        for (u32 i = 0; i < 30; ++i)
        {
            world.Step();
        }
        hashes[run] = world.StateHash();
    }
    CHECK(hashes[0] == hashes[1]);
}
