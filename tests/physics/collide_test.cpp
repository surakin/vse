// The narrow phase's closed-form tests (Phase 25 Day 379): analytic scenes, symmetry on random pairs and the separation
// property.
#include <catch2/catch_test_macros.hpp>

#include "lang/scalar.h"
#include "math/math_util.h"
#include "physics/collide.h"
#include "physics/state_hash.h"
#include "test_values.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;
using engine_placeholder::physics::testing::Rng;

namespace
{

Pose PoseAt(f32 x, f32 y, f32 z)
{
    Pose p;
    p.position = Vec3(x, y, z);
    return p;
}

Pose PoseAt(const Vec3& position, const Quaternion& rotation)
{
    Pose p;
    p.position = position;
    p.rotation = rotation;
    return p;
}

bool Within(f32 a, f32 b, f32 tolerance = 1e-5f)
{
    return Abs(a - b) <= tolerance;
}

bool WithinV(const Vec3& a, const Vec3& b, f32 tolerance = 1e-5f)
{
    return Within(a.x, b.x, tolerance) && Within(a.y, b.y, tolerance) && Within(a.z, b.z, tolerance);
}

f32 MaxDepth(const ContactManifold& m)
{
    f32 d = m.points[0].depth;
    for (u32 i = 1; i < m.count; ++i)
    {
        d = FMax(d, m.points[i].depth);
    }
    return d;
}

Quaternion Rot(f32 x, f32 y, f32 z, f32 angle)
{
    return Quaternion::FromAxisAngle(Vec3(x, y, z).Normalized(), angle);
}

}  // namespace

TEST_CASE("Collide sphere-sphere: touching, penetrating, apart, speculative and concentric", "[physics][collide]")
{
    const Shape ball = Shape::MakeSphere(0.5f);
    ContactManifold m;
    // Penetrating by 0.1 along x.
    REQUIRE(Collide(ball, PoseAt(0, 0, 0), ball, PoseAt(0.9f, 0, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(m.count == 1);
    CHECK(WithinV(m.normal, Vec3(1, 0, 0)));
    CHECK(Within(m.points[0].depth, 0.1f));
    CHECK(WithinV(m.points[0].position_a, Vec3(0.5f, 0, 0)));
    CHECK(WithinV(m.points[0].position_b, Vec3(0.4f, 0, 0)));
    // Touching: depth 0.
    REQUIRE(Collide(ball, PoseAt(0, 0, 0), ball, PoseAt(1.0f, 0, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(Within(m.points[0].depth, 0.0f));
    // Apart by 0.5: nothing; apart by 0.01 with a margin of 0.02: a speculative contact of depth -0.01; with no margin:
    // nothing.
    CHECK(Collide(ball, PoseAt(0, 0, 0), ball, PoseAt(1.5f, 0, 0), 0.02f, &m) == CollideResult::None);
    REQUIRE(Collide(ball, PoseAt(0, 0, 0), ball, PoseAt(1.01f, 0, 0), 0.02f, &m) == CollideResult::Contact);
    CHECK(Within(m.points[0].depth, -0.01f));
    CHECK(Collide(ball, PoseAt(0, 0, 0), ball, PoseAt(1.01f, 0, 0), 0.0f, &m) == CollideResult::None);
    // Concentric: a normal that is a unit vector and a depth of both radii.
    REQUIRE(Collide(ball, PoseAt(1, 2, 3), ball, PoseAt(1, 2, 3), 0.0f, &m) == CollideResult::Contact);
    CHECK(Within(m.normal.LengthSquared(), 1.0f));
    CHECK(Within(m.points[0].depth, 1.0f));
}

TEST_CASE("Collide sphere-capsule: the side, the cap and a tilted capsule", "[physics][collide]")
{
    const Shape ball = Shape::MakeSphere(0.5f);
    const Shape capsule = Shape::MakeCapsule(1.0f, 0.25f);
    ContactManifold m;
    // The ball beside the middle of a standing capsule: depth 0.5 + 0.25 - 0.7.
    REQUIRE(Collide(ball, PoseAt(0.7f, 0.3f, 0), capsule, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(WithinV(m.normal, Vec3(-1, 0, 0)));
    CHECK(Within(m.points[0].depth, 0.05f));
    // Above the top cap: the closest point is the end of the axis (0, 1, 0).
    REQUIRE(Collide(ball, PoseAt(0, 1.7f, 0), capsule, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(WithinV(m.normal, Vec3(0, -1, 0)));
    CHECK(Within(m.points[0].depth, 0.05f));
    // Capsule lying along x (turned a quarter about z), ball above its middle.
    const Quaternion lie = Rot(0, 0, 1, kPi * 0.5f);
    REQUIRE(Collide(ball, PoseAt(0.3f, 0.7f, 0), capsule, PoseAt(Vec3(0, 0, 0), lie), 0.0f, &m) ==
            CollideResult::Contact);
    CHECK(WithinV(m.normal, Vec3(0, -1, 0)));
    CHECK(Within(m.points[0].depth, 0.05f));
    // Past the end of the lying capsule: clear.
    CHECK(Collide(ball, PoseAt(2.0f, 0, 0), capsule, PoseAt(Vec3(0, 0, 0), lie), 0.0f, &m) == CollideResult::None);
    // The other order is the same contact, flipped.
    ContactManifold flipped;
    REQUIRE(Collide(capsule, PoseAt(0, 0, 0), ball, PoseAt(0.7f, 0.3f, 0), 0.0f, &flipped) == CollideResult::Contact);
    CHECK(WithinV(flipped.normal, Vec3(1, 0, 0)));
    CHECK(Within(flipped.points[0].depth, 0.05f));
}

TEST_CASE("Collide capsule-capsule: crossing, parallel, end to end, collinear overlap and concentric",
          "[physics][collide]")
{
    const Shape capsule = Shape::MakeCapsule(1.0f, 0.25f);
    const Quaternion along_x = Rot(0, 0, 1, kPi * 0.5f);
    const Quaternion along_z = Rot(1, 0, 0, kPi * 0.5f);
    ContactManifold m;
    // An x capsule over a z capsule, 0.4 apart: radii sum 0.5, depth 0.1, normal along y.
    REQUIRE(Collide(capsule, PoseAt(Vec3(0, 0, 0), along_x), capsule, PoseAt(Vec3(0, 0.4f, 0), along_z), 0.0f, &m) ==
            CollideResult::Contact);
    CHECK(m.count == 1);
    CHECK(WithinV(m.normal, Vec3(0, 1, 0)));
    CHECK(Within(m.points[0].depth, 0.1f));
    // 0.7 apart: nothing.
    CHECK(Collide(capsule, PoseAt(Vec3(0, 0, 0), along_x), capsule, PoseAt(Vec3(0, 0.7f, 0), along_z), 0.0f, &m) ==
          CollideResult::None);
    // Two standing capsules side by side, shifted 0.5 in y: parallel, overlapping over 1.5 m: two points.
    REQUIRE(Collide(capsule, PoseAt(0, 0, 0), capsule, PoseAt(0.4f, 0.5f, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(m.count == 2);
    CHECK(WithinV(m.normal, Vec3(1, 0, 0)));
    CHECK(Within(m.points[0].depth, 0.1f));
    CHECK(Within(m.points[1].depth, 0.1f));
    CHECK(m.points[0].feature_id != m.points[1].feature_id);
    CHECK(Within(Abs(m.points[0].position_a.y - m.points[1].position_a.y), 1.5f));
    // End to end, collinear: the tips overlap by 0.1 (axes end 0.4 apart at the touching tips).
    REQUIRE(Collide(capsule, PoseAt(0, 0, 0), capsule, PoseAt(0, 2.4f, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(m.count == 1);
    CHECK(WithinV(m.normal, Vec3(0, 1, 0)));
    CHECK(Within(m.points[0].depth, 0.1f));
    // Collinear and apart: nothing.
    CHECK(Collide(capsule, PoseAt(0, 0, 0), capsule, PoseAt(0, 2.6f, 0), 0.0f, &m) == CollideResult::None);
    // Crossing axes through one point: depth is both radii, the normal is the common perpendicular.
    REQUIRE(Collide(capsule, PoseAt(Vec3(0, 0, 0), along_x), capsule, PoseAt(Vec3(0, 0, 0), along_z), 0.0f, &m) ==
            CollideResult::Contact);
    CHECK(Within(m.normal.LengthSquared(), 1.0f));
    CHECK(Within(Abs(m.normal.y), 1.0f));
    CHECK(Within(m.points[0].depth, 0.5f));
    // Capsules without a cylinder part are spheres.
    const Shape pill = Shape::MakeCapsule(0.0f, 0.5f);
    REQUIRE(Collide(pill, PoseAt(0, 0, 0), pill, PoseAt(0.9f, 0, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(Within(m.points[0].depth, 0.1f));
}

TEST_CASE("Collide sphere-box: face, edge, vertex, rotated, inside and on the surface", "[physics][collide]")
{
    const Shape ball = Shape::MakeSphere(0.5f);
    const Shape box = Shape::MakeBox(Vec3(1, 1, 1));
    ContactManifold m;
    // Above the top face by 1.4: depth 0.1, normal down (from the sphere to the box).
    REQUIRE(Collide(ball, PoseAt(0.3f, 1.4f, -0.2f), box, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(WithinV(m.normal, Vec3(0, -1, 0)));
    CHECK(Within(m.points[0].depth, 0.1f));
    CHECK(WithinV(m.points[0].position_b, Vec3(0.3f, 1, -0.2f)));
    CHECK(WithinV(m.points[0].position_a, Vec3(0.3f, 0.9f, -0.2f)));
    const u32 face_id = m.points[0].feature_id;
    REQUIRE(Collide(ball, PoseAt(-0.4f, 1.45f, 0.5f), box, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(m.points[0].feature_id == face_id);  // the same face, the same feature
    // Next to an edge (x and y beyond): the closest point is on the edge.
    REQUIRE(Collide(ball, PoseAt(1.3f, 1.3f, 0), box, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(Within(m.points[0].depth, 0.5f - Sqrt(0.18f)));
    CHECK(WithinV(m.normal, Vec3(-0.70710678f, -0.70710678f, 0)));
    CHECK(m.points[0].feature_id != face_id);
    // Near a vertex.
    REQUIRE(Collide(ball, PoseAt(1.2f, 1.2f, 1.2f), box, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(Within(m.points[0].depth, 0.5f - Sqrt(0.12f)));
    // A box turned 45 degrees about z: its corner now points along x (reach sqrt 2).
    const Quaternion turned = Rot(0, 0, 1, kPi * 0.25f);
    REQUIRE(Collide(ball, PoseAt(1.8f, 0, 0), box, PoseAt(Vec3(0, 0, 0), turned), 0.0f, &m) == CollideResult::Contact);
    CHECK(Within(m.points[0].depth, 0.5f - (1.8f - 1.41421356f), 2e-5f));
    CHECK(WithinV(m.normal, Vec3(-1, 0, 0), 1e-4f));
    // Apart: nothing; within the margin: a speculative contact.
    CHECK(Collide(ball, PoseAt(0, 1.6f, 0), box, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::None);
    REQUIRE(Collide(ball, PoseAt(0, 1.51f, 0), box, PoseAt(0, 0, 0), 0.02f, &m) == CollideResult::Contact);
    CHECK(Within(m.points[0].depth, -0.01f));
    // The centre inside the box, near the +x face: leaves through it; depth is the radius plus the distance to the
    // face.
    REQUIRE(Collide(ball, PoseAt(0.8f, 0.1f, 0.2f), box, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(WithinV(m.normal, Vec3(-1, 0, 0)));
    CHECK(Within(m.points[0].depth, 0.5f + 0.2f));
    // The centre on the surface: still a contact, depth the radius.
    REQUIRE(Collide(ball, PoseAt(1.0f, 0, 0), box, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::Contact);
    CHECK(Within(m.points[0].depth, 0.5f));
    // The box first: the same contact, flipped.
    ContactManifold flipped;
    REQUIRE(Collide(box, PoseAt(0, 0, 0), ball, PoseAt(0.3f, 1.4f, -0.2f), 0.0f, &flipped) == CollideResult::Contact);
    CHECK(WithinV(flipped.normal, Vec3(0, 1, 0)));
    CHECK(Within(flipped.points[0].depth, 0.1f));
}

TEST_CASE("Collide: pairs without a test are Unsupported and invalid input is nothing", "[physics][collide]")
{
    const Shape box = Shape::MakeBox(Vec3(1, 1, 1));
    const Shape capsule = Shape::MakeCapsule(1.0f, 0.25f);
    ContactManifold m;
    m.count = 7;
    CHECK(Collide(box, PoseAt(0, 0, 0), box, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::Unsupported);
    CHECK(Collide(box, PoseAt(0, 0, 0), capsule, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::Unsupported);
    CHECK(Collide(capsule, PoseAt(0, 0, 0), box, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::Unsupported);
    CHECK(m.count == 7);  // left alone
    CHECK(Collide(Shape::MakeSphere(-1.0f), PoseAt(0, 0, 0), box, PoseAt(0, 0, 0), 0.0f, &m) == CollideResult::None);
    CHECK(Collide(Shape::MakeSphere(1.0f), PoseAt(0, 0, 0), Shape::MakeSphere(1.0f), PoseAt(0, 0, 0), -1.0f, &m) ==
          CollideResult::None);
    const Shape ball = Shape::MakeSphere(1.0f);
    CHECK(Collide(ball, PoseAt(0, 0, 0), ball, PoseAt(50, 0, 0), testing::kNaN, &m) == CollideResult::None);
    CHECK(Collide(ball, PoseAt(0, 0, 0), ball, PoseAt(50, 0, 0), __builtin_inff(), &m) == CollideResult::None);
}

TEST_CASE("CombineMaterials: the rule with the larger number, whatever the order", "[physics][collide]")
{
    PhysicsMaterial a;
    a.friction = 0.8f;
    a.restitution = 0.1f;
    PhysicsMaterial b;
    b.friction = 0.2f;
    b.restitution = 0.5f;
    b.friction_rule = CombineRule::Multiply;
    ContactManifold ab;
    ContactManifold ba;
    CombineMaterials(a, b, &ab);
    CombineMaterials(b, a, &ba);
    CHECK(Within(ab.friction, 0.16f));
    CHECK(ab.friction == ba.friction);
    CHECK(Within(ab.restitution, 0.5f));  // Maximum is the default restitution rule
    CHECK(ab.restitution == ba.restitution);
}

namespace
{

// The arguments are drawn into locals first: the order in which a call's arguments are evaluated is not specified, and
// GCC and Clang differ.
Vec3 RandomVec(Rng& rng, f32 lo, f32 hi)
{
    const f32 x = rng.Range(lo, hi);
    const f32 y = rng.Range(lo, hi);
    const f32 z = rng.Range(lo, hi);
    return Vec3(x, y, z);
}

Shape RandomShape(Rng& rng, u32 kind)
{
    switch (kind)
    {
    case 0: return Shape::MakeSphere(rng.Range(0.2f, 0.8f));
    case 1:
    {
        const f32 half_height = rng.Range(0.0f, 1.0f);
        const f32 radius = rng.Range(0.15f, 0.5f);
        return Shape::MakeCapsule(half_height, radius);
    }
    default: return Shape::MakeBox(RandomVec(rng, 0.2f, 1.0f));
    }
}

Pose RandomPose(Rng& rng, f32 spread)
{
    Pose p;
    p.position = RandomVec(rng, -spread, spread);
    // A random rotation from a normalized random 4-vector: no trigonometry, so the sample is the same on every compiler
    // and libm.
    const Vec3 v = RandomVec(rng, -1.0f, 1.0f);
    const f32 w = rng.Range(-1.0f, 1.0f);
    Quaternion q(v.x, v.y, v.z, w);
    if (q.LengthSquared() < 1e-4f)
    {
        q = Quaternion::Identity();
    }
    p.rotation = q.Normalized();
    return p;
}

// Whether two manifolds are the same contact with the shapes' roles swapped (points matched by position, as the order
// of the two points may differ).
bool SwappedMatch(const ContactManifold& ab, const ContactManifold& ba)
{
    if (ab.count != ba.count || !WithinV(ab.normal, Vec3(-ba.normal.x, -ba.normal.y, -ba.normal.z), 2e-3f))
    {
        return false;
    }
    for (u32 i = 0; i < ab.count; ++i)
    {
        bool found = false;
        for (u32 j = 0; j < ba.count; ++j)
        {
            if (WithinV(ab.points[i].position_a, ba.points[j].position_b, 2e-4f) &&
                WithinV(ab.points[i].position_b, ba.points[j].position_a, 2e-4f) &&
                Within(ab.points[i].depth, ba.points[j].depth, 2e-4f))
            {
                found = true;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("Collide: swapping the arguments gives the flipped manifold (100000 random pairs)",
          "[physics][collide][random]")
{
    Rng rng(379);
    u32 contacts = 0;
    u32 two_point = 0;
    for (u32 i = 0; i < 100000; ++i)
    {
        const u32 ka = rng.Next() % 3;
        const u32 kb = rng.Next() % 3;
        const Shape a = RandomShape(rng, ka);
        const Shape b = RandomShape(rng, kb);
        const Pose pa = RandomPose(rng, 1.2f);
        Pose pb = RandomPose(rng, 1.2f);
        if (ka == 1 && kb == 1 && rng.Next() % 2 == 0)
        {
            pb.rotation = pa.rotation;  // parallel capsules: the two-point case
        }
        ContactManifold ab;
        ContactManifold ba;
        const CollideResult r1 = Collide(a, pa, b, pb, 0.02f, &ab);
        const CollideResult r2 = Collide(b, pb, a, pa, 0.02f, &ba);
        REQUIRE(r1 == r2);
        if (r1 == CollideResult::Contact)
        {
            ++contacts;
            two_point += ab.count == 2 ? 1u : 0u;
            REQUIRE(SwappedMatch(ab, ba));
            // The depth of every point is the separation of its two surface points along the normal.
            for (u32 k = 0; k < ab.count; ++k)
            {
                const f32 along = (ab.points[k].position_a - ab.points[k].position_b).Dot(ab.normal);
                REQUIRE(Within(along, ab.points[k].depth, 2e-4f));
            }
            REQUIRE(Within(ab.normal.LengthSquared(), 1.0f, 1e-4f));
        }
    }
    CHECK(contacts > 10000);
    CHECK(two_point > 0);  // parallel capsules occur in the sample
}

TEST_CASE("Collide: moving B by the depth along the normal separates the pair (property, 50000 random pairs)",
          "[physics][collide][random]")
{
    Rng rng(380);
    u32 tested = 0;
    for (u32 i = 0; i < 50000; ++i)
    {
        const u32 ka = rng.Next() % 2;  // sphere or capsule first
        const u32 kb = rng.Next() % 3;
        if (ka == 1 && kb == 2)
        {
            continue;  // capsule-box is Day 380
        }
        const Shape a = RandomShape(rng, ka);
        const Shape b = RandomShape(rng, kb);
        const Pose pa = RandomPose(rng, 0.9f);
        const Pose pb = RandomPose(rng, 0.9f);
        ContactManifold m;
        if (Collide(a, pa, b, pb, 0.0f, &m) != CollideResult::Contact)
        {
            continue;
        }
        ++tested;
        Pose moved = pb;
        moved.position = pb.position + m.normal * (MaxDepth(m) + 1e-3f);
        ContactManifold after;
        const CollideResult r = Collide(a, pa, b, moved, 0.0f, &after);
        REQUIRE((r == CollideResult::None || MaxDepth(after) < 5e-3f));
    }
    CHECK(tested > 5000);
}

TEST_CASE("Collide: the manifolds of 100000 random pairs hash to the committed constant (the same on every compiler)",
          "[physics][collide][hash]")
{
    Rng rng(379);
    StateHasher inputs;
    StateHasher outputs;
    for (u32 i = 0; i < 100000; ++i)
    {
        const u32 ka = rng.Next() % 3;
        const u32 kb = rng.Next() % 3;
        const Shape a = RandomShape(rng, ka);
        const Shape b = RandomShape(rng, kb);
        const Pose pa = RandomPose(rng, 1.2f);
        Pose pb = RandomPose(rng, 1.2f);
        if (ka == 1 && kb == 1 && rng.Next() % 2 == 0)
        {
            pb.rotation = pa.rotation;
        }
        inputs.AddF32(pa.rotation.x);
        inputs.AddF32(pb.rotation.w);
        ContactManifold m;
        const CollideResult r = Collide(a, pa, b, pb, 0.02f, &m);
        outputs.AddU8(static_cast<u8>(r));
        if (r == CollideResult::Contact)
        {
            outputs.AddU32(m.count);
            outputs.AddF32(m.normal.x);
            outputs.AddF32(m.normal.y);
            outputs.AddF32(m.normal.z);
            for (u32 k = 0; k < m.count; ++k)
            {
                outputs.AddF32(m.points[k].depth);
                outputs.AddF32(m.points[k].position_a.x);
            }
        }
    }
    // The inputs come from integers and + - * / Sqrt only, so their hash is the same everywhere too: it tells a
    // difference in the test's sample from one in the code.
    CHECK(inputs.Value() == 0xab187a6cda5f0dfbull);
    CHECK(outputs.Value() == 0x00ff613a34e7890eull);
}
