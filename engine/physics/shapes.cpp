#include "physics/shapes.h"

#include "lang/scalar.h"

namespace engine_placeholder::physics
{

Shape Shape::MakeSphere(f32 radius)
{
    Shape shape;
    shape.kind = ShapeKind::Sphere;
    shape.radius = radius;
    shape.half_height = 0.0f;
    shape.half_extents = Vec3(radius, radius, radius);
    return shape;
}

Shape Shape::MakeBox(const Vec3& half_extents)
{
    Shape shape;
    shape.kind = ShapeKind::Box;
    shape.radius = 0.0f;
    shape.half_height = 0.0f;
    shape.half_extents = half_extents;
    return shape;
}

Shape Shape::MakeCapsule(f32 half_height, f32 radius)
{
    Shape shape;
    shape.kind = ShapeKind::Capsule;
    shape.radius = radius;
    shape.half_height = half_height;
    shape.half_extents = Vec3(radius, half_height + radius, radius);
    return shape;
}

Shape Shape::MakeHull(u32 hull, const Vec3& extent)
{
    Shape shape;
    shape.kind = ShapeKind::Hull;
    shape.radius = 0.0f;
    shape.half_height = 0.0f;
    shape.half_extents = extent;
    shape.hull = hull;
    return shape;
}

Validation Shape::Validate() const
{
    switch (kind)
    {
    case ShapeKind::Sphere:
        if (!IsFinite(radius))
        {
            return Validation::NotFinite;
        }
        return radius > 0.0f ? Validation::Ok : Validation::NotPositive;
    case ShapeKind::Hull:
        if (hull == 0)
        {
            return Validation::OutOfRange;
        }
        [[fallthrough]];
    case ShapeKind::Box:
        if (!IsFinite(half_extents.x) || !IsFinite(half_extents.y) || !IsFinite(half_extents.z))
        {
            return Validation::NotFinite;
        }
        return half_extents.x > 0.0f && half_extents.y > 0.0f && half_extents.z > 0.0f ? Validation::Ok
                                                                                       : Validation::NotPositive;
    case ShapeKind::Capsule:
        if (!IsFinite(radius) || !IsFinite(half_height))
        {
            return Validation::NotFinite;
        }
        return radius > 0.0f && half_height >= 0.0f ? Validation::Ok : Validation::NotPositive;
    }
    return Validation::UnknownKind;
}

Vec3 Shape::LocalHalfExtents() const
{
    switch (kind)
    {
    case ShapeKind::Sphere: return Vec3(radius, radius, radius);
    case ShapeKind::Box:
    case ShapeKind::Hull: return half_extents;
    case ShapeKind::Capsule: return Vec3(radius, half_height + radius, radius);
    }
    return Vec3(0.0f, 0.0f, 0.0f);
}

Aabb Shape::Bounds(const Vec3& position, const Quaternion& rotation) const
{
    if (Validate() != Validation::Ok)
    {
        return Aabb{position, position};
    }
    const Mat3 r = rotation.ToMat3();
    Vec3 extent;
    switch (kind)
    {
    case ShapeKind::Sphere: extent = Vec3(radius, radius, radius); break;
    case ShapeKind::Box:
    case ShapeKind::Hull:
        // The extent of a rotated box on axis i is the sum over its own axes j of |R(i, j)| * half_extent(j);
        // `r.col[j][i]` is element (i, j).
        extent = Vec3(
            Abs(r.col[0].x) * half_extents.x + Abs(r.col[1].x) * half_extents.y + Abs(r.col[2].x) * half_extents.z,
            Abs(r.col[0].y) * half_extents.x + Abs(r.col[1].y) * half_extents.y + Abs(r.col[2].y) * half_extents.z,
            Abs(r.col[0].z) * half_extents.x + Abs(r.col[1].z) * half_extents.y + Abs(r.col[2].z) * half_extents.z);
        break;
    case ShapeKind::Capsule:
    {
        // The capsule's axis in the world is the second column; its extent is the axis's absolute value times the half
        // height, plus the radius.
        const Vec3 axis = r.col[1];
        extent = Vec3(Abs(axis.x) * half_height + radius, Abs(axis.y) * half_height + radius,
                      Abs(axis.z) * half_height + radius);
        break;
    }
    }
    return Aabb{position - extent, position + extent};
}

}  // namespace engine_placeholder::physics
