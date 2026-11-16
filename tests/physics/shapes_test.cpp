// Shapes, bounds and validation (Phase 25 Day 375).
#include <catch2/catch_test_macros.hpp>

#include "lang/scalar.h"
#include "math/math_util.h"
#include "physics/components.h"
#include "physics/shapes.h"
#include "test_values.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;
using engine_placeholder::physics::testing::kNaN;

namespace
{

bool Near(f32 a, f32 b, f32 relative)
{
    return Abs(a - b) <= relative * (Abs(a) > Abs(b) ? Abs(a) : Abs(b)) + 1e-9f;
}

Quaternion Tilt(f32 x, f32 y, f32 z, f32 angle)
{
    return Quaternion::FromAxisAngle(Vec3(x, y, z).Normalized(), angle);
}

// The point of the shape's surface at the parameters (u, v), in the shape's space.
Vec3 SurfacePoint(const Shape& shape, f32 u, f32 v)
{
    // u and v are in [0, 1].
    const f32 phi = 2.0f * kPi * u;
    const f32 cos_theta = 1.0f - 2.0f * v;
    const f32 sin_theta = Sqrt(1.0f - cos_theta * cos_theta);
    const Vec3 dir(sin_theta * Cos(phi), cos_theta, sin_theta * Sin(phi));
    switch (shape.kind)
    {
    case ShapeKind::Sphere: return dir * shape.radius;
    case ShapeKind::Capsule:
        return dir * shape.radius + Vec3(0.0f, dir.y >= 0.0f ? shape.half_height : -shape.half_height, 0.0f);
    case ShapeKind::Box: break;
    }
    // A point of the box's boundary: the direction scaled to the box's faces.
    const f32 m = FMax(Abs(dir.x) / shape.half_extents.x,
                       FMax(Abs(dir.y) / shape.half_extents.y, Abs(dir.z) / shape.half_extents.z));
    return dir * (1.0f / m);
}

}  // namespace

TEST_CASE("Shape: validation refuses what a host can send", "[physics][shapes]")
{
    CHECK(Shape::MakeSphere(1.0f).Validate() == Validation::Ok);
    CHECK(Shape::MakeSphere(0.0f).Validate() == Validation::NotPositive);
    CHECK(Shape::MakeSphere(-1.0f).Validate() == Validation::NotPositive);
    CHECK(Shape::MakeSphere(kNaN).Validate() == Validation::NotFinite);
    CHECK(Shape::MakeBox(Vec3(1, 2, 3)).Validate() == Validation::Ok);
    CHECK(Shape::MakeBox(Vec3(1, 0, 3)).Validate() == Validation::NotPositive);
    CHECK(Shape::MakeBox(Vec3(1, kNaN, 3)).Validate() == Validation::NotFinite);
    CHECK(Shape::MakeCapsule(0.0f, 0.5f).Validate() == Validation::Ok);
    CHECK(Shape::MakeCapsule(-0.1f, 0.5f).Validate() == Validation::NotPositive);
    CHECK(Shape::MakeCapsule(1.0f, 0.0f).Validate() == Validation::NotPositive);
    CHECK(Shape::MakeCapsule(kNaN, 0.5f).Validate() == Validation::NotFinite);
    Shape unknown;
    unknown.kind = static_cast<ShapeKind>(200);
    CHECK(unknown.Validate() == Validation::UnknownKind);
}

TEST_CASE("Shape: an invalid shape has an empty box at the position", "[physics][shapes]")
{
    const Aabb box = Shape::MakeSphere(-1.0f).Bounds(Vec3(1, 2, 3), Quaternion::Identity());
    CHECK(box.min.x == 1.0f);
    CHECK(box.max.z == 3.0f);
}

TEST_CASE("Shape: bounds of an unrotated shape", "[physics][shapes]")
{
    const Aabb s = Shape::MakeSphere(2.0f).Bounds(Vec3(1, 0, 0), Quaternion::Identity());
    CHECK(s.min.x == -1.0f);
    CHECK(s.max.x == 3.0f);
    CHECK(s.max.y == 2.0f);
    const Aabb c = Shape::MakeCapsule(1.0f, 0.5f).Bounds(Vec3(0, 0, 0), Quaternion::Identity());
    CHECK(c.max.y == 1.5f);
    CHECK(c.max.x == 0.5f);
    const Aabb b = Shape::MakeBox(Vec3(1, 2, 3)).Bounds(Vec3(0, 0, 0), Quaternion::Identity());
    CHECK(b.max.x == 1.0f);
    CHECK(b.max.y == 2.0f);
    CHECK(b.min.z == -3.0f);
}

TEST_CASE("Shape: bounds under rotation contain a dense sample of the surface", "[physics][shapes]")
{
    const Shape shapes[] = {Shape::MakeSphere(0.7f), Shape::MakeBox(Vec3(0.5f, 1.0f, 1.5f)),
                            Shape::MakeCapsule(0.8f, 0.3f)};
    const Quaternion rotations[] = {Quaternion::Identity(), Tilt(1, 0, 0, 0.7f), Tilt(1, 2, 3, 2.1f),
                                    Tilt(0, 0, 1, kPi * 0.5f)};
    const Vec3 position(3.0f, -2.0f, 5.0f);
    for (const Shape& shape : shapes)
    {
        for (const Quaternion& q : rotations)
        {
            const Aabb box = shape.Bounds(position, q);
            const Mat3 r = q.ToMat3();
            Aabb seen{position, position};
            for (int i = 0; i <= 40; ++i)
            {
                for (int j = 0; j <= 40; ++j)
                {
                    const Vec3 local = SurfacePoint(shape, static_cast<f32>(i) / 40.0f, static_cast<f32>(j) / 40.0f);
                    const Vec3 world = position + r * local;
                    CHECK(world.x >= box.min.x - 1e-4f);
                    CHECK(world.y >= box.min.y - 1e-4f);
                    CHECK(world.z >= box.min.z - 1e-4f);
                    CHECK(world.x <= box.max.x + 1e-4f);
                    CHECK(world.y <= box.max.y + 1e-4f);
                    CHECK(world.z <= box.max.z + 1e-4f);
                    seen.min = Vec3(FMin(seen.min.x, world.x), FMin(seen.min.y, world.y), FMin(seen.min.z, world.z));
                    seen.max = Vec3(FMax(seen.max.x, world.x), FMax(seen.max.y, world.y), FMax(seen.max.z, world.z));
                }
            }
            // Tight: the sampled points reach within a few percent of the box on every side (the sample is dense, not
            // exhaustive).
            const Vec3 size = box.max - box.min;
            CHECK(seen.max.x >= box.max.x - 0.06f * size.x);
            CHECK(seen.max.y >= box.max.y - 0.06f * size.y);
            CHECK(seen.max.z >= box.max.z - 0.06f * size.z);
            CHECK(seen.min.x <= box.min.x + 0.06f * size.x);
            CHECK(seen.min.y <= box.min.y + 0.06f * size.y);
            CHECK(seen.min.z <= box.min.z + 0.06f * size.z);
        }
    }
}

TEST_CASE("Shape: local half extents", "[physics][shapes]")
{
    CHECK(Shape::MakeCapsule(1.0f, 0.5f).LocalHalfExtents().y == 1.5f);
    CHECK(Shape::MakeSphere(2.0f).LocalHalfExtents().x == 2.0f);
    CHECK(Shape::MakeBox(Vec3(1, 2, 3)).LocalHalfExtents().z == 3.0f);
}

TEST_CASE("Components: the material, body, velocity and collider are checked", "[physics][components]")
{
    PhysicsMaterial m;
    CHECK(m.Validate() == Validation::Ok);
    m.restitution = 1.5f;
    CHECK(m.Validate() == Validation::OutOfRange);
    m.restitution = 0.2f;
    m.friction = -1.0f;
    CHECK(m.Validate() == Validation::OutOfRange);
    m.friction = 0.5f;
    m.density = 0.0f;
    CHECK(m.Validate() == Validation::NotPositive);
    m.density = kNaN;
    CHECK(m.Validate() == Validation::NotFinite);
    m.density = 1.0f;
    m.friction_rule = static_cast<CombineRule>(9);
    CHECK(m.Validate() == Validation::UnknownKind);

    RigidBody b;
    CHECK(b.Validate() == Validation::Ok);
    b.mass = -1.0f;
    CHECK(b.Validate() == Validation::OutOfRange);
    b.mass = 1.0f;
    b.linear_damping = kNaN;
    CHECK(b.Validate() == Validation::NotFinite);
    b.linear_damping = 0.0f;
    b.flags = 1u << 9;
    CHECK(b.Validate() == Validation::UnknownKind);
    b.flags = kBodyLockRotation | kBodyContinuous;
    b.type = static_cast<BodyType>(7);
    CHECK(b.Validate() == Validation::UnknownKind);

    Velocity v;
    CHECK(v.Validate() == Validation::Ok);
    v.angular.y = kNaN;
    CHECK(v.Validate() == Validation::NotFinite);

    Collider c;
    CHECK(c.Validate() == Validation::Ok);
    c.layer = 32;
    CHECK(c.Validate() == Validation::OutOfRange);
    c.layer = 3;
    c.rotation = Quaternion(0.0f, 0.0f, 0.0f, 2.0f);
    CHECK(c.Validate() == Validation::NotNormalized);
    c.rotation = Quaternion::Identity();
    c.shape = Shape::MakeBox(Vec3(1, 1, -1));
    CHECK(c.Validate() == Validation::NotPositive);
}

TEST_CASE("Components: combine rules do not depend on the order", "[physics][components]")
{
    CHECK(Combine(CombineRule::Average, 0.2f, 0.6f) == 0.4f);
    CHECK(Combine(CombineRule::Minimum, 0.2f, 0.6f) == 0.2f);
    CHECK(Combine(CombineRule::Maximum, 0.2f, 0.6f) == 0.6f);
    CHECK(Near(Combine(CombineRule::Multiply, 0.5f, 0.5f), 0.25f, 1e-6f));
    CHECK(CombineRuleFor(CombineRule::Average, CombineRule::Multiply) ==
          CombineRuleFor(CombineRule::Multiply, CombineRule::Average));
    CHECK(CombineRuleFor(CombineRule::Minimum, CombineRule::Maximum) == CombineRule::Maximum);
}

TEST_CASE("Validation: every result has a name", "[physics][components]")
{
    CHECK(ValidationName(Validation::Ok)[0] == 'o');
    CHECK(ValidationName(Validation::NotNormalized)[0] == 'n');
    CHECK(ValidationName(static_cast<Validation>(99))[0] == 'u');
}
