#include "math/placement.h"

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

}  // namespace vse
