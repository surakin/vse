// Compound shapes, group and mask filtering and triggers (Phase 25 Day 395).
#include <catch2/catch_test_macros.hpp>

#include "lang/scalar.h"
#include "physics/broad_phase.h"
#include "physics/collide.h"
#include "physics/compound.h"
#include "physics/state_hash.h"
#include "physics/world.h"
#include "test_hulls.h"
#include "test_values.h"

using namespace vse;
using namespace vse::physics;
using vse::physics::testing::Rng;

namespace
{

bool CNear(f32 a, f32 b, f32 tolerance)
{
    return Abs(a - b) <= tolerance * FMax(1.0f, Abs(b));
}

CompoundChild Child(const Shape& shape, const Vec3& offset, f32 density = 100.0f)
{
    CompoundChild c;
    c.shape = shape;
    c.offset = offset;
    c.material.density = density;
    return c;
}

// The tensor of principal values and axes, as a matrix element.
f32 Tensor(const MassProperties& m, u32 row, u32 col)
{
    const Mat3 r = m.principal_rotation.ToMat3();
    const f32 v[3] = {m.inertia.x, m.inertia.y, m.inertia.z};
    f32 sum = 0.0f;
    for (u32 k = 0; k < 3; ++k)
    {
        sum += r.col[k][row] * v[k] * r.col[k][col];
    }
    return sum;
}

CompoundShapeConstPtr Table(const HullLibrary* hulls = nullptr)
{
    Vector<CompoundChild> children;
    children.Push(Child(Shape::MakeBox(Vec3(0.6f, 0.05f, 0.4f)), Vec3(0, 0.8f, 0)));
    for (u32 i = 0; i < 4; ++i)
    {
        const f32 x = (i & 1) ? 0.5f : -0.5f;
        const f32 z = (i & 2) ? 0.3f : -0.3f;
        children.Push(Child(Shape::MakeCapsule(0.3f, 0.05f), Vec3(x, 0.4f, z)));
    }
    Validation r = Validation::Ok;
    const CompoundShapeConstPtr t = CompoundShape::Create(Span<const CompoundChild>(children.Data(), children.Size()),
                                                          hulls, &r);
    REQUIRE(r == Validation::Ok);
    return t;
}

}  // namespace

TEST_CASE("CompoundShape: refusals", "[physics][compound]")
{
    Validation r = Validation::Ok;
    CHECK(!CompoundShape::Create(Span<const CompoundChild>(), nullptr, &r));
    CHECK(r == Validation::OutOfRange);
    Vector<CompoundChild> many;
    for (u32 i = 0; i < 65; ++i)
    {
        many.Push(Child(Shape::MakeSphere(0.1f), Vec3(static_cast<f32>(i), 0, 0)));
    }
    CHECK(!CompoundShape::Create(Span<const CompoundChild>(many.Data(), many.Size()), nullptr, &r));
    CHECK(r == Validation::OutOfRange);
    CHECK(CompoundShape::Create(Span<const CompoundChild>(many.Data(), 64), nullptr, &r));
    CompoundChild bad = Child(Shape::MakeMesh(1, Vec3(1, 1, 1)), Vec3(0, 0, 0));
    CHECK(!CompoundShape::Create(Span<const CompoundChild>(&bad, 1), nullptr, &r));
    bad = Child(Shape::MakeHull(3, Vec3(1, 1, 1)), Vec3(0, 0, 0));
    CHECK(!CompoundShape::Create(Span<const CompoundChild>(&bad, 1), nullptr, &r));  // no hull library
    bad = Child(Shape::MakeSphere(-1.0f), Vec3(0, 0, 0));
    CHECK(!CompoundShape::Create(Span<const CompoundChild>(&bad, 1), nullptr, &r));
    CHECK(r == Validation::NotPositive);
    bad = Child(Shape::MakeSphere(0.5f), Vec3(0, 0, 0));
    bad.rotation = Quaternion(0, 0, 0, 3);
    CHECK(!CompoundShape::Create(Span<const CompoundChild>(&bad, 1), nullptr, &r));
    CHECK(Shape::MakeCompound(0, Vec3(1, 1, 1)).Validate() == Validation::OutOfRange);
}

TEST_CASE("CompoundShape: the mass, the centre and the inertia of a table, and of a split box", "[physics][compound]")
{
    const CompoundShapeConstPtr table = Table();
    // By hand: the top is a box of density 100, the legs capsules of density 100.
    const MassProperties top = MassFromDensity(Shape::MakeBox(Vec3(0.6f, 0.05f, 0.4f)), 100.0f);
    const MassProperties leg = MassFromDensity(Shape::MakeCapsule(0.3f, 0.05f), 100.0f);
    const f32 mass = top.mass + 4.0f * leg.mass;
    CHECK(CNear(table->Mass().mass, mass, 1e-5f));
    const f32 cy = (top.mass * 0.8f + 4.0f * leg.mass * 0.4f) / mass;
    CHECK(CNear(table->Mass().center.y, cy, 1e-5f));
    CHECK(Abs(table->Mass().center.x) < 1e-5f);
    CHECK(Abs(table->Mass().center.z) < 1e-5f);
    // Ixx about the centre: the parts' own plus the parallel-axis terms.
    const f32 dt = 0.8f - cy;
    const f32 dl = 0.4f - cy;
    const f32 ixx = top.inertia.x + top.mass * dt * dt + 4.0f * (leg.inertia.x + leg.mass * (dl * dl + 0.09f));
    const f32 iyy = top.inertia.y + 4.0f * (leg.inertia.y + leg.mass * (0.25f + 0.09f));
    const f32 izz = top.inertia.z + top.mass * dt * dt + 4.0f * (leg.inertia.z + leg.mass * (dl * dl + 0.25f));
    CHECK(CNear(Tensor(table->Mass(), 0, 0), ixx, 1e-4f));
    CHECK(CNear(Tensor(table->Mass(), 1, 1), iyy, 1e-4f));
    CHECK(CNear(Tensor(table->Mass(), 2, 2), izz, 1e-4f));
    CHECK(Abs(Tensor(table->Mass(), 0, 1)) < 1e-3f * ixx);
    // The bound covers the legs and the top.
    CHECK(table->Extent().x >= 0.6f);
    CHECK(table->Extent().y >= 0.85f);

    // A box split in two rotated halves has the box's mass and inertia.
    const Vec3 half(0.5f, 0.3f, 0.7f);
    const f32 density = 250.0f;
    Vector<CompoundChild> parts;
    const Quaternion turn = Quaternion::FromAxisAngle(Vec3(0, 1, 0), 0.7f);
    for (u32 i = 0; i < 2; ++i)
    {
        CompoundChild c = Child(Shape::MakeBox(Vec3(half.x * 0.5f, half.y, half.z)),
                                Vec3(i == 0 ? -half.x * 0.5f : half.x * 0.5f, 0, 0), density);
        parts.Push(c);
    }
    // Turn the whole compound's children about the origin by rotating both offsets and orientations.
    for (usize i = 0; i < parts.Size(); ++i)
    {
        parts[i].offset = turn.RotateVector(parts[i].offset);
        parts[i].rotation = turn;
    }
    Validation r = Validation::Ok;
    const CompoundShapeConstPtr split = CompoundShape::Create(Span<const CompoundChild>(parts.Data(), parts.Size()),
                                                              nullptr, &r);
    REQUIRE(split);
    const MassProperties whole = MassFromDensity(Shape::MakeBox(half), density);
    CHECK(CNear(split->Mass().mass, whole.mass, 1e-5f));
    CHECK(Abs(split->Mass().center.x) < 1e-5f);
    // The tensors agree after turning the whole box by the same rotation.
    const Mat3 rt = turn.ToMat3();
    for (u32 row = 0; row < 3; ++row)
    {
        for (u32 col = 0; col < 3; ++col)
        {
            f32 expected = 0.0f;
            const f32 v[3] = {whole.inertia.x, whole.inertia.y, whole.inertia.z};
            for (u32 k = 0; k < 3; ++k)
            {
                expected += rt.col[k][row] * v[k] * rt.col[k][col];
            }
            CHECK(Abs(Tensor(split->Mass(), row, col) - expected) <
                  1e-4f * (whole.inertia.x + whole.inertia.y + whole.inertia.z));
        }
    }
}

TEST_CASE("Collide compound: a table on the floor touches with its legs, with child ids", "[physics][compound]")
{
    const CompoundShapeConstPtr table = Table();
    CompoundLibrary compounds;
    const u32 id = compounds.Add(table);
    const Shape shape = Shape::MakeCompound(id, table->Extent());
    const Shape floor = Shape::MakeBox(Vec3(5, 0.5f, 5));
    Pose floor_pose;
    floor_pose.position = Vec3(0, -0.5f, 0);
    Pose pose;
    pose.position = Vec3(0, -0.02f,
                         0);  // the legs sink a little: their bottoms are at 0.4 - 0.35 = 0.05 above the origin
    pose.position = Vec3(0, -0.07f, 0);
    ContactManifold m;
    REQUIRE(Collide(floor, floor_pose, shape, pose, 0.02f, &m, nullptr, nullptr, &compounds) == CollideResult::Contact);
    CHECK(Abs(m.normal.y - 1.0f) < 1e-3f);
    CHECK(m.count == 4);
    for (u32 i = 0; i < m.count; ++i)
    {
        CHECK(Abs(m.points[i].depth - 0.02f) < 3e-3f);
        for (u32 j = i + 1; j < m.count; ++j)
        {
            CHECK(m.points[i].feature_id != m.points[j].feature_id);
        }
    }
    // The other order: the normal turns around and the ids stay.
    ContactManifold flipped;
    REQUIRE(Collide(shape, pose, floor, floor_pose, 0.02f, &flipped, nullptr, nullptr, &compounds) ==
            CollideResult::Contact);
    CHECK(Abs(flipped.normal.y + 1.0f) < 1e-3f);
    CHECK(flipped.count == 4);
    // Lifted off: nothing; an unknown compound: nothing.
    pose.position = Vec3(0, 1.0f, 0);
    CHECK(Collide(floor, floor_pose, shape, pose, 0.02f, &m, nullptr, nullptr, &compounds) == CollideResult::None);
    CHECK(Collide(floor, floor_pose, shape, pose, 0.02f, &m) == CollideResult::None);
    // Two tables with their legs interleaved: the children that touch decide.
    Pose p1;
    Pose p2;
    p2.position = Vec3(0.0f, 0.0f, 0.55f);
    CHECK(Collide(shape, p1, shape, p2, 0.02f, &m, nullptr, nullptr, &compounds) == CollideResult::Contact);
}

TEST_CASE("Groups and masks: the truth table and the broad phase", "[physics][compound]")
{
    for (u32 ga = 0; ga < 8; ++ga)
    {
        for (u32 ma = 0; ma < 8; ++ma)
        {
            for (u32 gb = 0; gb < 8; ++gb)
            {
                for (u32 mb = 0; mb < 8; ++mb)
                {
                    const bool expected = (ga & mb) != 0 && (gb & ma) != 0;
                    CHECK(GroupsCollide(ga, ma, gb, mb) == expected);
                    CHECK(GroupsCollide(gb, mb, ga, ma) == expected);  // order does not matter
                }
            }
        }
    }
    PhysicsWorld world;
    BroadPhaseConfig config;
    BroadPhase bp(config);
    const auto add = [&](u32 group, u32 mask, bool trigger)
    {
        BodyDesc desc;
        desc.settings.type = BodyType::Dynamic;
        const BodyId b = world.CreateBody(desc);
        Collider c;
        c.shape = Shape::MakeSphere(0.5f);
        c.group = group;
        c.mask = mask;
        c.is_trigger = trigger;
        REQUIRE(world.AddCollider(b, c) == BodyStatus::Ok);
        return b;
    };
    add(1, 2, false);            // sees group 2 only
    add(2, 1, false);            // in group 2, sees group 1: they meet
    add(4, 0xFFFFFFFFu, false);  // in group 4: the first one's mask refuses it
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 1);
    CHECK(bp.Begun().Size() == 1);
}

TEST_CASE("Triggers: begin and end events, no contact pair kept, trigger pairs off by default", "[physics][compound]")
{
    PhysicsWorld world;
    BroadPhaseConfig config;
    BroadPhase bp(config);
    BodyDesc zone;
    zone.settings.type = BodyType::Static;
    const BodyId zone_body = world.CreateBody(zone);
    Collider zc;
    zc.shape = Shape::MakeBox(Vec3(2, 2, 2));
    zc.is_trigger = true;
    REQUIRE(world.AddCollider(zone_body, zc) == BodyStatus::Ok);
    BodyDesc ball;
    ball.settings.type = BodyType::Dynamic;
    ball.position = Vec3(-10, 0, 0);
    ball.velocity.linear = Vec3(5, 0, 0);
    const BodyId ball_body = world.CreateBody(ball);
    Collider bc;
    bc.shape = Shape::MakeSphere(0.5f);
    REQUIRE(world.AddCollider(ball_body, bc) == BodyStatus::Ok);
    BodyDesc other_zone;
    other_zone.settings.type = BodyType::Static;
    other_zone.position = Vec3(0, 1, 0);
    const BodyId other = world.CreateBody(other_zone);
    Collider oc = zc;
    REQUIRE(world.AddCollider(other, oc) == BodyStatus::Ok);

    u32 begins = 0;
    u32 ends = 0;
    bool begin_was_trigger = true;
    for (u32 step = 0; step < 400; ++step)
    {
        world.SetTransform(ball_body, Vec3(-10.0f + 0.05f * static_cast<f32>(step), 0, 0), Quaternion::Identity());
        bp.Update(world);
        for (usize i = 0; i < bp.Begun().Size(); ++i)
        {
            ++begins;
            begin_was_trigger = begin_was_trigger && bp.Begun()[i].trigger;
        }
        ends += static_cast<u32>(bp.Ended().Size());
        // Static trigger against static trigger never makes a pair; the ball against a trigger makes a trigger pair.
        for (usize i = 0; i < bp.Pairs().Size(); ++i)
        {
            CHECK(bp.Pairs()[i].trigger);
        }
    }
    // The ball meets the zone and the second zone; each begins once and ends once.
    CHECK(begins == 2);
    CHECK(ends == 2);
    CHECK(begin_was_trigger);
    // The body's velocity was never touched.
    const Body* b = world.Find(ball_body);
    CHECK(b->linear_velocity.x == 5.0f);
    CHECK(b->linear_velocity.y == 0.0f);
}

TEST_CASE("A compound collider rebuilds its mass and bound when the shape changes", "[physics][compound]")
{
    PhysicsWorld world;
    const CompoundShapeConstPtr table = Table();
    const u32 id = world.Compounds().Add(table);
    BodyDesc desc;
    desc.settings.type = BodyType::Dynamic;
    const BodyId body = world.CreateBody(desc);
    Collider c;
    c.shape = Shape::MakeCompound(id, table->Extent());
    Collider small = c;
    small.shape = Shape::MakeCompound(id, table->Extent() * 0.5f);
    CHECK(world.AddCollider(body, small) == BodyStatus::Invalid);  // the bound does not cover the compound
    CHECK(Shape::MakeCompound(9, table->Extent()).Validate() == Validation::Ok);
    Collider unknown = c;
    unknown.shape = Shape::MakeCompound(9, table->Extent());
    CHECK(world.AddCollider(body, unknown) == BodyStatus::Invalid);
    REQUIRE(world.AddCollider(body, c) == BodyStatus::Ok);
    const Body* held = world.Find(body);
    CHECK(CNear(held->mass.mass, table->Mass().mass, 1e-5f));
    CHECK(CNear(held->mass.center.y, table->Mass().center.y, 1e-5f));
    BroadPhase bp;
    bp.Update(world);
    // Replace the compound by a sphere: the mass and the bound follow.
    Collider ball;
    ball.shape = Shape::MakeSphere(0.5f);
    ball.material.density = 100.0f;
    REQUIRE(world.SetCollider(body, ball) == BodyStatus::Ok);
    CHECK(CNear(world.Find(body)->mass.mass, MassFromDensity(ball.shape, 100.0f).mass, 1e-5f));
    CHECK(Abs(world.Find(body)->mass.center.y) < 1e-6f);
    bp.Update(world);
    CHECK(bp.Pairs().Size() == 0);
}

namespace
{

// The sample (every shape and pose) and the contacts, hashed apart so that a difference in the sample is told from one
// in the code.
struct CompoundHashes
{
    u64 inputs;
    u64 outputs;
};

CompoundHashes CompoundHash()
{
    StateHasher in;
    CompoundLibrary compounds;
    const CompoundShapeConstPtr table = Table();
    const Shape shape = Shape::MakeCompound(compounds.Add(table), table->Extent());
    Rng rng(383);
    StateHasher out;
    for (u32 i = 0; i < 20000; ++i)
    {
        Shape other;
        const u32 kind = rng.Next() % 4;
        in.AddU32(kind);
        if (kind == 0)
        {
            const f32 radius = rng.Range(0.2f, 0.7f);
            in.AddF32(radius);
            other = Shape::MakeSphere(radius);
        }
        else if (kind == 1)
        {
            const f32 hh = rng.Range(0.0f, 0.8f);
            const f32 rr = rng.Range(0.15f, 0.4f);
            in.AddF32(hh);
            in.AddF32(rr);
            other = Shape::MakeCapsule(hh, rr);
        }
        else if (kind == 2)
        {
            const f32 hx = rng.Range(0.2f, 0.8f);
            const f32 hy = rng.Range(0.2f, 0.8f);
            const f32 hz = rng.Range(0.2f, 0.8f);
            in.AddF32(hx);
            in.AddF32(hy);
            in.AddF32(hz);
            other = Shape::MakeBox(Vec3(hx, hy, hz));
        }
        else
        {
            other = shape;
        }
        Pose pa;
        Pose pb;
        const f32 x = rng.Range(-1.2f, 1.2f);
        const f32 y = rng.Range(-0.5f, 1.5f);
        const f32 z = rng.Range(-1.2f, 1.2f);
        pb.position = Vec3(x, y, z);
        pb.rotation = testing::RandomTurnAbout(rng, Vec3(0, 1, 0));
        testing::HashPose(in, pb);
        ContactManifold m;
        const CollideResult r = Collide(shape, pa, other, pb, 0.02f, &m, nullptr, nullptr, &compounds);
        out.AddU8(static_cast<u8>(r));
        if (r == CollideResult::Contact)
        {
            out.AddU32(m.count);
            out.AddF32(m.normal.x);
            out.AddF32(m.normal.y);
            out.AddF32(m.normal.z);
            for (u32 k = 0; k < m.count; ++k)
            {
                out.AddF32(m.points[k].depth);
                out.AddF32(m.points[k].position_a.x);
                out.AddF32(m.points[k].position_a.y);
                out.AddF32(m.points[k].position_a.z);
                out.AddU32(m.points[k].feature_id);
            }
        }
    }
    return {in.Value(), out.Value()};
}

}  // namespace

TEST_CASE("Collide compound: the contacts of 20000 random shapes hash to the committed constant",
          "[physics][compound][hash]")
{
    const CompoundHashes h = CompoundHash();
    CHECK(h.inputs == 0xa9fbbe81c8cac993ull);
    CHECK(h.outputs == 0xc9a286265fa0bdc1ull);
}
