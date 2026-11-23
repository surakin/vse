#include <catch2/catch_test_macros.hpp>

#include "math/geometry.h"
#include "math/placement.h"
#include "math/vector.h"

using namespace vse;

namespace
{
// A small deterministic generator (an LCG), so the test needs no library.
struct Lcg
{
    u64 state = 0x9e3779b97f4a7c15ull;
    f64 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<f64>(state >> 11) / static_cast<f64>(1ull << 53) * 2.0 - 1.0;
    }
};
}  // namespace

TEST_CASE("Vec3d matches a scalar reference over random values and edge cases", "[math][double]")
{
    Lcg rng;
    for (int i = 0; i < 10000; ++i)
    {
        const Vec3d a(rng.Next() * 1e7, rng.Next() * 1e-7, rng.Next());
        const Vec3d b(rng.Next(), rng.Next() * 1e9, rng.Next() * 1e-3);
        const Vec3d sum = a + b;
        const Vec3d diff = a - b;
        REQUIRE(sum.x == a.x + b.x);
        REQUIRE(sum.y == a.y + b.y);
        REQUIRE(diff.z == a.z - b.z);
        REQUIRE(a.Dot(b) == a.x * b.x + a.y * b.y + a.z * b.z);
        REQUIRE(a.Length() == Sqrt(a.x * a.x + a.y * a.y + a.z * a.z));
    }
    const f64 edges[] = {0.0, -0.0, 1e300, -1e300, 1e-300, 5e-324};
    for (f64 e : edges)
    {
        const Vec3d v(e, e, e);
        REQUIRE((v + v).x == e + e);
        REQUIRE((v * 2.0).y == e * 2.0);
    }
}

TEST_CASE("Widening is exact and narrowing is named", "[math][double]")
{
    const Vec3 f(0.1f, -3.5f, 1e-30f);
    const Vec3d d = f;  // implicit, exact
    REQUIRE(d.x == static_cast<f64>(f.x));
    REQUIRE(d.y == static_cast<f64>(f.y));
    REQUIRE(d.z == static_cast<f64>(f.z));
    const Vec3 back = d.Narrow();
    REQUIRE(back == f);
    const Aabb box = Aabb::FromMinMax(Vec3(-1, -2, -3), Vec3(4, 5, 6));
    const Aabbd boxd = box;
    REQUIRE(boxd.Narrow().min == box.min);
    REQUIRE(boxd.Narrow().max == box.max);
}

TEST_CASE("An object 10,000 km from the origin keeps a millimetre in f64, loses it in f32", "[math][double]")
{
    const f64 far = 1.0e7;  // metres
    const Vec3d a(far, 0, 0);
    const Vec3d b(far + 0.001, 0, 0);
    REQUIRE((b - a).x > 0.00099);
    REQUIRE((b - a).x < 0.00101);
    const Vec3 af(static_cast<f32>(far), 0, 0);
    const Vec3 bf(static_cast<f32>(far + 0.001), 0, 0);
    REQUIRE((bf - af).x == 0.0f);  // f32 steps are 1 m here
    // The difference computed in f64 then narrowed is exact to f32.
    const Vec3 narrowed = (b - a).Narrow();
    REQUIRE(narrowed.x == static_cast<f32>((b - a).x));
}

TEST_CASE("Placement composes and relocates in double precision", "[math][double]")
{
    const Vec3d origin(1.0e7, 2.0e6, -3.0e6);
    Placement p = Placement::From(origin + Vec3d(0.001, 0, 0), Mat4::Identity());
    const Mat4 rel = p.RelativeTo(origin);
    REQUIRE(rel.col[3].x > 0.00099f);
    REQUIRE(rel.col[3].x < 0.00101f);
    REQUIRE(rel.col[3].y == 0.0f);
    Placement child = Placement::From(Vec3d(1, 2, 3), Mat4::Identity());
    const Placement both = p.Compose(child);
    REQUIRE(both.position.x == p.position.x + 1.0);
    REQUIRE(both.position.z == p.position.z + 3.0);
    REQUIRE(p.TransformPoint(Vec3d(1, 0, 0)).x == p.position.x + 1.0);
}

TEST_CASE("The f64 frustum tests a far box and Translated moves it", "[math][geometry][double]")
{
    const Mat4 vp = Mat4::Identity();  // the clip cube: x,y in [-1,1], z in [0,1]
    const Frustumd camera_space = Frustumd::FromViewProjection(vp);
    const Vec3d camera(1.0e7, 0, 0);
    const Frustumd world = camera_space.Translated(camera);
    const Aabbd inside = Aabbd::FromMinMax(camera + Vec3d(-0.1, -0.1, 0.2), camera + Vec3d(0.1, 0.1, 0.4));
    const Aabbd outside = Aabbd::FromMinMax(camera + Vec3d(5, -0.1, 0.2), camera + Vec3d(6, 0.1, 0.4));
    REQUIRE(world.TestAabb(inside) == Containment::Inside);
    REQUIRE(world.TestAabb(outside) == Containment::Outside);
    // The f32 frustum agrees with the f64 one near the origin.
    const Frustum f32_frustum = Frustum::FromViewProjection(vp);
    const Aabb near_box = Aabb::FromMinMax(Vec3(-0.1f, -0.1f, 0.2f), Vec3(0.1f, 0.1f, 0.4f));
    REQUIRE(f32_frustum.TestAabb(near_box) == Containment::Inside);
}

TEST_CASE("The f64 geometry functions agree with the f32 ones on exact values", "[math][geometry][double]")
{
    const Aabbd a = Aabbd::FromMinMax(Vec3d(0, 0, 0), Vec3d(2, 2, 2));
    const Aabbd b = Aabbd::FromMinMax(Vec3d(1, 1, 1), Vec3d(3, 3, 3));
    REQUIRE(Overlaps(a, b));
    REQUIRE(Volume(Intersection(a, b)) == 1.0);
    REQUIRE(SurfaceArea(Union(a, b)) == 54.0);
    f64 t = 0;
    const Rayd ray = Rayd::Make(Vec3d(-1, 1, 1), Vec3d(1, 0, 0));
    REQUIRE(Intersect(ray, a, 10.0, &t));
    REQUIRE(t == 1.0);
}

TEST_CASE("The f64 sphere, plane and bounds work like the f32 ones", "[math][geometry][double]")
{
    const Sphered s{Vec3d(1.0e7, 0, 0), 2.0};
    REQUIRE(Overlaps(s, Sphered{Vec3d(1.0e7 + 3.0, 0, 0), 1.5}));
    REQUIRE(!Overlaps(s, Sphered{Vec3d(1.0e7 + 4.0, 0, 0), 1.0}));
    const Planed plane = Planed::FromPointNormal(Vec3d(1.0e7, 0, 0), Vec3d(0, 2, 0));
    REQUIRE(plane.normal.y == 1.0);
    REQUIRE(plane.Distance(Vec3d(5, 3.5, 5)) == 3.5);
    // A sphere beyond the left plane of the f64 frustum is outside, one at the camera is inside.
    const Frustumd frustum = Frustumd::FromViewProjection(Mat4::Identity()).Translated(Vec3d(1.0e7, 0, 0));
    REQUIRE(frustum.TestSphere(Sphered{Vec3d(1.0e7, 0, 0.5), 0.1}) == Containment::Inside);
    REQUIRE(frustum.TestSphere(Sphered{Vec3d(1.0e7 - 9.0, 0, 0.5), 0.1}) == Containment::Outside);
    Boundsd bounds;
    bounds.box = Aabbd::FromMinMax(Vec3d(1.0e7 - 0.1, -0.1, 0.2), Vec3d(1.0e7 + 0.1, 0.1, 0.4));
    bounds.sphere = Sphered{Vec3d(1.0e7, 0, 0.3), 0.3};
    REQUIRE(frustum.Visible(bounds));
}
