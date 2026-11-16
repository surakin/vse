// Static triangle meshes in the physics module (Phase 25 Day 382): the mesh and its queries, the world's rules,
// contacts with every shape, the internal-edge rule and the cross-compiler hash.
#include <catch2/catch_test_macros.hpp>

#include "lang/limits.h"
#include "lang/scalar.h"
#include "physics/collide.h"
#include "physics/state_hash.h"
#include "physics/triangle_mesh.h"
#include "physics/world.h"
#include "test_hulls.h"
#include "test_values.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;
using engine_placeholder::physics::testing::Rng;

namespace
{

Pose MPose(f32 x, f32 y, f32 z)
{
    Pose p;
    p.position = Vec3(x, y, z);
    return p;
}

bool MNear(f32 a, f32 b, f32 tolerance = 1e-4f)
{
    return Abs(a - b) <= tolerance;
}

// An n by n grid of quads (two triangles each) of the given cell size, centred on the origin, height `height(x, z)`.
template <class Height> TriangleMeshConstPtr Grid(u32 n, f32 cell, Height height)
{
    Vector<Vec3> v;
    const f32 half = cell * static_cast<f32>(n) * 0.5f;
    for (u32 j = 0; j <= n; ++j)
    {
        for (u32 i = 0; i <= n; ++i)
        {
            const f32 x = static_cast<f32>(i) * cell - half;
            const f32 z = static_cast<f32>(j) * cell - half;
            v.Push(Vec3(x, height(x, z), z));
        }
    }
    Vector<u32> idx;
    for (u32 j = 0; j < n; ++j)
    {
        for (u32 i = 0; i < n; ++i)
        {
            const u32 a = j * (n + 1) + i;
            const u32 b = a + 1;
            const u32 d = a + (n + 1);
            const u32 c = d + 1;
            idx.Push(a);
            idx.Push(d);
            idx.Push(c);
            idx.Push(a);
            idx.Push(c);
            idx.Push(b);
        }
    }
    TriangleMeshSpec spec;
    spec.positions = Span<const Vec3>(v.Data(), v.Size());
    spec.indices = Span<const u32>(idx.Data(), idx.Size());
    Validation r = Validation::Ok;
    TriangleMeshConstPtr mesh = TriangleMesh::Create(spec, &r);
    REQUIRE(r == Validation::Ok);
    return mesh;
}

struct MeshWorld
{
    MeshLibrary meshes;
    HullLibrary hulls;
    Shape shape;
    explicit MeshWorld(const TriangleMeshConstPtr& mesh)
    {
        const u32 id = meshes.Add(mesh);
        shape = Shape::MakeMesh(id, mesh->Extent());
    }
    CollideResult Collide(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin,
                          ContactManifold* m) const
    {
        return physics::Collide(a, pa, b, pb, margin, m, &hulls, &meshes);
    }
};

f32 Flat(f32, f32)
{
    return 0.0f;
}

}  // namespace

TEST_CASE("TriangleMesh: refusals, neighbours and box queries", "[physics][mesh]")
{
    Validation r = Validation::Ok;
    TriangleMeshSpec spec;
    CHECK(!TriangleMesh::Create(spec, &r));
    CHECK(r == Validation::OutOfRange);
    const Vec3 p[3] = {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 0, 1)};
    const u32 good[3] = {0, 1, 2};
    spec.positions = Span<const Vec3>(p, 3);
    spec.indices = Span<const u32>(good, 3);
    CHECK(TriangleMesh::Create(spec, &r));
    const u32 bad[3] = {0, 1, 3};
    spec.indices = Span<const u32>(bad, 3);
    CHECK(!TriangleMesh::Create(spec, &r));
    CHECK(r == Validation::OutOfRange);
    const u32 line[3] = {0, 1, 1};
    spec.indices = Span<const u32>(line, 3);
    CHECK(!TriangleMesh::Create(spec, &r));
    CHECK(r == Validation::NotPositive);
    const Vec3 nan[3] = {Vec3(0, 0, 0), Vec3(Limits<f32>::QuietNaN(), 0, 0), Vec3(0, 0, 1)};
    spec.positions = Span<const Vec3>(nan, 3);
    spec.indices = Span<const u32>(good, 3);
    CHECK(!TriangleMesh::Create(spec, &r));
    CHECK(r == Validation::NotFinite);

    const TriangleMeshConstPtr grid = Grid(8, 1.0f, Flat);
    CHECK(grid->TriangleCount() == 128);
    u32 borders = 0;
    u32 inner = 0;
    for (u32 t = 0; t < grid->TriangleCount(); ++t)
    {
        CHECK(MNear(grid->Normal(t).y, 1.0f));
        for (u32 e = 0; e < 3; ++e)
        {
            (grid->Neighbor(t, e) == kNoTriangle ? borders : inner) += 1;
            if (grid->Neighbor(t, e) != kNoTriangle)
            {
                // The neighbour names this triangle back.
                bool back = false;
                for (u32 f = 0; f < 3; ++f)
                {
                    back = back || grid->Neighbor(grid->Neighbor(t, e), f) == t;
                }
                CHECK(back);
            }
        }
    }
    CHECK(borders == 32);  // the rim of the 8 by 8 grid
    // Box queries agree with a scan of every triangle, and come back in increasing order.
    Rng rng(382);
    for (u32 i = 0; i < 300; ++i)
    {
        const Vec3 c(rng.Range(-5, 5), rng.Range(-1, 1), rng.Range(-5, 5));
        const Vec3 h(rng.Range(0.1f, 2), rng.Range(0.1f, 1), rng.Range(0.1f, 2));
        const Aabb box = Aabb::FromMinMax(c - h, c + h);
        Vector<u32> found;
        grid->QueryBox(box, &found);
        Vector<u32> expected;
        for (u32 t = 0; t < grid->TriangleCount(); ++t)
        {
            Vec3 lo = grid->Vertex(grid->Index(t, 0));
            Vec3 hi = lo;
            for (u32 k = 1; k < 3; ++k)
            {
                const Vec3& q = grid->Vertex(grid->Index(t, k));
                lo = Vec3(FMin(lo.x, q.x), FMin(lo.y, q.y), FMin(lo.z, q.z));
                hi = Vec3(FMax(hi.x, q.x), FMax(hi.y, q.y), FMax(hi.z, q.z));
            }
            if (Overlaps(Aabb::FromMinMax(lo, hi), box))
            {
                expected.Push(t);
            }
        }
        REQUIRE(found.Size() == expected.Size());
        for (usize k = 0; k < found.Size(); ++k)
        {
            REQUIRE(found[k] == expected[k]);
        }
    }
}

TEST_CASE("Mesh shape: static bodies only, a known mesh with a covering extent", "[physics][mesh]")
{
    PhysicsWorld world;
    const TriangleMeshConstPtr grid = Grid(4, 1.0f, Flat);
    BodyDesc desc;
    desc.settings.type = BodyType::Static;
    const BodyId floor = world.CreateBody(desc);
    Collider collider;
    collider.shape = Shape::MakeMesh(1, grid->Extent());
    CHECK(world.AddCollider(floor, collider) == BodyStatus::Invalid);  // not in the library
    const u32 id = world.Meshes().Add(grid);
    collider.shape = Shape::MakeMesh(id, grid->Extent());
    CHECK(Shape::MakeMesh(0, grid->Extent()).Validate() == Validation::OutOfRange);
    Collider small = collider;
    small.shape = Shape::MakeMesh(id, grid->Extent() * 0.5f);
    CHECK(world.AddCollider(floor, small) == BodyStatus::Invalid);
    CHECK(world.AddCollider(floor, collider) == BodyStatus::Ok);
    // A dynamic body takes no mesh, and a body that holds one cannot become dynamic.
    BodyDesc dynamic;
    dynamic.settings.type = BodyType::Dynamic;
    const BodyId ball = world.CreateBody(dynamic);
    CHECK(world.AddCollider(ball, collider) == BodyStatus::Invalid);
    RigidBody settings = desc.settings;
    settings.type = BodyType::Dynamic;
    CHECK(world.SetSettings(floor, settings) == BodyStatus::Invalid);
    settings.type = BodyType::Kinematic;
    CHECK(world.SetSettings(floor, settings) == BodyStatus::Invalid);
    // No mass.
    CHECK(MassFromDensity(collider.shape, 1.0f).mass == 0.0f);
}

TEST_CASE("Collide mesh: shapes resting on a flat mesh", "[physics][mesh]")
{
    const MeshWorld world(Grid(10, 1.0f, Flat));
    ContactManifold m;
    // A box lying on the floor, sunk 0.05, centred on a seam: four points, normal up.
    const Shape box = Shape::MakeBox(Vec3(0.4f, 0.5f, 0.4f));
    REQUIRE(world.Collide(world.shape, MPose(0, 0, 0), box, MPose(0.3f, 0.45f, 0.2f), 0.02f, &m) ==
            CollideResult::Contact);
    CHECK(MNear(m.normal.y, 1.0f, 1e-3f));
    CHECK(m.count == 4);
    for (u32 i = 0; i < m.count; ++i)
    {
        CHECK(MNear(m.points[i].depth, 0.05f, 2e-3f));
    }
    // Symmetric: the box first gives the normal the other way.
    ContactManifold flipped;
    REQUIRE(world.Collide(box, MPose(0.3f, 0.45f, 0.2f), world.shape, MPose(0, 0, 0), 0.02f, &flipped) ==
            CollideResult::Contact);
    CHECK(MNear(flipped.normal.y, -1.0f, 1e-3f));
    CHECK(flipped.count == m.count);
    // A sphere: one point, the normal up, its depth the sink.
    REQUIRE(world.Collide(world.shape, MPose(0, 0, 0), Shape::MakeSphere(0.5f), MPose(0.3f, 0.45f, 0.2f), 0.02f, &m) ==
            CollideResult::Contact);
    CHECK(MNear(m.normal.y, 1.0f, 1e-3f));
    CHECK(m.count == 1);
    CHECK(MNear(m.points[0].depth, 0.05f, 2e-3f));
    // A capsule lying on the floor: contact, normal up.
    const Quaternion lie = Quaternion::FromAxisAngle(Vec3(0, 0, 1), 1.5707963f);
    Pose capsule;
    capsule.position = Vec3(0.1f, 0.28f, 0.0f);
    capsule.rotation = lie;
    REQUIRE(world.Collide(world.shape, MPose(0, 0, 0), Shape::MakeCapsule(0.5f, 0.3f), capsule, 0.02f, &m) ==
            CollideResult::Contact);
    CHECK(MNear(m.normal.y, 1.0f, 1e-3f));
    CHECK(MNear(m.points[0].depth, 0.02f, 3e-3f));
    // A hull cube.
    const ConvexHullConstPtr cube = testing::BoxHull(Vec3(0.5f, 0.5f, 0.5f));
    MeshWorld with_hull(Grid(10, 1.0f, Flat));
    const Shape hull = Shape::MakeHull(with_hull.hulls.Add(cube), cube->Extent());
    REQUIRE(with_hull.Collide(with_hull.shape, MPose(0, 0, 0), hull, MPose(0.2f, 0.48f, 0.1f), 0.02f, &m) ==
            CollideResult::Contact);
    CHECK(MNear(m.normal.y, 1.0f, 1e-3f));
    CHECK(m.count == 4);
    // Apart by more than the margin, and above the whole mesh: nothing.
    CHECK(world.Collide(world.shape, MPose(0, 0, 0), box, MPose(0, 1.0f, 0), 0.02f, &m) == CollideResult::None);
    CHECK(world.Collide(world.shape, MPose(0, 0, 0), box, MPose(100, 0.3f, 0), 0.02f, &m) == CollideResult::None);
    // Speculative: 1 cm above.
    REQUIRE(world.Collide(world.shape, MPose(0, 0, 0), box, MPose(0, 0.51f, 0), 0.02f, &m) == CollideResult::Contact);
    CHECK(MNear(m.points[0].depth, -0.01f, 2e-3f));
    // Two meshes: unsupported. An unknown mesh: nothing.
    CHECK(world.Collide(world.shape, MPose(0, 0, 0), world.shape, MPose(0, 0, 0), 0.02f, &m) ==
          CollideResult::Unsupported);
    CHECK(Collide(world.shape, MPose(0, 0, 0), box, MPose(0, 0.4f, 0), 0.02f, &m) == CollideResult::None);
}

TEST_CASE("Collide mesh: the internal-edge rule keeps the normal on a seam", "[physics][mesh]")
{
    // A single big quad of two triangles, so the diagonal is the only interior edge, and a tilted mesh as well.
    const MeshWorld flat(Grid(2, 4.0f, Flat));
    ContactManifold m;
    u32 wrong = 0;
    for (u32 i = 0; i < 400; ++i)
    {
        const f32 x = -1.5f + 3.0f * static_cast<f32>(i) / 399.0f;
        // A sphere, a box and a capsule sliding across the diagonal of the first cell (z = -x there).
        const Shape shapes[3] = {Shape::MakeSphere(0.5f), Shape::MakeBox(Vec3(0.3f, 0.3f, 0.3f)),
                                 Shape::MakeCapsule(0.3f, 0.2f)};
        const f32 heights[3] = {0.48f, 0.28f, 0.48f};
        for (u32 s = 0; s < 3; ++s)
        {
            const Pose pose = MPose(x, heights[s], -x * 0.7f);
            if (flat.Collide(flat.shape, MPose(0, 0, 0), shapes[s], pose, 0.02f, &m) != CollideResult::Contact ||
                !MNear(m.normal.y, 1.0f, 2e-3f) || !MNear(m.points[0].depth, 0.02f, 4e-3f))
            {
                ++wrong;
            }
        }
    }
    CHECK(wrong == 0);
    // A tilted plane: the normal is the plane's, across the seam as well.
    const f32 slope = 0.5f;
    const MeshWorld ramp(Grid(2, 4.0f, [slope](f32 x, f32) { return slope * x; }));
    const Vec3 up = Vec3(-slope, 1.0f, 0.0f) * (1.0f / Sqrt(1.0f + slope * slope));
    u32 bad_ramp = 0;
    for (u32 i = 0; i < 200; ++i)
    {
        const f32 z = -1.5f + 3.0f * static_cast<f32>(i) / 199.0f;
        const Vec3 on_plane(0.2f, slope * 0.2f, z);
        const Pose pose = MPose(on_plane.x + up.x * 0.48f, on_plane.y + up.y * 0.48f, z * 0.8f);
        if (ramp.Collide(ramp.shape, MPose(0, 0, 0), Shape::MakeSphere(0.5f), pose, 0.02f, &m) !=
                CollideResult::Contact ||
            m.normal.Dot(up) < 0.999f)
        {
            ++bad_ramp;
        }
    }
    CHECK(bad_ramp == 0);
}

TEST_CASE("Collide mesh: a corner, a wall and a big mesh", "[physics][mesh]")
{
    ContactManifold m;
    // A bowl: heights rise with the distance from the centre; a sphere at the bottom touches its triangles around.
    const MeshWorld bowl(Grid(16, 0.5f, [](f32 x, f32 z) { return 0.15f * (x * x + z * z); }));
    REQUIRE(bowl.Collide(bowl.shape, MPose(0, 0, 0), Shape::MakeSphere(0.3f), MPose(0, 0.28f, 0), 0.02f, &m) ==
            CollideResult::Contact);
    CHECK(m.normal.y > 0.99f);
    // Outside the bowl's rim (past its border) the sphere does not touch.
    CHECK(bowl.Collide(bowl.shape, MPose(0, 0, 0), Shape::MakeSphere(0.3f), MPose(9, 0.0f, 0), 0.02f, &m) ==
          CollideResult::None);
    // A mesh at a moved and turned pose: the same contact seen through the pose.
    const MeshWorld flat(Grid(4, 2.0f, Flat));
    Pose turned;
    turned.position = Vec3(5, 2, -3);
    turned.rotation = Quaternion::FromAxisAngle(Vec3(0, 0, 1), 1.5707963f);  // the floor becomes a wall at x = 5 - ...
    const Vec3 wall_normal = turned.rotation.RotateVector(Vec3(0, 1, 0));
    const Vec3 at = turned.position + wall_normal * 0.28f;
    REQUIRE(flat.Collide(flat.shape, turned, Shape::MakeSphere(0.3f), MPose(at.x, at.y, at.z), 0.02f, &m) ==
            CollideResult::Contact);
    CHECK(m.normal.Dot(wall_normal) > 0.999f);
    // 100,000 triangles build, and a small query touches a handful of them.
    const TriangleMeshConstPtr big = Grid(224, 0.1f,
                                          [](f32 x, f32 z) { return 0.05f * Sin(x * 3.0f) * Cos(z * 2.0f); });
    CHECK(big->TriangleCount() == 224 * 224 * 2);
    Vector<u32> found;
    big->QueryBox(Aabb::FromMinMax(Vec3(0, -1, 0), Vec3(0.3f, 1, 0.3f)), &found);
    CHECK(found.Size() > 0);
    CHECK(found.Size() < 60);
}

namespace
{

u64 MeshHash()
{
    const TriangleMeshConstPtr mesh = Grid(12, 0.7f, [](f32 x, f32 z) { return 0.3f * Sin(x) * Cos(z * 1.3f); });
    MeshWorld world(mesh);
    const ConvexHullConstPtr cube = testing::BoxHull(Vec3(0.4f, 0.3f, 0.5f));
    const Shape hull = Shape::MakeHull(world.hulls.Add(cube), cube->Extent());
    Rng rng(382);
    StateHasher out;
    for (u32 i = 0; i < 20000; ++i)
    {
        Shape other;
        const u32 kind = rng.Next() % 4;
        if (kind == 0)
        {
            other = Shape::MakeSphere(rng.Range(0.2f, 0.7f));
        }
        else if (kind == 1)
        {
            const f32 hh = rng.Range(0.0f, 0.8f);
            const f32 rr = rng.Range(0.15f, 0.4f);
            other = Shape::MakeCapsule(hh, rr);
        }
        else if (kind == 2)
        {
            const f32 hx = rng.Range(0.2f, 0.8f);
            const f32 hy = rng.Range(0.2f, 0.8f);
            const f32 hz = rng.Range(0.2f, 0.8f);
            other = Shape::MakeBox(Vec3(hx, hy, hz));
        }
        else
        {
            other = hull;
        }
        Pose pose;
        const f32 px = rng.Range(-3, 3);
        const f32 py = rng.Range(-0.5f, 1.2f);
        const f32 pz = rng.Range(-3, 3);
        pose.position = Vec3(px, py, pz);
        const f32 ax = rng.Range(-1, 1);
        const f32 ay = rng.Range(-1, 1);
        const f32 az = rng.Range(-1, 1);
        const f32 angle = rng.Range(0, 6.28f);
        const Vec3 axis = Vec3(ax, ay, az + 0.01f);
        pose.rotation = Quaternion::FromAxisAngle(axis * (1.0f / Sqrt(axis.LengthSquared())), angle);
        ContactManifold m;
        const CollideResult r = world.Collide(world.shape, MPose(0, 0, 0), other, pose, 0.02f, &m);
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
                out.AddF32(m.points[k].position_b.x);
                out.AddF32(m.points[k].position_b.y);
                out.AddF32(m.points[k].position_b.z);
                out.AddU32(m.points[k].feature_id);
            }
        }
    }
    return out.Value();
}

}  // namespace

TEST_CASE("Collide mesh: random contacts are sane (unit normal, depth identity, separable)", "[physics][mesh][random]")
{
    const TriangleMeshConstPtr mesh = Grid(12, 0.7f, [](f32 x, f32 z) { return 0.3f * Sin(x) * Cos(z * 1.3f); });
    MeshWorld world(mesh);
    Rng rng(99);
    u32 contacts = 0;
    for (u32 i = 0; i < 20000; ++i)
    {
        const f32 rad = rng.Range(0.2f, 0.6f);
        const f32 px = rng.Range(-3, 3);
        const f32 pz = rng.Range(-3, 3);
        const f32 py = rng.Range(-0.2f, 1.0f);
        ContactManifold m;
        if (world.Collide(world.shape, MPose(0, 0, 0), Shape::MakeSphere(rad), MPose(px, py, pz), 0.02f, &m) !=
            CollideResult::Contact)
        {
            continue;
        }
        ++contacts;
        REQUIRE(MNear(m.normal.LengthSquared(), 1.0f, 1e-3f));
        for (u32 k = 0; k < m.count; ++k)
        {
            REQUIRE(MNear((m.points[k].position_a - m.points[k].position_b).Dot(m.normal), m.points[k].depth, 3e-4f));
            REQUIRE(m.points[k].depth >= -0.02f - 1e-3f);
        }
    }
    CHECK(contacts > 3000);
}

TEST_CASE("Collide mesh: the contacts of 20000 random shapes hash to the committed constant", "[physics][mesh][hash]")
{
    CHECK(MeshHash() == 0x716573da705d5b3cull);
}
