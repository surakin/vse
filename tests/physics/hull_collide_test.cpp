// Convex hulls in the physics module (Phase 25 Day 393): the hull data, the hull shape and its mass, GJK and EPA
// against the closed forms, face manifolds and the cross-compiler hash.
#include <catch2/catch_test_macros.hpp>

#include "lang/scalar.h"
#include "math/math_util.h"
#include "physics/collide.h"
#include "physics/collide_math.h"
#include "physics/mass.h"
#include "physics/state_hash.h"
#include "physics/world.h"
#include "test_hulls.h"
#include "test_values.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;
using engine_placeholder::physics::testing::Rng;

namespace
{

Pose HPose(f32 x, f32 y, f32 z)
{
    Pose p;
    p.position = Vec3(x, y, z);
    return p;
}

Pose HPose(const Vec3& position, const Quaternion& rotation)
{
    Pose p;
    p.position = position;
    p.rotation = rotation;
    return p;
}

bool HNear(f32 a, f32 b, f32 tolerance = 1e-4f)
{
    return Abs(a - b) <= tolerance;
}

bool HNearV(const Vec3& a, const Vec3& b, f32 tolerance = 1e-4f)
{
    return HNear(a.x, b.x, tolerance) && HNear(a.y, b.y, tolerance) && HNear(a.z, b.z, tolerance);
}

f32 HMaxDepth(const ContactManifold& m)
{
    f32 d = m.points[0].depth;
    for (u32 i = 1; i < m.count; ++i)
    {
        d = FMax(d, m.points[i].depth);
    }
    return d;
}

bool HHasPointA(const ContactManifold& m, const Vec3& position, f32 tolerance = 1e-4f)
{
    for (u32 i = 0; i < m.count; ++i)
    {
        if (HNearV(m.points[i].position_a, position, tolerance))
        {
            return true;
        }
    }
    return false;
}

// A world with the hulls the tests share; ids are kept in the order added.
struct HullSet
{
    HullLibrary library;
    u32 cube = 0;  // half extents 0.5
    u32 tetra = 0;
    u32 octa = 0;
    u32 pyramid = 0;
    u32 slab = 0;  // half extents 1, 0.2, 0.7
    Shape cube_shape;
    Shape tetra_shape;
    Shape octa_shape;
    Shape pyramid_shape;
    Shape slab_shape;

    static ConvexHullConstPtr Octahedron()
    {
        Vector<Vec3> v;
        v.Push(Vec3(1, 0, 0));
        v.Push(Vec3(-1, 0, 0));
        v.Push(Vec3(0, 1, 0));
        v.Push(Vec3(0, -1, 0));
        v.Push(Vec3(0, 0, 1));
        v.Push(Vec3(0, 0, -1));
        Vector<testing::TestFace> f;
        const u16 tri[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
        for (u32 i = 0; i < 8; ++i)
        {
            testing::TestFace t;
            t.v.Push(tri[i][0]);
            t.v.Push(tri[i][1]);
            t.v.Push(tri[i][2]);
            f.Push(Move(t));
        }
        return testing::MakeHullAuto(v, f);
    }

    static ConvexHullConstPtr Pyramid()
    {
        Vector<Vec3> v;
        v.Push(Vec3(-1, 0, -1));
        v.Push(Vec3(1, 0, -1));
        v.Push(Vec3(1, 0, 1));
        v.Push(Vec3(-1, 0, 1));
        v.Push(Vec3(0, 1, 0));
        Vector<testing::TestFace> f;
        f.Push(testing::Quad(0, 1, 2, 3));
        const u16 tri[4][3] = {{1, 0, 4}, {2, 1, 4}, {3, 2, 4}, {0, 3, 4}};
        for (u32 i = 0; i < 4; ++i)
        {
            testing::TestFace t;
            t.v.Push(tri[i][0]);
            t.v.Push(tri[i][1]);
            t.v.Push(tri[i][2]);
            f.Push(Move(t));
        }
        return testing::MakeHullAuto(v, f);
    }

    HullSet()
    {
        const ConvexHullConstPtr c = testing::BoxHull(Vec3(0.5f, 0.5f, 0.5f));
        const ConvexHullConstPtr t = testing::TetraHull();
        const ConvexHullConstPtr o = Octahedron();
        const ConvexHullConstPtr p = Pyramid();
        const ConvexHullConstPtr s = testing::BoxHull(Vec3(1.0f, 0.2f, 0.7f));
        cube = library.Add(c);
        tetra = library.Add(t);
        octa = library.Add(o);
        pyramid = library.Add(p);
        slab = library.Add(s);
        cube_shape = Shape::MakeHull(cube, c->Extent());
        tetra_shape = Shape::MakeHull(tetra, t->Extent());
        octa_shape = Shape::MakeHull(octa, o->Extent());
        pyramid_shape = Shape::MakeHull(pyramid, p->Extent());
        slab_shape = Shape::MakeHull(slab, s->Extent());
    }

    Shape Pick(u32 k) const
    {
        switch (k % 5)
        {
        case 0: return cube_shape;
        case 1: return tetra_shape;
        case 2: return octa_shape;
        case 3: return pyramid_shape;
        default: return slab_shape;
        }
    }
};

Vec3 HRandVec(Rng& rng, f32 lo, f32 hi)
{
    const f32 x = rng.Range(lo, hi);
    const f32 y = rng.Range(lo, hi);
    const f32 z = rng.Range(lo, hi);
    return Vec3(x, y, z);
}

Pose HRandPose(Rng& rng, f32 spread)
{
    Pose p;
    p.position = HRandVec(rng, -spread, spread);
    p.rotation = testing::RandomTurn(rng);
    return p;
}

}  // namespace

TEST_CASE("ConvexHull: bad numbers are refused with a reason", "[physics][hull]")
{
    const ConvexHullConstPtr good = testing::BoxHull(Vec3(1, 1, 1));
    REQUIRE(good);
    CHECK(good->VertexCount() == 8);
    CHECK(good->FaceCount() == 6);
    CHECK(HNear(good->Volume(), 8.0f));
    CHECK(HNearV(good->Extent(), Vec3(1, 1, 1)));

    Vector<Vec3> v;
    for (u32 i = 0; i < 4; ++i)
    {
        v.Push(Vec3(static_cast<f32>(i), static_cast<f32>(i % 2), static_cast<f32>(i / 2)));
    }
    Vector<Vec3> n;
    Vector<f32> d;
    Vector<u32> first;
    Vector<u32> count;
    Vector<u16> idx;
    for (u32 f = 0; f < 4; ++f)
    {
        n.Push(Vec3(0, 1, 0));
        d.Push(0.0f);
        first.Push(f * 3);
        count.Push(3);
        idx.Push(0);
        idx.Push(1);
        idx.Push(2);
    }
    ConvexHullSpec spec;
    spec.vertices = Span<const Vec3>(v.Data(), v.Size());
    spec.normals = Span<const Vec3>(n.Data(), n.Size());
    spec.offsets = Span<const f32>(d.Data(), d.Size());
    spec.face_first = Span<const u32>(first.Data(), first.Size());
    spec.face_count = Span<const u32>(count.Data(), count.Size());
    spec.indices = Span<const u16>(idx.Data(), idx.Size());
    spec.volume = 1.0f;
    Validation r = Validation::Ok;
    CHECK(ConvexHull::Create(spec, &r));  // a made-up but consistent spec is accepted
    spec.volume = 0.0f;
    CHECK(!ConvexHull::Create(spec, &r));
    CHECK(r == Validation::NotPositive);
    spec.volume = 1.0f;
    idx[4] = 9;
    CHECK(!ConvexHull::Create(spec, &r));
    CHECK(r == Validation::OutOfRange);
    idx[4] = 1;
    n[2] = Vec3(0, 2, 0);
    CHECK(!ConvexHull::Create(spec, &r));
    CHECK(r == Validation::NotNormalized);
    n[2] = Vec3(0, 1, 0);
    v[1] = Vec3(testing::kNaN, 0, 0);
    CHECK(!ConvexHull::Create(spec, &r));
    CHECK(r == Validation::NotFinite);
    v[1] = Vec3(1, 0, 0);
    spec.vertices = Span<const Vec3>(v.Data(), 3);
    CHECK(!ConvexHull::Create(spec, &r));  // fewer than four vertices
}

TEST_CASE("Hull shape: the world refuses an unknown hull and a cube hull has the mass of a box", "[physics][hull]")
{
    PhysicsWorld world;
    const ConvexHullConstPtr cube = testing::BoxHull(Vec3(0.5f, 1.0f, 1.5f));
    const Shape missing = Shape::MakeHull(1, cube->Extent());
    BodyDesc desc;
    desc.settings.type = BodyType::Dynamic;
    const BodyId body = world.CreateBody(desc);
    Collider collider;
    collider.shape = missing;
    CHECK(world.AddCollider(body, collider) == BodyStatus::Invalid);
    const u32 id = world.Hulls().Add(cube);
    REQUIRE(id == 1);
    collider.shape = Shape::MakeHull(id, cube->Extent());
    CHECK(collider.shape.Validate() == Validation::Ok);
    CHECK(Shape::MakeHull(0, cube->Extent()).Validate() == Validation::OutOfRange);
    // An extent that does not cover the hull would make the broad phase miss it: refused.
    Collider small = collider;
    small.shape = Shape::MakeHull(id, cube->Extent() * 0.5f);
    CHECK(world.AddCollider(body, small) == BodyStatus::Invalid);
    CHECK(world.AddCollider(body, collider) == BodyStatus::Ok);
    CHECK(world.SetCollider(body, small) == BodyStatus::Invalid);

    const f32 density = 200.0f;
    const MassProperties hull_mass = MassFromDensity(collider.shape, density, &world.Hulls());
    const MassProperties box_mass = MassFromDensity(Shape::MakeBox(Vec3(0.5f, 1.0f, 1.5f)), density);
    CHECK(HNear(hull_mass.volume, box_mass.volume, 1e-3f));
    CHECK(HNear(hull_mass.mass, box_mass.mass, 0.05f));
    CHECK(HNearV(hull_mass.inertia, box_mass.inertia, 0.5f));
    CHECK(HNearV(hull_mass.center, Vec3(0, 0, 0), 1e-5f));
    // Without the library a hull has no mass.
    CHECK(MassFromDensity(collider.shape, density).mass == 0.0f);

    // An off-centre hull: the centre of mass is its centroid.
    const ConvexHullConstPtr off = testing::BoxHull(Vec3(0.5f, 0.5f, 0.5f), Vec3(2.0f, 0.0f, 0.0f));
    const u32 off_id = world.Hulls().Add(off);
    const MassProperties off_mass = MassFromDensity(Shape::MakeHull(off_id, off->Extent()), density, &world.Hulls());
    CHECK(HNearV(off_mass.center, Vec3(2, 0, 0), 1e-4f));
    CHECK(HNear(off_mass.mass, 200.0f, 0.05f));
    // A tetrahedron's tensor has products of inertia: its principal frame is turned, and the principal moments still
    // add up (the trace of the tensor is invariant).
    const ConvexHullConstPtr tetra = testing::TetraHull();
    const u32 tetra_id = world.Hulls().Add(tetra);
    const MassProperties tm = MassFromDensity(Shape::MakeHull(tetra_id, tetra->Extent()), 1.0f, &world.Hulls());
    CHECK(HNear(tm.mass, 1.0f / 6.0f, 1e-5f));
    CHECK(HNearV(tm.center, Vec3(0.25f, 0.25f, 0.25f), 1e-5f));
    const f32 trace = tetra->Inertia().col[0].x + tetra->Inertia().col[1].y + tetra->Inertia().col[2].z;
    CHECK(HNear(tm.inertia.x + tm.inertia.y + tm.inertia.z, trace, 1e-5f));
    CHECK(HNear(tm.principal_rotation.LengthSquared(), 1.0f, 1e-4f));
    // The world's mass picks up the hull's centre and frame.
    BodyDesc dyn;
    dyn.settings.type = BodyType::Dynamic;
    const BodyId dyn_body = world.CreateBody(dyn);
    Collider tc;
    tc.shape = Shape::MakeHull(off_id, off->Extent());
    tc.offset = Vec3(0, 1, 0);
    REQUIRE(world.AddCollider(dyn_body, tc) == BodyStatus::Ok);
    const Body* record = world.Find(dyn_body);
    REQUIRE(record != nullptr);
    CHECK(HNearV(record->mass.center, Vec3(2, 1, 0), 1e-4f));
}

TEST_CASE("Collide hull: a cube hull on a cube hull makes four corner contacts", "[physics][hull]")
{
    const HullSet hulls;
    ContactManifold m;
    REQUIRE(Collide(hulls.cube_shape, HPose(0, 0, 0), hulls.cube_shape, HPose(0, 0.9f, 0), 0.0f, &m, &hulls.library) ==
            CollideResult::Contact);
    CHECK(m.count == 4);
    CHECK(HNearV(m.normal, Vec3(0, 1, 0)));
    for (u32 i = 0; i < m.count; ++i)
    {
        CHECK(HNear(m.points[i].depth, 0.1f));
        CHECK(HNear((m.points[i].position_a - m.points[i].position_b).Dot(m.normal), m.points[i].depth));
    }
    CHECK(HHasPointA(m, Vec3(0.5f, 0.5f, 0.5f)));
    CHECK(HHasPointA(m, Vec3(-0.5f, 0.5f, -0.5f)));
    // Shifted: the overlap rectangle.
    REQUIRE(Collide(hulls.cube_shape, HPose(0, 0, 0), hulls.cube_shape, HPose(0.3f, 0.9f, 0), 0.0f, &m,
                    &hulls.library) == CollideResult::Contact);
    CHECK(m.count == 4);
    CHECK(HHasPointA(m, Vec3(-0.2f, 0.5f, 0.5f)));
    CHECK(HHasPointA(m, Vec3(0.5f, 0.5f, -0.5f)));
    // Apart by more than the margin, and within it.
    CHECK(Collide(hulls.cube_shape, HPose(0, 0, 0), hulls.cube_shape, HPose(0, 1.05f, 0), 0.02f, &m, &hulls.library) ==
          CollideResult::None);
    REQUIRE(Collide(hulls.cube_shape, HPose(0, 0, 0), hulls.cube_shape, HPose(0, 1.01f, 0), 0.02f, &m,
                    &hulls.library) == CollideResult::Contact);
    CHECK(HNear(HMaxDepth(m), -0.01f, 1e-3f));
    // A hull that is not in the library: nothing.
    CHECK(Collide(Shape::MakeHull(77, Vec3(1, 1, 1)), HPose(0, 0, 0), hulls.cube_shape, HPose(0, 0, 0), 0.0f, &m,
                  &hulls.library) == CollideResult::None);
    CHECK(Collide(hulls.cube_shape, HPose(0, 0, 0), hulls.cube_shape, HPose(0, 0, 0), 0.0f, &m, nullptr) ==
          CollideResult::None);
}

TEST_CASE("Collide hull: a pyramid, a tilted slab and a sphere against hulls", "[physics][hull]")
{
    const HullSet hulls;
    ContactManifold m;
    // A pyramid standing on a slab: the base (2 by 2) sinks 0.05 into the slab's top (y = 0.2): four points.
    REQUIRE(Collide(hulls.slab_shape, HPose(0, 0, 0), hulls.pyramid_shape, HPose(0, 0.15f, 0), 0.0f, &m,
                    &hulls.library) == CollideResult::Contact);
    CHECK(HNearV(m.normal, Vec3(0, 1, 0), 1e-3f));
    CHECK(m.count == 4);
    CHECK(HNear(HMaxDepth(m), 0.05f, 1e-3f));
    // The pyramid's tip down onto the slab: a single point.
    const Quaternion flip = Quaternion::FromAxisAngle(Vec3(1, 0, 0), 3.14159265f);
    REQUIRE(Collide(hulls.slab_shape, HPose(0, 0, 0), hulls.pyramid_shape, HPose(Vec3(0, 1.15f, 0), flip), 0.0f, &m,
                    &hulls.library) == CollideResult::Contact);
    CHECK(m.count == 1);
    CHECK(HNearV(m.normal, Vec3(0, 1, 0), 1e-3f));
    CHECK(HNear(m.points[0].depth, 0.05f, 1e-3f));
    CHECK(HNearV(m.points[0].position_a, Vec3(0, 0.2f, 0), 1e-3f));
    // A sphere on the pyramid's apex side: the sphere is shape A.
    const Shape ball = Shape::MakeSphere(0.25f);
    REQUIRE(Collide(ball, HPose(0, 1.2f, 0), hulls.slab_shape, HPose(0, 0, 0), 0.0f, &m, &hulls.library) ==
            CollideResult::None);
    REQUIRE(Collide(ball, HPose(0, 0.4f, 0), hulls.slab_shape, HPose(0, 0, 0), 0.0f, &m, &hulls.library) ==
            CollideResult::Contact);
    CHECK(m.count == 1);
    CHECK(HNearV(m.normal, Vec3(0, -1, 0), 1e-4f));
    CHECK(HNear(m.points[0].depth, 0.05f, 1e-4f));
    // A sphere whose centre is inside the hull: out through the nearest face.
    REQUIRE(Collide(hulls.cube_shape, HPose(0, 0, 0), ball, HPose(0.3f, 0, 0), 0.0f, &m, &hulls.library) ==
            CollideResult::Contact);
    CHECK(HNearV(m.normal, Vec3(1, 0, 0), 1e-3f));
    CHECK(HNear(m.points[0].depth, 0.5f - 0.3f + 0.25f, 1e-3f));
}

TEST_CASE("Collide hull: a hull agrees with the primitive it copies (random pairs)", "[physics][hull][random]")
{
    // A box hull against sphere, capsule and box: the same answer as the closed forms (EPA and GJK against the
    // separating axis and the clamps), up to the contact margin's boundary.
    Rng rng(381);
    u32 both = 0;
    u32 mismatched_existence = 0;
    u32 worst_depth_cases = 0;
    u32 shallow = 0;
    for (u32 i = 0; i < 20000; ++i)
    {
        const Vec3 half = HRandVec(rng, 0.2f, 1.0f);
        const ConvexHullConstPtr hull = testing::BoxHull(half);
        HullLibrary library;
        const Shape as_hull = Shape::MakeHull(library.Add(hull), hull->Extent());
        const Shape as_box = Shape::MakeBox(half);
        Shape other;
        const u32 kind = rng.Next() % 3;
        if (kind == 0)
        {
            other = Shape::MakeSphere(rng.Range(0.2f, 0.8f));
        }
        else if (kind == 1)
        {
            const f32 hh = rng.Range(0.0f, 1.0f);
            const f32 rr = rng.Range(0.15f, 0.5f);
            other = Shape::MakeCapsule(hh, rr);
        }
        else
        {
            other = Shape::MakeBox(HRandVec(rng, 0.2f, 1.0f));
        }
        const Pose pa = HRandPose(rng, 0.6f);
        const Pose pb = HRandPose(rng, 1.2f);
        ContactManifold mh;
        ContactManifold mb;
        const CollideResult rh = Collide(as_hull, pa, other, pb, 0.02f, &mh, &library);
        const CollideResult rb = Collide(as_box, pa, other, pb, 0.02f, &mb, nullptr);
        if (rh == CollideResult::Contact && rb == CollideResult::Contact)
        {
            ++both;
            REQUIRE(HNear(mh.normal.LengthSquared(), 1.0f, 1e-3f));
            for (u32 k = 0; k < mh.count; ++k)
            {
                REQUIRE(HNear((mh.points[k].position_a - mh.points[k].position_b).Dot(mh.normal), mh.points[k].depth,
                              2e-4f));
            }
            // The deepest point agrees while the overlap is shallow. A deep overlap has several ways out (the closed
            // forms pick one by their own rule, EPA the shortest), so only the shallow ones are compared.
            if (HMaxDepth(mb) < 0.1f)
            {
                ++shallow;
                if (!HNear(HMaxDepth(mh), HMaxDepth(mb), 5e-3f))
                {
                    ++worst_depth_cases;
                }
            }
        }
        else if (rh != rb)
        {
            // Only at the edge of the margin may one of them see a contact and the other not.
            const f32 depth = rh == CollideResult::Contact ? HMaxDepth(mh) : HMaxDepth(mb);
            INFO("hull " << static_cast<int>(rh) << " box " << static_cast<int>(rb) << " other kind " << kind
                         << " depth " << depth << " half " << half.x << "," << half.y << "," << half.z << " pa "
                         << pa.position.x << "," << pa.position.y << "," << pa.position.z << " pb " << pb.position.x
                         << "," << pb.position.y << "," << pb.position.z << " qa " << pa.rotation.x << ","
                         << pa.rotation.y << "," << pa.rotation.z << "," << pa.rotation.w << " qb " << pb.rotation.x
                         << "," << pb.rotation.y << "," << pb.rotation.z << "," << pb.rotation.w << " cap "
                         << other.half_height << "," << other.radius);
            REQUIRE(depth < -0.02f + 5e-3f);
            ++mismatched_existence;
        }
    }
    CHECK(both > 3000);
    CHECK(mismatched_existence < 100);
    // The depth of a clipped manifold can differ from the SAT's where the deepest corner is outside the reference face;
    // that is rare.
    CHECK(shallow > 500);
    CHECK(worst_depth_cases * 100 < shallow);
}

TEST_CASE("Collide hull: random hull pairs are symmetric, unit-normal and separable (property)",
          "[physics][hull][random]")
{
    const HullSet hulls;
    Rng rng(382);
    u32 contacts = 0;
    u32 asymmetric = 0;
    u32 unseparated = 0;
    for (u32 i = 0; i < 30000; ++i)
    {
        const Shape a = hulls.Pick(rng.Next());
        const Shape b = hulls.Pick(rng.Next());
        const Pose pa = HRandPose(rng, 0.8f);
        const Pose pb = HRandPose(rng, 1.4f);
        ContactManifold ab;
        ContactManifold ba;
        const CollideResult r1 = Collide(a, pa, b, pb, 0.0f, &ab, &hulls.library);
        const CollideResult r2 = Collide(b, pb, a, pa, 0.0f, &ba, &hulls.library);
        if (r1 != r2)
        {
            // Within a hair of touching, the two orders may disagree.
            const f32 depth = r1 == CollideResult::Contact ? HMaxDepth(ab) : HMaxDepth(ba);
            REQUIRE(depth < 3e-3f);
            continue;
        }
        if (r1 != CollideResult::Contact)
        {
            continue;
        }
        ++contacts;
        REQUIRE(ab.count >= 1);
        REQUIRE(ab.count <= 4);
        REQUIRE(HNear(ab.normal.LengthSquared(), 1.0f, 1e-3f));
        for (u32 k = 0; k < ab.count; ++k)
        {
            REQUIRE(
                HNear((ab.points[k].position_a - ab.points[k].position_b).Dot(ab.normal), ab.points[k].depth, 2e-4f));
        }
        if (!HNear(HMaxDepth(ab), HMaxDepth(ba), 4e-3f))
        {
            ++asymmetric;
        }
        // Moving B along the normal by the depth gets the pair apart (or nearly).
        Pose moved = pb;
        moved.position = pb.position + ab.normal * (HMaxDepth(ab) + 2e-3f);
        ContactManifold after;
        const CollideResult r = Collide(a, pa, b, moved, 0.0f, &after, &hulls.library);
        if (!(r == CollideResult::None || HMaxDepth(after) < 6e-3f))
        {
            ++unseparated;
        }
    }
    CHECK(contacts > 2000);
    CHECK(asymmetric * 200 < contacts);
    CHECK(unseparated * 200 < contacts);
}

TEST_CASE("Collide hull: the feature ids stay the same while a hull slides on a face", "[physics][hull]")
{
    const HullSet hulls;
    ContactManifold first;
    REQUIRE(Collide(hulls.cube_shape, HPose(0, 0, 0), hulls.cube_shape, HPose(0.01f, 0.95f, 0), 0.0f, &first,
                    &hulls.library) == CollideResult::Contact);
    REQUIRE(first.count == 4);
    for (u32 step = 2; step <= 20; ++step)
    {
        ContactManifold m;
        const f32 x = 0.01f * static_cast<f32>(step);
        REQUIRE(Collide(hulls.cube_shape, HPose(0, 0, 0), hulls.cube_shape, HPose(x, 0.95f, 0), 0.0f, &m,
                        &hulls.library) == CollideResult::Contact);
        REQUIRE(m.count == 4);
        for (u32 i = 0; i < 4; ++i)
        {
            bool found = false;
            for (u32 j = 0; j < 4; ++j)
            {
                found = found || m.points[i].feature_id == first.points[j].feature_id;
            }
            REQUIRE(found);
        }
    }
}

TEST_CASE("Collide hull: a prism with a many-cornered cap rests on a cube hull", "[physics][hull]")
{
    HullLibrary library;
    const ConvexHullConstPtr prism = testing::PrismHull(24, 0.4f, 0.5f);
    const ConvexHullConstPtr cube = testing::BoxHull(Vec3(1, 0.5f, 1));
    REQUIRE(prism);
    CHECK(prism->VertexCount() == 48);
    CHECK(prism->FaceCount() == 26);
    const Shape p = Shape::MakeHull(library.Add(prism), prism->Extent());
    const Shape c = Shape::MakeHull(library.Add(cube), cube->Extent());
    ContactManifold m;
    // The prism's bottom cap (radius 0.4) sinks 0.05 into the cube's top face at y = 0.5.
    REQUIRE(Collide(c, HPose(0, 0, 0), p, HPose(0, 1.0f - 0.05f, 0), 0.0f, &m, &library) == CollideResult::Contact);
    CHECK(m.count == 4);
    CHECK(HNearV(m.normal, Vec3(0, 1, 0), 1e-3f));
    for (u32 i = 0; i < m.count; ++i)
    {
        CHECK(HNear(m.points[i].depth, 0.05f, 1e-3f));
    }
    // Hanging over the edge: still a manifold, and the points are on the cube's face.
    REQUIRE(Collide(c, HPose(0, 0, 0), p, HPose(0.8f, 0.95f, 0), 0.0f, &m, &library) == CollideResult::Contact);
    CHECK(m.count >= 2);
    for (u32 i = 0; i < m.count; ++i)
    {
        CHECK(m.points[i].position_a.x <= 1.0f + 1e-4f);
    }
}

namespace
{

u64 HullPairHash()
{
    const HullSet hulls;
    Rng rng(383);
    StateHasher out;
    for (u32 i = 0; i < 30000; ++i)
    {
        const Shape a = hulls.Pick(rng.Next());
        Shape b;
        const u32 kind = rng.Next() % 4;
        if (kind == 0)
        {
            b = Shape::MakeSphere(rng.Range(0.2f, 0.8f));
        }
        else if (kind == 1)
        {
            const f32 hh = rng.Range(0.0f, 1.0f);
            const f32 rr = rng.Range(0.15f, 0.5f);
            b = Shape::MakeCapsule(hh, rr);
        }
        else if (kind == 2)
        {
            b = Shape::MakeBox(HRandVec(rng, 0.2f, 1.0f));
        }
        else
        {
            b = hulls.Pick(rng.Next());
        }
        const Pose pa = HRandPose(rng, 0.8f);
        const Pose pb = HRandPose(rng, 1.4f);
        ContactManifold m;
        const CollideResult r = Collide(a, pa, b, pb, 0.02f, &m, &hulls.library);
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

TEST_CASE("Collide hull: the manifolds of 30000 random hull pairs hash to the committed constant",
          "[physics][hull][hash]")
{
    CHECK(HullPairHash() == 0x172dfbee2611621eull);
}
