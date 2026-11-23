// Bodies, the broad phase, queries and snapshots far from the origin (double precision, Day 6): positions are `f64`, so
// a scene 10,000 km away moves, is found and is saved as exactly as the same scene at the origin. The narrow phase and
// the solver are still single precision in world coordinates (the next day), so nothing here rests on a surface.
#include <catch2/catch_test_macros.hpp>

#include "bytes/byte_writer.h"
#include "lang/scalar.h"
#include "physics/query.h"
#include "physics/world.h"

using namespace vse;
using namespace vse::physics;

namespace
{

// A whole number of metres, so the subtractions below are exact.
const Vec3d kFar(1.0e7, 2.0e6, -5.0e6);

BodyId FarBall(PhysicsWorld& world, const Vec3d& position, BodyType type, f32 radius)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.position = position;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeSphere(radius);
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

}  // namespace

TEST_CASE("Far world: a body in free fall 10,000 km away falls exactly like the one at the origin",
          "[physics][far-world]")
{
    PhysicsWorld here;
    PhysicsWorld far;
    const BodyId a = FarBall(here, Vec3d(0.0, 100.0, 0.0), BodyType::Dynamic, 0.5f);
    const BodyId b = FarBall(far, kFar + Vec3d(0.0, 100.0, 0.0), BodyType::Dynamic, 0.5f);
    Velocity v;
    v.linear = Vec3(3.0f, 1.0f, -2.0f);
    REQUIRE(here.SetVelocity(a, v) == BodyStatus::Ok);
    REQUIRE(far.SetVelocity(b, v) == BodyStatus::Ok);
    for (int i = 0; i < 600; ++i)
    {
        here.Step();
        far.Step();
    }
    const Vec3d moved = far.Find(b)->position - kFar;
    const Vec3d expected = here.Find(a)->position;
    // The velocities are the same bits; the positions differ only by the rounding of the sums at 1e7 m (2e-9 m).
    CHECK(far.Find(b)->linear_velocity.y == here.Find(a)->linear_velocity.y);
    CHECK(Abs(moved.x - expected.x) < 1e-6);
    CHECK(Abs(moved.y - expected.y) < 1e-6);
    CHECK(Abs(moved.z - expected.z) < 1e-6);
}

TEST_CASE("Far world: a body can be put anywhere inside a billion kilometres and not beyond", "[physics][far-world]")
{
    PhysicsWorld world;
    const BodyId id = FarBall(world, Vec3d(0.0, 0.0, 0.0), BodyType::Kinematic, 0.5f);
    CHECK(world.SetTransform(id, Vec3d(5.0e11, 0.0, 0.0), Quaternion::Identity()) == BodyStatus::Ok);
    CHECK(world.Find(id)->position.x == 5.0e11);
    CHECK(world.SetTransform(id, Vec3d(2.0e12, 0.0, 0.0), Quaternion::Identity()) != BodyStatus::Ok);
    CHECK(world.Find(id)->position.x == 5.0e11);  // refused: unchanged
}

TEST_CASE("Far world: the broad phase finds the pairs of a scene 10,000 km away", "[physics][far-world]")
{
    PhysicsWorld here;
    PhysicsWorld far;
    for (u32 i = 0; i < 6; ++i)
    {
        const Vec3d p(0.8 * static_cast<f64>(i), 0.0, 0.0);  // neighbours overlap, the next ones do not
        FarBall(here, p, BodyType::Dynamic, 0.5f);
        FarBall(far, kFar + p, BodyType::Dynamic, 0.5f);
    }
    here.Step();
    far.Step();
    CHECK(here.AllStats().broad_pairs == 5);
    CHECK(far.AllStats().broad_pairs == here.AllStats().broad_pairs);
}

TEST_CASE("Far world: queries 10,000 km away give the results of the same scene at the origin", "[physics][far-world]")
{
    PhysicsWorld here;
    PhysicsWorld far;
    const Vec3d centre(0.0, 0.0, 5.0);
    FarBall(here, centre, BodyType::Static, 1.0f);
    FarBall(far, kFar + centre, BodyType::Static, 1.0f);
    const PhysicsQueryFilter filter;

    PhysicsHit h0;
    PhysicsHit h1;
    REQUIRE(RayCast(here, Vec3d(0.0, 0.0, 0.0), Vec3(0, 0, 1), 100.0f, filter, &h0));
    REQUIRE(RayCast(far, kFar, Vec3(0, 0, 1), 100.0f, filter, &h1));
    CHECK(h1.distance == h0.distance);
    CHECK(h1.normal.z == h0.normal.z);
    CHECK(h1.point.x == kFar.x + h0.point.x);
    CHECK(Abs(h1.point.z - (kFar.z + h0.point.z)) < 1e-9);

    // A millimetre inside the sphere's edge hits, a millimetre outside misses, at 10,000 km as at the origin.
    CHECK(RayCast(far, kFar + Vec3d(0.0, 0.999, 0.0), Vec3(0, 0, 1), 100.0f, filter, &h1));
    CHECK_FALSE(RayCast(far, kFar + Vec3d(0.0, 1.001, 0.0), Vec3(0, 0, 1), 100.0f, filter, &h1));

    const Shape probe = Shape::MakeSphere(0.5f);
    Vector<ColliderId> found;
    CHECK(Overlap(far, probe, kFar + Vec3d(0.0, 0.0, 3.6), Quaternion::Identity(), filter, &found) == 1);
    CHECK(Overlap(far, probe, kFar + Vec3d(0.0, 0.0, 3.4), Quaternion::Identity(), filter, &found) == 0);
    CHECK(Overlap(here, probe, Vec3d(0.0, 0.0, 3.6), Quaternion::Identity(), filter, &found) == 1);
    CHECK(Overlap(here, probe, Vec3d(0.0, 0.0, 3.4), Quaternion::Identity(), filter, &found) == 0);

    PhysicsHit cast0;
    PhysicsHit cast1;
    REQUIRE(ShapeCast(here, probe, Vec3d(0.0, 0.0, 0.0), Quaternion::Identity(), Vec3(0, 0, 1), 20.0f, filter, &cast0));
    REQUIRE(ShapeCast(far, probe, kFar, Quaternion::Identity(), Vec3(0, 0, 1), 20.0f, filter, &cast1));
    CHECK(Abs(cast1.distance - cast0.distance) < 1e-4f);
    CHECK(Abs(cast1.distance - 3.5f) < 1e-3f);

    PhysicsHit near0;
    PhysicsHit near1;
    REQUIRE(ClosestPoint(here, Vec3d(0.0, 0.0, 0.0), 10.0f, filter, &near0));
    REQUIRE(ClosestPoint(far, kFar, 10.0f, filter, &near1));
    CHECK(Abs(near1.distance - near0.distance) < 1e-4f);
    CHECK(Abs(near1.distance - 4.0f) < 1e-3f);
}

TEST_CASE("Far world: a snapshot keeps a position 10,000 km away to the bit", "[physics][far-world]")
{
    PhysicsWorld world;
    const BodyId id = FarBall(world, kFar + Vec3d(0.001, 0.0, 0.0), BodyType::Kinematic, 0.5f);
    bytes::ByteWriter writer;
    world.Snapshot(&writer);
    Vector<byte> bytes;
    for (usize i = 0; i < writer.Size(); ++i)
    {
        bytes.Push(writer.Bytes()[i]);
    }
    PhysicsWorld restored;
    REQUIRE(restored.Restore(ByteSpan(bytes.Data(), bytes.Size())) == SnapshotStatus::Ok);
    const Body* body = restored.Find(id);
    REQUIRE(body != nullptr);
    CHECK(body->position.x == kFar.x + 0.001);
    CHECK(body->position.y == kFar.y);
    CHECK(restored.StateHash() == world.StateHash());
}
