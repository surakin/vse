// The shared shapes of math/geometry.h (Phase 18 Day 238): every overlap test against brute-force point sampling on
// 10,000 random pairs (a sampled common point means they must overlap; a reported overlap must come with a witness
// point in both, except for the frustum, which is conservative and is checked against "entirely beyond one plane"), the
// ray tests against analytic cases and against marching along the ray, and the transformed box against the box of its
// eight transformed corners.
#include <catch2/catch_test_macros.hpp>

#include "math/geometry.h"
#include "math/quaternion.h"
#include "spatial_test_rng.h"

using namespace engine_placeholder;
using spatial_test::Rng;

namespace
{

constexpr u32 kPairs = 10000;
constexpr u32 kSamples = 48;

Aabb RandomBox(Rng& rng, f32 extent, f32 size)
{
    const Vec3 c = rng.Point(-extent, extent);
    const Vec3 h(rng.Float(0.05f, size), rng.Float(0.05f, size), rng.Float(0.05f, size));
    return Aabb::FromMinMax(c - h, c + h);
}

Sphere RandomSphere(Rng& rng, f32 extent, f32 size)
{
    Sphere s;
    s.center = rng.Point(-extent, extent);
    s.radius = rng.Float(0.05f, size);
    return s;
}

Vec3 PointIn(Rng& rng, const Aabb& box)
{
    return Vec3(rng.Float(box.min.x, box.max.x), rng.Float(box.min.y, box.max.y), rng.Float(box.min.z, box.max.z));
}

Vec3 PointIn(Rng& rng, const Sphere& s)
{
    for (;;)
    {  // rejection from the enclosing cube
        const Vec3 p(rng.Float(-1, 1), rng.Float(-1, 1), rng.Float(-1, 1));
        if (p.LengthSquared() <= 1.0f)
        {
            return s.center + p * s.radius;
        }
    }
}

bool InFrustum(const Frustum& f, const Vec3& p)
{
    for (const Plane& plane : f.planes)
    {
        if (plane.Distance(p) < 0.0f)
        {
            return false;
        }
    }
    return true;
}

// A view frustum looking down -Z from the origin: near 1, far 40, half angles of about 40 degrees across and 30 up.
Frustum TestFrustum()
{
    Frustum f;
    f.planes[0] = Plane::FromPointNormal(Vec3(0, 0, 0), Vec3(0.766f, 0, -0.643f));   // left
    f.planes[1] = Plane::FromPointNormal(Vec3(0, 0, 0), Vec3(-0.766f, 0, -0.643f));  // right
    f.planes[2] = Plane::FromPointNormal(Vec3(0, 0, 0), Vec3(0, 0.866f, -0.5f));     // bottom
    f.planes[3] = Plane::FromPointNormal(Vec3(0, 0, 0), Vec3(0, -0.866f, -0.5f));    // top
    f.planes[4] = Plane::FromPointNormal(Vec3(0, 0, -1), Vec3(0, 0, -1));            // near
    f.planes[5] = Plane::FromPointNormal(Vec3(0, 0, -40), Vec3(0, 0, 1));            // far
    return f;
}

bool Near(f32 a, f32 b, f32 tolerance)
{
    return (a > b ? a - b : b - a) <= tolerance;
}

}  // namespace

TEST_CASE("Geometry: box/box overlap agrees with sampling and has a witness", "[spatial][geometry]")
{
    Rng rng(238001);
    u32 overlapping = 0;
    for (u32 i = 0; i < kPairs; ++i)
    {
        const Aabb a = RandomBox(rng, 3, 3);
        const Aabb b = RandomBox(rng, 3, 3);
        const bool overlaps = Overlaps(a, b);
        CHECK(overlaps == Overlaps(b, a));
        bool sampled = false;
        for (u32 s = 0; s < kSamples && !sampled; ++s)
        {
            sampled = Contains(b, PointIn(rng, a));
        }
        if (sampled)
        {
            CHECK(overlaps);
        }
        if (overlaps)
        {
            ++overlapping;
            const Aabb common = Intersection(a, b);
            REQUIRE_FALSE(common.IsEmpty());
            CHECK(Contains(a, common.Center()));
            CHECK(Contains(b, common.Center()));
        }
        else
        {
            CHECK(Intersection(a, b).IsEmpty());
        }
    }
    CHECK(overlapping > kPairs / 10);  // the seed gives both answers often
    CHECK(overlapping < kPairs - kPairs / 10);
}

TEST_CASE("Geometry: box/sphere overlap agrees with sampling and has a witness", "[spatial][geometry]")
{
    Rng rng(238002);
    u32 overlapping = 0;
    for (u32 i = 0; i < kPairs; ++i)
    {
        const Aabb box = RandomBox(rng, 3, 3);
        const Sphere sphere = RandomSphere(rng, 3, 3);
        const bool overlaps = Overlaps(box, sphere);
        bool sampled = false;
        for (u32 s = 0; s < kSamples && !sampled; ++s)
        {
            sampled = Contains(sphere, PointIn(rng, box));
        }
        if (sampled)
        {
            CHECK(overlaps);
        }
        if (overlaps)
        {
            ++overlapping;
            // The point of the box nearest the center is in both.
            const Vec3 c = sphere.center;
            const Vec3 nearest(c.x < box.min.x ? box.min.x : (c.x > box.max.x ? box.max.x : c.x),
                               c.y < box.min.y ? box.min.y : (c.y > box.max.y ? box.max.y : c.y),
                               c.z < box.min.z ? box.min.z : (c.z > box.max.z ? box.max.z : c.z));
            CHECK(Contains(box, nearest));
            CHECK((nearest - c).LengthSquared() <= sphere.radius * sphere.radius * (1.0f + 1e-5f));
        }
    }
    CHECK(overlapping > kPairs / 10);
    CHECK(overlapping < kPairs - kPairs / 10);
}

TEST_CASE("Geometry: sphere/sphere overlap agrees with sampling and has a witness", "[spatial][geometry]")
{
    Rng rng(238003);
    u32 overlapping = 0;
    for (u32 i = 0; i < kPairs; ++i)
    {
        const Sphere a = RandomSphere(rng, 3, 3);
        const Sphere b = RandomSphere(rng, 3, 3);
        const bool overlaps = Overlaps(a, b);
        CHECK(overlaps == Overlaps(b, a));
        bool sampled = false;
        for (u32 s = 0; s < kSamples && !sampled; ++s)
        {
            sampled = Contains(b, PointIn(rng, a));
        }
        if (sampled)
        {
            CHECK(overlaps);
        }
        if (overlaps)
        {
            ++overlapping;
            // The point of `a` nearest b's center (or b's center itself when it is inside `a`) is in both.
            const Vec3 to_b = b.center - a.center;
            const f32 distance = to_b.Length();
            const Vec3 witness = distance <= a.radius ? b.center : a.center + to_b * (a.radius / distance);
            CHECK((witness - a.center).LengthSquared() <= a.radius * a.radius * (1.0f + 1e-4f));
            CHECK((witness - b.center).LengthSquared() <= b.radius * b.radius * (1.0f + 1e-4f) + 1e-5f);
        }
    }
    CHECK(overlapping > kPairs / 10);
    CHECK(overlapping < kPairs - kPairs / 10);
}

TEST_CASE("Geometry: box/frustum and sphere/frustum are conservative and exact per plane", "[spatial][geometry]")
{
    Rng rng(238004);
    const Frustum f = TestFrustum();
    u32 culled = 0;
    for (u32 i = 0; i < kPairs; ++i)
    {
        const Vec3 c(rng.Float(-40, 40), rng.Float(-30, 30), rng.Float(-50, 5));
        const Vec3 h(rng.Float(0.1f, 4), rng.Float(0.1f, 4), rng.Float(0.1f, 4));
        const Aabb b = Aabb::FromMinMax(c - h, c + h);
        const bool box_in = Overlaps(f, b);
        bool sampled = false;
        for (u32 s = 0; s < kSamples && !sampled; ++s)
        {
            sampled = InFrustum(f, PointIn(rng, b));
        }
        if (sampled)
        {
            CHECK(box_in);
        }
        // Entirely beyond one plane (all eight corners outside it) must be culled.
        bool beyond_one = false;
        for (const Plane& plane : f.planes)
        {
            bool all_out = true;
            for (u32 corner = 0; corner < 8; ++corner)
            {
                const Vec3 p((corner & 1) != 0 ? b.max.x : b.min.x, (corner & 2) != 0 ? b.max.y : b.min.y,
                             (corner & 4) != 0 ? b.max.z : b.min.z);
                if (plane.Distance(p) >= 0.0f)
                {
                    all_out = false;
                }
            }
            beyond_one = beyond_one || all_out;
        }
        CHECK(box_in == !beyond_one);
        if (!box_in)
        {
            ++culled;
        }

        Sphere s;
        s.center = c;
        s.radius = rng.Float(0.1f, 4);
        const bool sphere_in = Overlaps(f, s);
        sampled = false;
        for (u32 k = 0; k < kSamples && !sampled; ++k)
        {
            sampled = InFrustum(f, PointIn(rng, s));
        }
        if (sampled)
        {
            CHECK(sphere_in);
        }
        bool sphere_beyond = false;
        for (const Plane& plane : f.planes)
        {
            sphere_beyond = sphere_beyond || plane.Distance(s.center) < -s.radius;
        }
        CHECK(sphere_in == !sphere_beyond);
    }
    CHECK(culled > kPairs / 10);
    CHECK(culled < kPairs - kPairs / 10);
}

TEST_CASE("Geometry: ray against box, the analytic cases", "[spatial][geometry][ray]")
{
    const Aabb box = Aabb::FromMinMax(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    f32 t = -1;
    // Hits a face head on.
    REQUIRE(Intersect(Ray::Make(Vec3(-5, 0, 0), Vec3(1, 0, 0)), box, 100, &t));
    CHECK(t == 4.0f);
    // A direction of any length: t counts in its units.
    REQUIRE(Intersect(Ray::Make(Vec3(-5, 0, 0), Vec3(2, 0, 0)), box, 100, &t));
    CHECK(t == 2.0f);
    // Starts inside: t is 0.
    REQUIRE(Intersect(Ray::Make(Vec3(0.5f, 0.2f, -0.3f), Vec3(0.3f, -1, 0.2f)), box, 100, &t));
    CHECK(t == 0.0f);
    // Along an edge (y = 1, z = 1): a graze counts.
    REQUIRE(Intersect(Ray::Make(Vec3(-5, 1, 1), Vec3(1, 0, 0)), box, 100, &t));
    CHECK(t == 4.0f);
    // Through a corner diagonally.
    REQUIRE(Intersect(Ray::Make(Vec3(-2, -2, -2), Vec3(1, 1, 1)), box, 100, &t));
    CHECK(t == 1.0f);
    // Parallel to a face, outside its slab: a miss. Parallel inside the slab: a hit (no NaN from 0 * infinity).
    CHECK_FALSE(Intersect(Ray::Make(Vec3(-5, 2, 0), Vec3(1, 0, 0)), box, 100, &t));
    REQUIRE(
        Intersect(Ray::Make(Vec3(-1, -5, 0.5f), Vec3(0, 1, 0)), box, 100, &t));  // on the face x = -1, moving along it
    CHECK(t == 4.0f);
    // Two zero components, and a negative direction.
    REQUIRE(Intersect(Ray::Make(Vec3(0, 0, 9), Vec3(0, 0, -3)), box, 100, &t));
    CHECK(t == (9.0f - 1.0f) / 3.0f);
    // Behind the origin: a miss. Beyond max_t: a miss; exactly at max_t: a hit.
    CHECK_FALSE(Intersect(Ray::Make(Vec3(5, 0, 0), Vec3(1, 0, 0)), box, 100, &t));
    CHECK_FALSE(Intersect(Ray::Make(Vec3(-5, 0, 0), Vec3(1, 0, 0)), box, 3.9f, &t));
    CHECK(Intersect(Ray::Make(Vec3(-5, 0, 0), Vec3(1, 0, 0)), box, 4.0f, &t));
    CHECK_FALSE(Intersect(Ray::Make(Vec3(-5, 0, 0), Vec3(1, 0, 0)), box, -1.0f, &t));
    // An empty box is never hit; a null t is allowed.
    CHECK_FALSE(Intersect(Ray::Make(Vec3(0, 0, 0), Vec3(1, 0, 0)), Aabb::Empty(), 100, nullptr));
    CHECK(Intersect(Ray::Make(Vec3(-5, 0, 0), Vec3(1, 0, 0)), box, 100, nullptr));
}

TEST_CASE("Geometry: ray against box agrees with marching along the ray", "[spatial][geometry][ray]")
{
    Rng rng(238005);
    constexpr u32 kSteps = 400;
    for (u32 i = 0; i < 2000; ++i)
    {
        const Aabb box = RandomBox(rng, 4, 2.5f);
        const Vec3 origin = rng.Point(-8, 8);
        Vec3 direction = rng.Point(-1, 1);
        if (rng.Below(4) == 0)
        {
            direction[rng.Below(3)] = 0.0f;  // some rays with a zero component
        }
        if (direction.LengthSquared() < 1e-4f)
        {
            direction = Vec3(1, 0, 0);
        }
        const Ray ray = Ray::Make(origin, direction);
        const f32 max_t = 20.0f;
        f32 t = -1;
        const bool hit = Intersect(ray, box, max_t, &t);
        if (hit)
        {
            REQUIRE(t >= 0.0f);
            REQUIRE(t <= max_t);
            CHECK(Distance2(ray.At(t), box) <= 1e-6f * (1.0f + ray.At(t).LengthSquared()));
            // Just before the entry the ray is outside (unless it starts inside).
            if (t > 1e-3f)
            {
                CHECK(Distance2(ray.At(t * 0.99f), box) > 0.0f);
            }
        }
        else
        {
            // No sample along [0, max_t] is inside the box (shrunk a little, so a sample on a face is not a
            // disagreement).
            const Aabb inner = Expand(box, -1e-3f);
            for (u32 s = 0; s <= kSteps; ++s)
            {
                CHECK_FALSE(Contains(inner, ray.At(max_t * static_cast<f32>(s) / kSteps)));
            }
        }
    }
}

TEST_CASE("Geometry: ray against sphere and plane", "[spatial][geometry][ray]")
{
    Sphere s;
    s.center = Vec3(0, 0, -10);
    s.radius = 2;
    f32 t = -1;
    REQUIRE(Intersect(Ray::Make(Vec3(0, 0, 0), Vec3(0, 0, -1)), s, 100, &t));
    CHECK(t == 8.0f);
    REQUIRE(Intersect(Ray::Make(Vec3(2, 0, 0), Vec3(0, 0, -1)), s, 100, &t));  // tangent
    CHECK(Near(t, 10.0f, 1e-3f));
    CHECK_FALSE(Intersect(Ray::Make(Vec3(2.01f, 0, 0), Vec3(0, 0, -1)), s, 100, &t));
    CHECK_FALSE(Intersect(Ray::Make(Vec3(0, 0, 0), Vec3(0, 0, 1)), s, 100, &t));    // pointing away
    CHECK_FALSE(Intersect(Ray::Make(Vec3(0, 0, 0), Vec3(0, 0, -1)), s, 7.9f, &t));  // too short
    REQUIRE(Intersect(Ray::Make(Vec3(0.5f, 0, -10), Vec3(1, 0, 0)), s, 100, &t));   // inside
    CHECK(t == 0.0f);

    Rng rng(238006);
    for (u32 i = 0; i < 2000; ++i)
    {
        const Sphere sphere = RandomSphere(rng, 4, 2.5f);
        Vec3 direction = rng.Point(-1, 1);
        if (direction.LengthSquared() < 1e-4f)
        {
            direction = Vec3(0, 1, 0);
        }
        const Ray ray = Ray::Make(rng.Point(-8, 8), direction);
        const bool hit = Intersect(ray, sphere, 20.0f, &t);
        if (hit)
        {
            const f32 d = (ray.At(t) - sphere.center).Length();
            if (t > 0.0f)
            {
                CHECK(Near(d, sphere.radius, 1e-3f * (1.0f + sphere.radius)));
            }
        }
        else
        {
            for (u32 k = 0; k <= 400; ++k)
            {
                CHECK((ray.At(20.0f * static_cast<f32>(k) / 400) - sphere.center).Length() > sphere.radius * 0.999f);
            }
        }
    }

    const Plane ground = Plane::FromPointNormal(Vec3(0, 1, 0), Vec3(0, 2, 0));  // y = 1, normal normalized
    CHECK(ground.normal == Vec3(0, 1, 0));
    REQUIRE(Intersect(Ray::Make(Vec3(3, 5, 1), Vec3(0, -2, 0)), ground, 100, &t));
    CHECK(t == 2.0f);
    CHECK_FALSE(Intersect(Ray::Make(Vec3(3, 5, 1), Vec3(0, 2, 0)), ground, 100, &t));    // crosses behind the origin
    CHECK_FALSE(Intersect(Ray::Make(Vec3(3, 5, 1), Vec3(1, 0, 0)), ground, 100, &t));    // parallel
    CHECK_FALSE(Intersect(Ray::Make(Vec3(3, 1, 1), Vec3(1, 0, 0)), ground, 100, &t));    // parallel, in the plane
    CHECK_FALSE(Intersect(Ray::Make(Vec3(3, 5, 1), Vec3(0, -2, 0)), ground, 1.5f, &t));  // too short
}

TEST_CASE("Geometry: a transformed box is the box of its eight transformed corners", "[spatial][geometry]")
{
    Rng rng(238007);
    for (u32 i = 0; i < 1000; ++i)
    {
        const Aabb box = RandomBox(rng, 5, 3);
        const Vec3 axis = rng.Point(-1, 1);
        const Quaternion q = axis.LengthSquared() > 1e-4f
                                 ? Quaternion::FromAxisAngle(axis.Normalized(), rng.Float(-3.1f, 3.1f))
                                 : Quaternion::Identity();
        Mat4 m = q.ToMat4();
        const Vec3 scale(rng.Float(0.2f, 3), rng.Float(0.2f, 3), rng.Float(0.2f, 3));
        for (u32 k = 0; k < 3; ++k)
        {
            m.col[k] = m.col[k] * scale[k];
        }
        m.col[3] = Vec4(rng.Point(-10, 10), 1.0f);
        Aabb expected = Aabb::Empty();
        for (u32 corner = 0; corner < 8; ++corner)
        {
            const Vec3 p((corner & 1) != 0 ? box.max.x : box.min.x, (corner & 2) != 0 ? box.max.y : box.min.y,
                         (corner & 4) != 0 ? box.max.z : box.min.z);
            const Vec4 tp = m * Vec4(p, 1.0f);
            expected = Union(expected, Vec3(tp.x, tp.y, tp.z));
        }
        const Aabb got = TransformAabb(box, m);
        for (u32 k = 0; k < 3; ++k)
        {
            CHECK(Near(got.min[k], expected.min[k], 1e-4f * (1.0f + Abs(expected.min[k]))));
            CHECK(Near(got.max[k], expected.max[k], 1e-4f * (1.0f + Abs(expected.max[k]))));
        }
    }
}

TEST_CASE("Geometry: union, intersection, expansion, containment, area, volume and distance", "[spatial][geometry]")
{
    const Aabb a = Aabb::FromMinMax(Vec3(0, 0, 0), Vec3(2, 3, 4));
    const Aabb b = Aabb::FromMinMax(Vec3(1, 1, 1), Vec3(5, 2, 2));
    const Aabb u = Union(a, b);
    CHECK(u.min == Vec3(0, 0, 0));
    CHECK(u.max == Vec3(5, 3, 4));
    CHECK(Union(Aabb::Empty(), a).min == a.min);
    CHECK(Union(a, Aabb::Empty()).max == a.max);
    CHECK(Union(a, Intersection(a, Aabb::FromMinMax(Vec3(9, 9, 9), Vec3(10, 10, 10)))).max ==
          a.max);  // a non-canonical empty box adds nothing
    CHECK(Aabb::Empty().IsEmpty());
    CHECK(Union(a, Vec3(-1, 7, 2)).min == Vec3(-1, 0, 0));
    const Aabb i = Intersection(a, b);
    CHECK(i.min == Vec3(1, 1, 1));
    CHECK(i.max == Vec3(2, 2, 2));
    CHECK(Expand(a, 1).min == Vec3(-1, -1, -1));
    CHECK(Expand(a, -1.5f).IsEmpty());
    CHECK(Contains(a, Vec3(2, 3, 4)));
    CHECK_FALSE(Contains(a, Vec3(2.01f, 3, 4)));
    CHECK(Contains(u, a));
    CHECK_FALSE(Contains(a, u));
    CHECK(Contains(a, Aabb::Empty()));
    CHECK(SurfaceArea(a) == 2.0f * (6 + 12 + 8));
    CHECK(Volume(a) == 24.0f);
    CHECK(SurfaceArea(Aabb::Empty()) == 0.0f);
    CHECK(Volume(Aabb::Empty()) == 0.0f);
    CHECK(Distance2(Vec3(1, 1, 1), a) == 0.0f);
    CHECK(Distance2(Vec3(-3, 7, 4), a) == 9.0f + 16.0f);
    CHECK(Center(a) == Vec3(1, 1.5f, 2));
    // Touching counts as overlapping; an empty box overlaps nothing.
    CHECK(Overlaps(a, Aabb::FromMinMax(Vec3(2, 0, 0), Vec3(3, 1, 1))));
    CHECK_FALSE(Overlaps(a, Aabb::Empty()));
    Sphere touching;
    touching.center = Vec3(3, 1, 1);
    touching.radius = 1;
    CHECK(Overlaps(a, touching));
}

TEST_CASE("Geometry: an empty box stays empty through a transform and is never in a frustum", "[spatial][geometry]")
{
    // Aabb::Empty() has infinite corners, so its center is NaN; every plane comparison is then false, which must not
    // read as "inside".
    const Frustum f = TestFrustum();
    CHECK_FALSE(Overlaps(f, Aabb::Empty()));
    CHECK_FALSE(Overlaps(f, Intersection(Aabb::FromMinMax(Vec3(0, 0, -5), Vec3(1, 1, -4)),
                                         Aabb::FromMinMax(Vec3(3, 3, -5), Vec3(4, 4, -4)))));
    Mat4 m = Quaternion::FromAxisAngle(Vec3(0, 1, 0), 0.5f).ToMat4();
    m.col[3] = Vec4(Vec3(2, 0, 1), 1.0f);
    CHECK(TransformAabb(Aabb::Empty(), m).IsEmpty());
    CHECK(TransformAabb(Intersection(Aabb::FromMinMax(Vec3(0, 0, 0), Vec3(1, 1, 1)),
                                     Aabb::FromMinMax(Vec3(2, 2, 2), Vec3(3, 3, 3))),
                        m)
              .IsEmpty());
    CHECK_FALSE(
        Overlaps(TransformAabb(Aabb::Empty(), m), Aabb::FromMinMax(Vec3(-100, -100, -100), Vec3(100, 100, 100))));
}

TEST_CASE("TransformBounds: the sphere covers the largest stretch of a rotation under a non-uniform scale",
          "[spatial][geometry]")
{
    using namespace engine_placeholder;
    Bounds local;
    local.box = Aabb::FromMinMax(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    local.sphere.center = Vec3(0, 0, 0);
    local.sphere.radius = 1.0f;
    const f32
        c = 0.70710678f;  // diag(2,1,1) * Rz(45 degrees): the columns are about 1.58 long but the matrix stretches by 2
    const Mat4 m = Mat4::FromBasis(Vec3(2 * c, c, 0), Vec3(-2 * c, c, 0), Vec3(0, 0, 1), Vec3(0, 0, 0));
    CHECK(TransformBounds(local, m).sphere.radius >= 2.0f);
    CHECK(TransformBounds(local, m).sphere.radius < 2.001f);
    const Mat4 uniform = Mat4::FromBasis(Vec3(3 * c, 3 * c, 0), Vec3(-3 * c, 3 * c, 0), Vec3(0, 0, 3), Vec3(5, 0, 0));
    CHECK(TransformBounds(local, uniform).sphere.radius >= 3.0f);
    CHECK(TransformBounds(local, uniform).sphere.radius < 3.001f);
}
