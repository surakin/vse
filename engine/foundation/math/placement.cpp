#include "math/placement.h"

#include "lang/scalar.h"

namespace vse
{

Placement Placement::From(const Vec3d& position, const Mat4& linear)
{
    Placement p;
    p.linear = linear;
    p.linear.col[3] = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    p.position = position;
    return p;
}

Vec3d Placement::TransformPoint(const Vec3d& local) const
{
    // Every product and sum is in double precision: the matrix entries widen exactly.
    const f64 x = static_cast<f64>(linear.col[0].x) * local.x + static_cast<f64>(linear.col[1].x) * local.y +
                  static_cast<f64>(linear.col[2].x) * local.z;
    const f64 y = static_cast<f64>(linear.col[0].y) * local.x + static_cast<f64>(linear.col[1].y) * local.y +
                  static_cast<f64>(linear.col[2].y) * local.z;
    const f64 z = static_cast<f64>(linear.col[0].z) * local.x + static_cast<f64>(linear.col[1].z) * local.y +
                  static_cast<f64>(linear.col[2].z) * local.z;
    return Vec3d(position.x + x, position.y + y, position.z + z);
}

Placement Placement::Compose(const Placement& child) const
{
    Placement out;
    out.linear = linear * child.linear;
    out.linear.col[3] = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    out.position = TransformPoint(child.position);
    return out;
}

Mat4 Placement::RelativeTo(const Vec3d& origin) const
{
    Mat4 m = linear;
    const Vec3 t = (position - origin).Narrow();
    m.col[3] = Vec4(t.x, t.y, t.z, 1.0f);
    return m;
}

Aabbd PlaceAabb(const Aabb& local, const Placement& placement)
{
    const Vec3d center = placement.TransformPoint(Vec3d(local.Center()));
    const Vec3d half(local.HalfExtents());
    f64 reach[3] = {0.0, 0.0, 0.0};
    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            reach[row] += Abs(static_cast<f64>(placement.linear.col[col][static_cast<u32>(row)])) *
                          half[static_cast<usize>(col)];
        }
    }
    const Vec3d extent(reach[0], reach[1], reach[2]);
    return Aabbd::FromMinMax(center - extent, center + extent);
}

}  // namespace vse
