// Bodies, the broad phase, queries, contacts and joints far from the origin (double precision, Days 6 and 7): positions
// are `f64` and contacts and joints are measured from a nearby anchor, so a scene 10,000 km away moves, collides, rests
// and is saved as the same scene at the origin does.
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "bytes/byte_writer.h"
#include "lang/scalar.h"
#include "physics/character.h"
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

BodyId FarBox(PhysicsWorld& world, BodyType type, const Vec3& half, const Vec3d& at)
{
    BodyDesc desc;
    desc.settings.type = type;
    desc.position = at;
    const BodyId id = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeBox(half);
    c.material.friction = 0.6f;
    REQUIRE(world.AddCollider(id, c) == BodyStatus::Ok);
    return id;
}

// A floor, a stack of ten boxes, a pyramid, a plank bridge, a pendulum hung from the world and a bullet at a thin wall,
// everything placed relative to `base`.
void BuildFarScene(PhysicsWorld& world, const Vec3d& base, Vector<BodyId>* bodies)
{
    FarBox(world, BodyType::Static, Vec3(60.0f, 0.5f, 60.0f), base + Vec3d(0, -0.5, 0));
    for (u32 i = 0; i < 10; ++i)
    {
        bodies->Push(FarBox(world, BodyType::Dynamic, Vec3(0.5f, 0.5f, 0.5f),
                            base + Vec3d(0.01 * static_cast<f64>(i % 3), 0.5 + static_cast<f64>(i), 0)));
    }
    for (u32 row = 0; row < 4; ++row)
    {
        for (u32 i = 0; i < 4 - row; ++i)
        {
            bodies->Push(FarBox(world, BodyType::Dynamic, Vec3(0.5f, 0.5f, 0.5f),
                                base + Vec3d(10.0 + 0.5 * static_cast<f64>(row) + static_cast<f64>(i),
                                             0.5 + static_cast<f64>(row), 0)));
        }
    }
    const Vec3d deck = base + Vec3d(-20.0, 5.0, 0.0);
    BodyId previous = FarBox(world, BodyType::Static, Vec3(0.5f, 0.5f, 1.0f), deck + Vec3d(-0.5, 0, 0));
    for (u32 i = 0; i < 8; ++i)
    {
        const BodyId plank = FarBox(world, BodyType::Dynamic, Vec3(0.45f, 0.08f, 0.9f),
                                    deck + Vec3d(0.5 + static_cast<f64>(i), 0, 0));
        REQUIRE(world
                    .CreateJoint(
                        world.MakeHingeJoint(previous, plank, deck + Vec3d(static_cast<f64>(i), 0, 0), Vec3(0, 0, 1)))
                    .IsValid());
        bodies->Push(plank);
        previous = plank;
    }
    const BodyId right = FarBox(world, BodyType::Static, Vec3(0.5f, 0.5f, 1.0f), deck + Vec3d(8.5, 0, 0));
    REQUIRE(world.CreateJoint(world.MakeHingeJoint(previous, right, deck + Vec3d(8.0, 0, 0), Vec3(0, 0, 1))).IsValid());
    // A pendulum hung from the world.
    const Vec3d pivot = base + Vec3d(0.0, 8.0, 20.0);
    const BodyId bob = FarBox(world, BodyType::Dynamic, Vec3(0.2f, 0.2f, 0.2f), pivot + Vec3d(3.0, 0, 0));
    REQUIRE(world.CreateJoint(world.MakeBallJoint(bob, BodyId(), pivot)).IsValid());
    bodies->Push(bob);
    // A bullet and a thin wall.
    FarBox(world, BodyType::Static, Vec3(0.05f, 3.0f, 3.0f), base + Vec3d(30.0, 3.0, -20.0));
    BodyDesc desc;
    desc.settings.type = BodyType::Dynamic;
    desc.position = base + Vec3d(10.0, 2.0, -20.0);
    const BodyId bullet = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeSphere(0.05f);
    REQUIRE(world.AddCollider(bullet, c) == BodyStatus::Ok);
    Velocity v;
    v.linear = Vec3(250, 0, 0);
    REQUIRE(world.SetVelocity(bullet, v) == BodyStatus::Ok);
    bodies->Push(bullet);
}

}  // namespace

TEST_CASE("Far world: stacks, a bridge, a pendulum and a bullet 10,000 km away behave like the ones at the origin",
          "[physics][far-world]")
{
    PhysicsWorld here;
    PhysicsWorld far;
    Vector<BodyId> a;
    Vector<BodyId> b;
    BuildFarScene(here, Vec3d(0, 0, 0), &a);
    BuildFarScene(far, kFar, &b);
    for (u32 step = 0; step < 600; ++step)
    {
        here.Step();
        far.Step();
    }
    REQUIRE(a.Size() == b.Size());
    f64 worst = 0.0;
    for (usize i = 0; i < a.Size(); ++i)
    {
        const Vec3d p = here.Find(a[i])->position;
        const Vec3d q = far.Find(b[i])->position - kFar;
        worst = Max(worst, Max(Abs(p.x - q.x), Max(Abs(p.y - q.y), Abs(p.z - q.z))));
    }
    INFO("worst difference " << worst);
    CHECK(worst < 1e-2);
    // The scene itself is sane: the stack stands, the bullet stopped at the wall.
    CHECK(far.Find(b[9])->position.y - kFar.y > 8.0);
    CHECK(far.Find(b[b.Size() - 1])->position.x - kFar.x < 30.0);
    CHECK(far.GetSolverStats().max_penetration < 0.05f);
}

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

TEST_CASE("Far world: a character walks, steps up a ledge and stands 10,000 km away as at the origin",
          "[physics][far-world][character]")
{
    PhysicsWorld here;
    PhysicsWorld far;
    CharacterController walker_here;
    CharacterController walker_far;
    for (PhysicsWorld* world : {&here, &far})
    {
        const Vec3d base = world == &here ? Vec3d(0, 0, 0) : kFar;
        FarBox(*world, BodyType::Static, Vec3(60.0f, 0.5f, 60.0f), base + Vec3d(0, -0.5, 0));
        FarBox(*world, BodyType::Static, Vec3(1.0f, 0.1f, 2.0f), base + Vec3d(5.0, 0.1, 0.0));  // a ledge 0.2 m high
        CharacterController& controller = world == &here ? walker_here : walker_far;
        REQUIRE(controller.Create(world, CharacterConfig(), base + Vec3d(0, 0.95, 0)) == BodyStatus::Ok);
    }
    const f32 dt = 1.0f / 60.0f;
    for (u32 i = 0; i < 240; ++i)
    {
        walker_here.Move(&here, Vec3(2, 0, 0), dt);
        here.Step();
        walker_far.Move(&far, Vec3(2, 0, 0), dt);
        far.Step();
    }
    const Vec3d p = here.Find(walker_here.BodyHandle())->position;
    const Vec3d q = far.Find(walker_far.BodyHandle())->position - kFar;
    CHECK(walker_far.State().on_ground);
    CHECK(q.x > 6.0);  // it went over the ledge
    CHECK(Abs(p.x - q.x) < 1e-3);
    CHECK(Abs(p.y - q.y) < 1e-3);
    CHECK(Abs(p.z - q.z) < 1e-3);
}
