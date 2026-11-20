// Mass properties (Phase 25 Day 387): closed forms against analytic values and against a numeric integral.
#include <catch2/catch_test_macros.hpp>

#include "lang/scalar.h"
#include "math/math_util.h"
#include "physics/mass.h"

using namespace vse;
using namespace vse::physics;

namespace
{

bool Close(f32 a, f32 b, f32 relative)
{
    return Abs(a - b) <= relative * (Abs(a) > Abs(b) ? Abs(a) : Abs(b)) + 1e-9f;
}

// The inertia about each axis by summing cells: a grid of n^3 points over the shape's box, each inside point weighing
// density * cell volume.
Vec3 NumericInertia(const Shape& shape, f32 density, int n, f32* mass_out)
{
    const Vec3 h = shape.LocalHalfExtents();
    const Vec3 cell(2.0f * h.x / static_cast<f32>(n), 2.0f * h.y / static_cast<f32>(n),
                    2.0f * h.z / static_cast<f32>(n));
    const f32 dv = cell.x * cell.y * cell.z;
    f64 mass = 0.0;
    f64 ix = 0.0;
    f64 iy = 0.0;
    f64 iz = 0.0;
    for (int k = 0; k < n; ++k)
    {
        for (int j = 0; j < n; ++j)
        {
            for (int i = 0; i < n; ++i)
            {
                const f32 x = -h.x + (static_cast<f32>(i) + 0.5f) * cell.x;
                const f32 y = -h.y + (static_cast<f32>(j) + 0.5f) * cell.y;
                const f32 z = -h.z + (static_cast<f32>(k) + 0.5f) * cell.z;
                bool inside = false;
                if (shape.kind == ShapeKind::Capsule)
                {
                    const f32 cy = FMax(-shape.half_height, FMin(shape.half_height, y));
                    inside = x * x + (y - cy) * (y - cy) + z * z <= shape.radius * shape.radius;
                }
                else if (shape.kind == ShapeKind::Sphere)
                {
                    inside = x * x + y * y + z * z <= shape.radius * shape.radius;
                }
                else
                {
                    inside = true;
                }
                if (inside)
                {
                    const f64 w = static_cast<f64>(density) * static_cast<f64>(dv);
                    mass += w;
                    ix += w * static_cast<f64>(y * y + z * z);
                    iy += w * static_cast<f64>(x * x + z * z);
                    iz += w * static_cast<f64>(x * x + y * y);
                }
            }
        }
    }
    *mass_out = static_cast<f32>(mass);
    return Vec3(static_cast<f32>(ix), static_cast<f32>(iy), static_cast<f32>(iz));
}

}  // namespace

TEST_CASE("Mass: a sphere", "[physics][mass]")
{
    const MassProperties p = MassFromDensity(Shape::MakeSphere(0.5f), 2000.0f);
    const f32 volume = (4.0f / 3.0f) * kPi * 0.125f;
    CHECK(Close(p.volume, volume, 1e-6f));
    CHECK(Close(p.mass, 2000.0f * volume, 1e-6f));
    CHECK(Close(p.inertia.x, 0.4f * p.mass * 0.25f, 1e-6f));
    CHECK(p.inertia.x == p.inertia.y);
    CHECK(p.inertia.y == p.inertia.z);
    CHECK(p.center.x == 0.0f);
}

TEST_CASE("Mass: a box", "[physics][mass]")
{
    const MassProperties p = MassFromDensity(Shape::MakeBox(Vec3(0.5f, 1.0f, 1.5f)), 100.0f);
    CHECK(Close(p.volume, 6.0f, 1e-6f));
    CHECK(Close(p.mass, 600.0f, 1e-6f));
    // A box with sides a, b, c has the moment m (b^2 + c^2) / 12 about x; here the sides are 1, 2, 3.
    CHECK(Close(p.inertia.x, 600.0f * (4.0f + 9.0f) / 12.0f, 1e-5f));
    CHECK(Close(p.inertia.y, 600.0f * (1.0f + 9.0f) / 12.0f, 1e-5f));
    CHECK(Close(p.inertia.z, 600.0f * (1.0f + 4.0f) / 12.0f, 1e-5f));
}

TEST_CASE("Mass: a capsule without a cylinder is a sphere", "[physics][mass]")
{
    const MassProperties c = MassFromDensity(Shape::MakeCapsule(0.0f, 0.5f), 1000.0f);
    const MassProperties s = MassFromDensity(Shape::MakeSphere(0.5f), 1000.0f);
    CHECK(Close(c.mass, s.mass, 1e-5f));
    CHECK(Close(c.inertia.x, s.inertia.x, 1e-5f));
    CHECK(Close(c.inertia.y, s.inertia.y, 1e-5f));
}

TEST_CASE("Mass: a capsule matches a numeric integral", "[physics][mass]")
{
    const Shape shapes[] = {Shape::MakeCapsule(1.0f, 0.5f), Shape::MakeCapsule(0.25f, 0.75f)};
    for (const Shape& shape : shapes)
    {
        const MassProperties p = MassFromDensity(shape, 800.0f);
        f32 mass = 0.0f;
        const Vec3 numeric = NumericInertia(shape, 800.0f, 120, &mass);
        CHECK(Close(p.mass, mass, 0.01f));
        CHECK(Close(p.inertia.x, numeric.x, 0.015f));
        CHECK(Close(p.inertia.y, numeric.y, 0.015f));
        CHECK(Close(p.inertia.z, numeric.z, 0.015f));
        CHECK(p.inertia.x == p.inertia.z);
    }
}

TEST_CASE("Mass: a refused shape or density gives nothing", "[physics][mass]")
{
    CHECK(MassFromDensity(Shape::MakeSphere(-1.0f), 1.0f).mass == 0.0f);
    CHECK(MassFromDensity(Shape::MakeSphere(1.0f), 0.0f).mass == 0.0f);
    CHECK(MassFromDensity(Shape::MakeSphere(1.0f), __builtin_nanf("")).mass == 0.0f);
    CHECK(ShapeVolume(Shape::MakeBox(Vec3(0, 1, 1))) == 0.0f);
    MassProperties none;
    CHECK(none.InverseMass() == 0.0f);
    const Mat3 inverse = none.InverseInertiaBody();
    CHECK(inverse.col[0].x == 0.0f);
}

TEST_CASE("Mass: the world inertia of a rotated box", "[physics][mass]")
{
    const MassProperties p = MassFromDensity(Shape::MakeBox(Vec3(0.5f, 1.0f, 1.5f)), 100.0f);
    // Turned a quarter around Z, the body's x axis points along the world's y: the world inverse inertia swaps the x
    // and y entries.
    const Quaternion q = Quaternion::FromAxisAngle(Vec3(0, 0, 1), kPi * 0.5f);
    const Mat3 world = p.InverseInertiaWorld(q);
    CHECK(Close(world.col[0].x, 1.0f / p.inertia.y, 1e-4f));
    CHECK(Close(world.col[1].y, 1.0f / p.inertia.x, 1e-4f));
    CHECK(Close(world.col[2].z, 1.0f / p.inertia.z, 1e-4f));
    // The tensor is symmetric.
    CHECK(Close(world.col[0].y, world.col[1].x, 1e-4f));
    // Turned about a skew axis it is still the inverse of a symmetric matrix that moves a vector along its principal
    // axes: I^-1 (I w) = w.
    const Quaternion skew = Quaternion::FromAxisAngle(Vec3(1, 2, 3).Normalized(), 1.1f);
    const Mat3 r = skew.ToMat3();
    const Vec3 axis = r * Vec3(1, 0, 0);
    const Vec3 spun = p.InverseInertiaWorld(skew) * (axis * p.inertia.x);
    CHECK(Close(spun.x, axis.x, 1e-4f));
    CHECK(Close(spun.y, axis.y, 1e-4f));
    CHECK(Close(spun.z, axis.z, 1e-4f));
}

TEST_CASE("Mass: the parallel axis theorem", "[physics][mass]")
{
    const MassProperties p = MassFromDensity(Shape::MakeSphere(0.5f), 1000.0f);
    const Mat3 shifted = InertiaAbout(p, Vec3(2.0f, 0.0f, 0.0f));
    CHECK(Close(shifted.col[0].x, p.inertia.x, 1e-5f));
    CHECK(Close(shifted.col[1].y, p.inertia.y + p.mass * 4.0f, 1e-5f));
    CHECK(Close(shifted.col[2].z, p.inertia.z + p.mass * 4.0f, 1e-5f));
}

TEST_CASE("Mass: a size that overflows is refused, a denormal inertia does not rotate", "[physics][mass]")
{
    // 1e20 metres cubed overflows an f32 volume.
    const MassProperties huge = MassFromDensity(Shape::MakeBox(Vec3(1e20f, 1e20f, 1e20f)), 1.0f);
    CHECK(huge.mass == 0.0f);
    CHECK(huge.volume == 0.0f);
    MassProperties tiny;
    tiny.mass = 1.0f;
    tiny.inertia = Vec3(1e-45f, 1.0f, 1.0f);
    const Mat3 inverse = tiny.InverseInertiaBody();
    CHECK(inverse.col[0].x == 0.0f);
    CHECK(inverse.col[1].y == 1.0f);
}
