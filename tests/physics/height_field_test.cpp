// Height fields in the physics module: the field, its surface and queries, the shape's rules in a world, the contacts
// of shapes resting on it, and ray, overlap and closest-point queries through the world.
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "bytes/byte_writer.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "physics/collide.h"
#include "physics/height_field.h"
#include "physics/query.h"
#include "physics/snapshot.h"
#include "physics/world.h"
#include "test_values.h"

using namespace vse;
using namespace vse::physics;
using vse::physics::testing::Rng;

namespace
{

bool HfNear(f32 a, f32 b, f32 tolerance = 1e-4f)
{
    return Abs(a - b) <= tolerance;
}

Pose HfPose(f32 x, f32 y, f32 z)
{
    Pose p;
    p.position = Vec3(x, y, z);
    return p;
}

// A field of w by d samples; `value(x, z)` gives the sample value (0..65535).
template <class Value> HeightFieldConstPtr MakeField(u32 w, u32 d, f32 lo, f32 hi, f32 cell, Value value)
{
    Vector<u16> samples;
    for (u32 z = 0; z < d; ++z)
    {
        for (u32 x = 0; x < w; ++x)
        {
            samples.Push(static_cast<u16>(value(x, z)));
        }
    }
    Validation v = Validation::Ok;
    HeightFieldConstPtr field = HeightField::CreateFromSamples(Span<const u16>(samples.Data(), samples.Size()), w, d,
                                                               lo, hi, cell, &v);
    REQUIRE(v == Validation::Ok);
    REQUIRE(field);
    return field;
}

// A flat field at height 0 (range 0 to 1, every sample 0), `n` cells across, one metre a cell.
HeightFieldConstPtr FlatField(u32 n)
{
    return MakeField(n + 1, n + 1, 0.0f, 1.0f, 1.0f, [](u32, u32) { return 0u; });
}

// Samples that are the same hash of (x, z): a rough field for the brute-force comparisons.
u32 Rough(u32 x, u32 z)
{
    u32 h = x * 73856093u ^ z * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h % 65536u;
}

// A brute-force ray against every triangle, from either side.
bool BruteRay(const HeightField& field, const Vec3& o, const Vec3& d, f32 tmax, f32* t_out, u32* tri_out)
{
    bool hit = false;
    for (u32 t = 0; t < field.TriangleCount(); ++t)
    {
        Vec3 a;
        Vec3 b;
        Vec3 c;
        field.Triangle(t, &a, &b, &c);
        const Vec3 e1 = b - a;
        const Vec3 e2 = c - a;
        const Vec3 p = d.Cross(e2);
        const f32 det = e1.Dot(p);
        if (Abs(det) < 1e-12f)
        {
            continue;
        }
        const f32 inv = 1.0f / det;
        const Vec3 s = o - a;
        const f32 u = s.Dot(p) * inv;
        if (u < 0.0f || u > 1.0f)
        {
            continue;
        }
        const Vec3 q = s.Cross(e1);
        const f32 v = d.Dot(q) * inv;
        if (v < 0.0f || u + v > 1.0f)
        {
            continue;
        }
        const f32 tt = e2.Dot(q) * inv;
        if (tt < 0.0f || tt > tmax)
        {
            continue;
        }
        if (!hit || tt < *t_out)
        {
            hit = true;
            *t_out = tt;
            *tri_out = t;
        }
    }
    return hit;
}

}  // namespace

TEST_CASE("HeightField: bad data is refused by status", "[physics][heightfield]")
{
    Validation v = Validation::Ok;
    Vector<u16> two;
    for (u32 i = 0; i < 4; ++i)
    {
        two.Push(0);
    }
    const Span<const u16> four(two.Data(), 4);
    CHECK(HeightField::CreateFromSamples(four, 2, 2, 0.0f, 1.0f, 1.0f, &v));
    CHECK(v == Validation::Ok);
    // A one-sample-wide grid, a wrong sample count, a side past the limit.
    CHECK(!HeightField::CreateFromSamples(Span<const u16>(two.Data(), 2), 1, 2, 0.0f, 1.0f, 1.0f, &v));
    CHECK(v == Validation::OutOfRange);
    CHECK(!HeightField::CreateFromSamples(four, 2, 3, 0.0f, 1.0f, 1.0f, &v));
    CHECK(v == Validation::OutOfRange);
    CHECK(!HeightField::CreateFromSamples(four, 8193, 2, 0.0f, 1.0f, 1.0f, &v));
    CHECK(v == Validation::OutOfRange);
    // Non-finite numbers, a cell that is not positive, an inverted range, a field too big for a shape.
    CHECK(!HeightField::CreateFromSamples(four, 2, 2, testing::kNaN, 1.0f, 1.0f, &v));
    CHECK(v == Validation::NotFinite);
    CHECK(!HeightField::CreateFromSamples(four, 2, 2, 0.0f, 1.0f, Limits<f32>::Infinity(), &v));
    CHECK(v == Validation::NotFinite);
    CHECK(!HeightField::CreateFromSamples(four, 2, 2, 0.0f, 1.0f, 0.0f, &v));
    CHECK(v == Validation::NotPositive);
    CHECK(!HeightField::CreateFromSamples(four, 2, 2, 2.0f, 1.0f, 1.0f, &v));
    CHECK(v == Validation::NotPositive);
    CHECK(!HeightField::CreateFromSamples(four, 2, 2, 0.0f, 1.0f, 1.0e6f, &v));
    CHECK(v == Validation::OutOfRange);
    // A region too short for the samples, and no region.
    HeightFieldSpec spec;
    spec.width = 2;
    spec.height = 2;
    CHECK(!HeightField::Create(spec, &v));
    CHECK(v == Validation::OutOfRange);
    Vector<byte> bytes;
    bytes.Resize(7);
    spec.bytes = CopyShared(bytes);
    CHECK(!HeightField::Create(spec, &v));
    CHECK(v == Validation::OutOfRange);
    spec.offset = 4;
    bytes.Resize(8);
    spec.bytes = CopyShared(bytes);
    CHECK(!HeightField::Create(spec, &v));  // the samples would end at byte 12
    spec.offset = 0;
    CHECK(HeightField::Create(spec, &v));
}

TEST_CASE("HeightField: the surface, its normals and neighbours", "[physics][heightfield]")
{
    // A ramp rising along x: sample value 6553 per sample, range 0 to 10, cell 2 m.
    const HeightFieldConstPtr ramp = MakeField(5, 4, 0.0f, 10.0f, 2.0f, [](u32 x, u32) { return x * 16383u; });
    CHECK(ramp->TriangleCount() == 2 * 4 * 3);
    CHECK(HfNear(ramp->Extent().x, 4.0f));
    CHECK(HfNear(ramp->Extent().z, 3.0f));
    // The slope is 16383/65535 * 10 / 2 m: the height at the centre of the map is half of the way across.
    const f32 slope = 16383.0f / 65535.0f * 10.0f / 2.0f;
    CHECK(HfNear(ramp->HeightAt(0.0f, 0.0f), slope * 4.0f, 1e-3f));
    CHECK(HfNear(ramp->HeightAt(-4.0f, 1.0f), 0.0f, 1e-3f));
    CHECK(HfNear(ramp->HeightAt(4.0f, -1.5f), slope * 8.0f, 1e-3f));
    CHECK(HfNear(ramp->HeightAt(100.0f, 0.0f), ramp->HeightAt(4.0f, 0.0f)));  // clamped to the map
    // Every triangle faces up; edges are shared both ways.
    const f32 unit = Sqrt(1.0f + slope * slope);
    for (u32 t = 0; t < ramp->TriangleCount(); ++t)
    {
        const Vec3 n = ramp->Normal(t);
        CHECK(HfNear(n.x, -slope / unit, 1e-4f));
        CHECK(HfNear(n.y, 1.0f / unit, 1e-4f));
        CHECK(HfNear(n.z, 0.0f, 1e-4f));
        for (u32 e = 0; e < 3; ++e)
        {
            const u32 other = ramp->Neighbor(t, e);
            if (other == kNoFieldTriangle)
            {
                continue;
            }
            REQUIRE(other < ramp->TriangleCount());
            bool back = false;
            for (u32 f = 0; f < 3; ++f)
            {
                back = back || ramp->Neighbor(other, f) == t;
            }
            CHECK(back);
        }
    }
    // The border has no neighbour: 2 * (4 + 3) edges on the outside.
    u32 border = 0;
    for (u32 t = 0; t < ramp->TriangleCount(); ++t)
    {
        for (u32 e = 0; e < 3; ++e)
        {
            border += ramp->Neighbor(t, e) == kNoFieldTriangle ? 1u : 0u;
        }
    }
    CHECK(border == 2 * (4 + 3));
}

TEST_CASE("HeightField: the library ids stay stable", "[physics][heightfield]")
{
    HeightFieldLibrary library;
    const HeightFieldConstPtr a = FlatField(4);
    const HeightFieldConstPtr b = FlatField(8);
    CHECK(library.Add(HeightFieldConstPtr()) == 0);
    CHECK(library.Add(a) == 1);
    CHECK(library.Add(b) == 2);
    CHECK(library.Count() == 2);
    CHECK(library.Find(1) == a.Get());
    CHECK(library.Find(2) == b.Get());
    CHECK(library.Find(0) == nullptr);
    CHECK(library.Find(3) == nullptr);
    CHECK(library.Share(2).Get() == b.Get());
    CHECK(a->ContentHash() != b->ContentHash());
    CHECK(a->ContentHash() == FlatField(4)->ContentHash());
}

TEST_CASE("HeightField: rays against a ramp, a bump and the edge of the map", "[physics][heightfield]")
{
    f32 t = 0.0f;
    u32 tri = 0;
    // The flat floor, from above and from below: the hit is at the same distance, the triangle is the same.
    const HeightFieldConstPtr flat = FlatField(10);
    REQUIRE(flat->RayCast(Vec3(0.3f, 5.0f, 0.2f), Vec3(0, -1, 0), 100.0f, &t, &tri));
    CHECK(HfNear(t, 5.0f));
    u32 below_tri = 0;
    f32 below = 0.0f;
    // The flat field has range 0..1 but every sample is 0: the surface is at y = 0.
    REQUIRE(flat->RayCast(Vec3(0.3f, -5.0f, 0.2f), Vec3(0, 1, 0), 100.0f, &below, &below_tri));
    CHECK(HfNear(below, 5.0f));
    CHECK(below_tri == tri);
    // A ray that stops short, one that points away, one lying in the surface.
    CHECK(!flat->RayCast(Vec3(0.3f, 5.0f, 0.2f), Vec3(0, -1, 0), 4.0f, &t, &tri));
    CHECK(!flat->RayCast(Vec3(0.3f, 5.0f, 0.2f), Vec3(0, 1, 0), 100.0f, &t, &tri));
    CHECK(!flat->RayCast(Vec3(-20.0f, 0.0f, 0.2f), Vec3(1, 0, 0), 100.0f, &t, &tri));
    // Parallel above the surface.
    CHECK(!flat->RayCast(Vec3(-20.0f, 0.5f, 0.2f), Vec3(1, 0, 0), 100.0f, &t, &tri));
    // Outside the map, and entering it from outside.
    CHECK(!flat->RayCast(Vec3(20.0f, 5.0f, 0.0f), Vec3(0, -1, 0), 100.0f, &t, &tri));
    REQUIRE(flat->RayCast(Vec3(-20.0f, 5.0f, 0.0f), Vec3(1, -0.25f, 0), 100.0f, &t, &tri));
    CHECK(HfNear(t * 0.25f, 5.0f, 1e-3f));  // y = 5 - 0.25 * t reaches 0 at t = 20; the field spans x -5..5 at t >= 15
    // Bad rays.
    CHECK(!flat->RayCast(Vec3(0, 5, 0), Vec3(0, 0, 0), 100.0f, &t, &tri));
    CHECK(!flat->RayCast(Vec3(0, 5, 0), Vec3(0, -1, 0), -1.0f, &t, &tri));
    CHECK(!flat->RayCast(Vec3(testing::kNaN, 5, 0), Vec3(0, -1, 0), 100.0f, &t, &tri));

    // A bump of 20 m in the middle of a 33-sample field (four blocks across): rays aimed at the foot reach it, rays
    // that pass above everything miss, and the hit is the closest.
    const HeightFieldConstPtr bump = MakeField(33, 33, 0.0f, 20.0f, 1.0f,
                                               [](u32 x, u32 z)
                                               {
                                                   const i32 dx = static_cast<i32>(x) - 16;
                                                   const i32 dz = static_cast<i32>(z) - 16;
                                                   const i32 r2 = dx * dx + dz * dz;
                                                   return r2 >= 64 ? 0u : static_cast<u32>((64 - r2) * 1023);
                                               });
    REQUIRE(bump->RayCast(Vec3(0.0f, 30.0f, 0.0f), Vec3(0, -1, 0), 100.0f, &t, &tri));
    CHECK(HfNear(30.0f - t, bump->HeightAt(0.0f, 0.0f), 1e-3f));
    REQUIRE(bump->RayCast(Vec3(-30.0f, 3.0f, 0.0f), Vec3(1, 0, 0), 100.0f, &t, &tri));  // sideways into the hill
    CHECK(t > 14.0f);
    CHECK(t < 30.0f);
    CHECK(HfNear(bump->HeightAt(-30.0f + t, 0.0f), 3.0f, 2e-2f));
    CHECK(!bump->RayCast(Vec3(-30.0f, 25.0f, 0.0f), Vec3(1, 0, 0), 100.0f, &t, &tri));  // above the top
}

TEST_CASE("HeightField: random rays agree with every triangle tested one by one", "[physics][heightfield][random]")
{
    const HeightFieldConstPtr field = MakeField(41, 37, -30.0f, 50.0f, 1.5f, Rough);
    Rng rng(7);
    u32 hits = 0;
    for (u32 i = 0; i < 400; ++i)
    {
        const Vec3 o(rng.Range(-40.0f, 40.0f), rng.Range(-40.0f, 60.0f), rng.Range(-40.0f, 40.0f));
        const Vec3 target(rng.Range(-30.0f, 30.0f), rng.Range(-30.0f, 50.0f), rng.Range(-27.0f, 27.0f));
        const Vec3 d = target - o;
        f32 expect_t = 0.0f;
        u32 expect_tri = 0;
        const bool expect = BruteRay(*field, o, d, 1.5f, &expect_t, &expect_tri);
        f32 t = 0.0f;
        u32 tri = 0;
        const bool got = field->RayCast(o, d, 1.5f, &t, &tri);
        REQUIRE(got == expect);
        if (expect)
        {
            ++hits;
            CHECK(HfNear(t, expect_t, 1e-3f));
            CHECK(tri == expect_tri);
        }
    }
    CHECK(hits > 100);
}

TEST_CASE("HeightField: a box query returns every triangle the box can touch, in order", "[physics][heightfield]")
{
    const HeightFieldConstPtr field = MakeField(41, 37, -30.0f, 50.0f, 1.5f, Rough);
    Rng rng(11);
    for (u32 i = 0; i < 200; ++i)
    {
        const Vec3 c(rng.Range(-35.0f, 35.0f), rng.Range(-30.0f, 50.0f), rng.Range(-35.0f, 35.0f));
        const Vec3 h(rng.Range(0.1f, 6.0f), rng.Range(0.1f, 10.0f), rng.Range(0.1f, 6.0f));
        const Aabb box = Aabb::FromMinMax(c - h, c + h);
        Vector<u32> found;
        const u32 n = field->QueryBox(box, &found);
        CHECK(n == found.Size());
        for (usize k = 1; k < found.Size(); ++k)
        {
            REQUIRE(found[k - 1] < found[k]);
        }
        for (u32 t = 0; t < field->TriangleCount(); ++t)
        {
            Vec3 a;
            Vec3 b;
            Vec3 cc;
            field->Triangle(t, &a, &b, &cc);
            const Vec3 lo(FMin(a.x, FMin(b.x, cc.x)), FMin(a.y, FMin(b.y, cc.y)), FMin(a.z, FMin(b.z, cc.z)));
            const Vec3 hi(FMax(a.x, FMax(b.x, cc.x)), FMax(a.y, FMax(b.y, cc.y)), FMax(a.z, FMax(b.z, cc.z)));
            const bool overlap = lo.x <= box.max.x && hi.x >= box.min.x && lo.y <= box.max.y && hi.y >= box.min.y &&
                                 lo.z <= box.max.z && hi.z >= box.min.z;
            if (!overlap)
            {
                continue;
            }
            bool listed = false;
            for (usize k = 0; k < found.Size(); ++k)
            {
                listed = listed || found[k] == t;
            }
            REQUIRE(listed);
        }
    }
    // The limit is kept, and a huge shape over the whole map still collides (the pair lists a bounded number).
    Vector<u32> few;
    CHECK(field->QueryBox(Aabb::FromMinMax(Vec3(-100, -100, -100), Vec3(100, 100, 100)), &few, 10) == 10);
    CHECK(few.Size() == 10);
    HeightFieldLibrary library;
    const Shape ground = Shape::MakeHeightField(library.Add(field), field->Extent());
    ContactManifold manifold;
    Pose p;
    p.position = Vec3(0, 20, 0);
    CHECK(Collide(ground, Pose(), Shape::MakeSphere(5000.0f), p, 0.02f, &manifold, nullptr, nullptr, nullptr,
                  &library) == CollideResult::Contact);
    // Outside the map and empty boxes find nothing.
    Vector<u32> none;
    CHECK(field->QueryBox(Aabb::FromMinMax(Vec3(100, -50, 100), Vec3(110, 90, 110)), &none) == 0);
    CHECK(field->QueryBox(Aabb::FromMinMax(Vec3(0, 80, 0), Vec3(1, 90, 1)), &none) == 0);
}

TEST_CASE("Height field shape: static bodies only, a known field with a covering extent", "[physics][heightfield]")
{
    PhysicsWorld world;
    const HeightFieldConstPtr field = FlatField(8);
    BodyDesc desc;
    desc.settings.type = BodyType::Static;
    const BodyId ground = world.CreateBody(desc);
    Collider collider;
    collider.shape = Shape::MakeHeightField(1, field->Extent());
    CHECK(world.AddCollider(ground, collider) == BodyStatus::Invalid);  // not in the library
    const u32 id = world.HeightFields().Add(field);
    CHECK(Shape::MakeHeightField(0, field->Extent()).Validate() == Validation::OutOfRange);
    collider.shape = Shape::MakeHeightField(id, field->Extent() * 0.5f);
    CHECK(world.AddCollider(ground, collider) == BodyStatus::Invalid);  // the bound does not cover the field
    collider.shape = Shape::MakeHeightField(id, field->Extent());
    CHECK(world.AddCollider(ground, collider) == BodyStatus::Ok);
    BodyDesc dynamic;
    dynamic.settings.type = BodyType::Dynamic;
    const BodyId ball = world.CreateBody(dynamic);
    CHECK(world.AddCollider(ball, collider) == BodyStatus::Invalid);
    RigidBody settings = desc.settings;
    settings.type = BodyType::Dynamic;
    CHECK(world.SetSettings(ground, settings) == BodyStatus::Invalid);
    CHECK(MassFromDensity(collider.shape, 1.0f).mass == 0.0f);
    // The shape's bound is the field's extent about the origin.
    const Aabb bound = collider.shape.Bounds(Vec3(10, 0, 0), Quaternion::Identity());
    CHECK(HfNear(bound.min.x, 6.0f));
    CHECK(HfNear(bound.max.x, 14.0f));
    // A height field is not a compound's child.
    CompoundShapeConstPtr compound;
    Validation v = Validation::Ok;
    Vector<CompoundChild> children;
    CompoundChild child;
    child.shape = collider.shape;
    children.Push(child);
    compound = CompoundShape::Create(Span<const CompoundChild>(children.Data(), children.Size()), &world.Hulls(), &v);
    CHECK(!compound);
    CHECK(v == Validation::OutOfRange);
}

TEST_CASE("Collide height field: shapes resting on a ramp and on flat ground", "[physics][heightfield]")
{
    HeightFieldLibrary fields;
    const HeightFieldConstPtr flat = FlatField(10);
    const Shape ground = Shape::MakeHeightField(fields.Add(flat), flat->Extent());
    ContactManifold m;
    const Shape box = Shape::MakeBox(Vec3(0.4f, 0.5f, 0.4f));
    REQUIRE(Collide(ground, HfPose(0, 0, 0), box, HfPose(0.3f, 0.45f, 0.2f), 0.02f, &m, nullptr, nullptr, nullptr,
                    &fields) == CollideResult::Contact);
    CHECK(HfNear(m.normal.y, 1.0f, 1e-3f));
    CHECK(m.count == 4);
    for (u32 i = 0; i < m.count; ++i)
    {
        CHECK(HfNear(m.points[i].depth, 0.05f, 2e-3f));
    }
    // The other order flips the normal.
    ContactManifold flipped;
    REQUIRE(Collide(box, HfPose(0.3f, 0.45f, 0.2f), ground, HfPose(0, 0, 0), 0.02f, &flipped, nullptr, nullptr, nullptr,
                    &fields) == CollideResult::Contact);
    CHECK(HfNear(flipped.normal.y, -1.0f, 1e-3f));
    // A sphere: one point; a capsule: contact. Above the field or past its edge: nothing.
    REQUIRE(Collide(ground, HfPose(0, 0, 0), Shape::MakeSphere(0.5f), HfPose(1.3f, 0.45f, -0.7f), 0.02f, &m, nullptr,
                    nullptr, nullptr, &fields) == CollideResult::Contact);
    CHECK(m.count == 1);
    CHECK(HfNear(m.points[0].depth, 0.05f, 2e-3f));
    CHECK(Collide(ground, HfPose(0, 0, 0), box, HfPose(0, 2.0f, 0), 0.02f, &m, nullptr, nullptr, nullptr, &fields) ==
          CollideResult::None);
    CHECK(Collide(ground, HfPose(0, 0, 0), box, HfPose(40, 0.3f, 0), 0.02f, &m, nullptr, nullptr, nullptr, &fields) ==
          CollideResult::None);
    // A field that is not in the library: nothing. Two fields, or a field and a mesh: unsupported.
    CHECK(Collide(ground, HfPose(0, 0, 0), box, HfPose(0, 0.3f, 0), 0.02f, &m) == CollideResult::None);
    CHECK(Collide(ground, HfPose(0, 0, 0), ground, HfPose(0, 0, 0), 0.02f, &m, nullptr, nullptr, nullptr, &fields) ==
          CollideResult::Unsupported);

    // A 45 degree ramp: a sphere resting on it is pushed along the slope's normal.
    const HeightFieldConstPtr ramp = MakeField(11, 11, 0.0f, 10.0f, 1.0f, [](u32 x, u32) { return x * 6553u; });
    const f32 slope = 6553.0f / 65535.0f * 10.0f;  // about 1
    HeightFieldLibrary ramps;
    const Shape slab = Shape::MakeHeightField(ramps.Add(ramp), ramp->Extent());
    const f32 x = 0.0f;
    const f32 height = ramp->HeightAt(x, 0.0f);
    const Vec3 up = Vec3(-slope, 1.0f, 0.0f) * (1.0f / Sqrt(1.0f + slope * slope));
    const Vec3 centre = Vec3(x, height, 0.0f) + up * 0.45f;
    REQUIRE(Collide(slab, HfPose(0, 0, 0), Shape::MakeSphere(0.5f), HfPose(centre.x, centre.y, centre.z), 0.02f, &m,
                    nullptr, nullptr, nullptr, &ramps) == CollideResult::Contact);
    CHECK(HfNear(m.normal.x, up.x, 2e-2f));
    CHECK(HfNear(m.normal.y, up.y, 2e-2f));
    CHECK(HfNear(m.points[0].depth, 0.05f, 5e-3f));
}

TEST_CASE("Queries: rays, overlaps and the closest point meet a height field in a world", "[physics][heightfield]")
{
    PhysicsWorld world;
    const HeightFieldConstPtr field = FlatField(20);
    BodyDesc desc;
    desc.settings.type = BodyType::Static;
    desc.position = Vec3(0, 2, 0);
    const BodyId ground = world.CreateBody(desc);
    Collider collider;
    collider.shape = Shape::MakeHeightField(world.HeightFields().Add(field), field->Extent());
    REQUIRE(world.AddCollider(ground, collider) == BodyStatus::Ok);
    PhysicsQueryFilter filter;
    PhysicsHit hit;
    REQUIRE(RayCast(world, Vec3(1, 10, 1), Vec3(0, -1, 0), 100.0f, filter, &hit));
    CHECK(HfNear(hit.distance, 8.0f));
    CHECK(HfNear(hit.point.y, 2.0f));
    CHECK(HfNear(hit.normal.y, 1.0f));
    CHECK(hit.triangle < field->TriangleCount());
    // From below the surface: hit from below, the normal faces the ray.
    REQUIRE(RayCast(world, Vec3(1, -10, 1), Vec3(0, 1, 0), 100.0f, filter, &hit));
    CHECK(HfNear(hit.distance, 12.0f));
    CHECK(HfNear(hit.normal.y, -1.0f));
    CHECK(!RayCast(world, Vec3(1, 10, 1), Vec3(0, 1, 0), 100.0f, filter, &hit));
    // A sphere overlapping the ground is found; one above it is not.
    Vector<ColliderId> found;
    Overlap(world, Shape::MakeSphere(0.5f), Vec3(3, 2.3f, 3), Quaternion::Identity(), filter, &found);
    CHECK(found.Size() == 1);
    found.Clear();
    Overlap(world, Shape::MakeSphere(0.5f), Vec3(3, 4.0f, 3), Quaternion::Identity(), filter, &found);
    CHECK(found.Size() == 0);
}

TEST_CASE("Height field: a body resting on it stays, and a snapshot holds the shape", "[physics][heightfield]")
{
    PhysicsWorld world;
    const HeightFieldConstPtr field = MakeField(33, 33, 0.0f, 4.0f, 1.0f, [](u32, u32) { return 16384u; });
    BodyDesc ground;
    ground.settings.type = BodyType::Static;
    const BodyId ground_id = world.CreateBody(ground);
    Collider collider;
    collider.shape = Shape::MakeHeightField(world.HeightFields().Add(field), field->Extent());
    REQUIRE(world.AddCollider(ground_id, collider) == BodyStatus::Ok);
    BodyDesc ball;
    ball.settings.type = BodyType::Dynamic;
    ball.position = Vec3(2, 3.5f, -1);
    const BodyId ball_id = world.CreateBody(ball);
    Collider sphere;
    sphere.shape = Shape::MakeSphere(0.5f);
    REQUIRE(world.AddCollider(ball_id, sphere) == BodyStatus::Ok);
    for (u32 i = 0; i < 240; ++i)
    {
        world.Step();
    }
    // The surface is at 1.0 (16384/65535 * 4); the ball rests on it.
    const Body* body = world.Find(ball_id);
    REQUIRE(body != nullptr);
    CHECK(HfNear(body->position.y, 1.5f, 0.05f));
    CHECK(HfNear(body->position.x, 2.0f, 0.05f));
    // Save and restore into the same world: the shape and the library match.
    bytes::ByteWriter writer;
    world.Snapshot(&writer);
    Vector<byte> bytes;
    for (usize i = 0; i < writer.Size(); ++i)
    {
        bytes.Push(writer.Bytes()[i]);
    }
    CHECK(world.Restore(ByteSpan(bytes.Data(), bytes.Size())) == SnapshotStatus::Ok);
    // A world without the field refuses a snapshot that names it.
    PhysicsWorld other;
    CHECK(other.Restore(ByteSpan(bytes.Data(), bytes.Size())) == SnapshotStatus::LibraryMismatch);
}
