#include "physics/mass.h"

#include "lang/scalar.h"
#include "math/math_util.h"

namespace engine_placeholder::physics
{

namespace
{

Mat3 Diagonal(const Vec3& d)
{
    return Mat3::FromBasis(Vec3(d.x, 0.0f, 0.0f), Vec3(0.0f, d.y, 0.0f), Vec3(0.0f, 0.0f, d.z));
}

f32 InverseOrZero(f32 value)
{
    // A denormal inertia has an infinite inverse: it counts as no inertia (the axis does not rotate).
    const f32 inverse = value > 0.0f ? 1.0f / value : 0.0f;
    return IsFinite(inverse) ? inverse : 0.0f;
}

}  // namespace

f32 ShapeVolume(const Shape& shape)
{
    if (shape.Validate() != Validation::Ok)
    {
        return 0.0f;
    }
    switch (shape.kind)
    {
    case ShapeKind::Sphere: return (4.0f / 3.0f) * kPi * shape.radius * shape.radius * shape.radius;
    case ShapeKind::Box: return 8.0f * shape.half_extents.x * shape.half_extents.y * shape.half_extents.z;
    case ShapeKind::Capsule:
    {
        const f32 r2 = shape.radius * shape.radius;
        return kPi * r2 * (2.0f * shape.half_height) + (4.0f / 3.0f) * kPi * r2 * shape.radius;
    }
    }
    return 0.0f;
}

MassProperties MassFromDensity(const Shape& shape, f32 density)
{
    MassProperties out;
    if (shape.Validate() != Validation::Ok || !IsFinite(density) || !(density > 0.0f))
    {
        return out;
    }
    out.volume = ShapeVolume(shape);
    out.mass = density * out.volume;
    const f32 m = out.mass;
    switch (shape.kind)
    {
    case ShapeKind::Sphere:
    {
        const f32 i = 0.4f * m * shape.radius * shape.radius;
        out.inertia = Vec3(i, i, i);
        break;
    }
    case ShapeKind::Box:
    {
        const Vec3 h = shape.half_extents;
        // The moment about an axis is m / 3 times the sum of the squares of the half extents on the other two axes.
        out.inertia = Vec3(m * (h.y * h.y + h.z * h.z) / 3.0f, m * (h.x * h.x + h.z * h.z) / 3.0f,
                           m * (h.x * h.x + h.y * h.y) / 3.0f);
        break;
    }
    case ShapeKind::Capsule:
    {
        const f32 r = shape.radius;
        const f32 hh = shape.half_height;
        const f32 r2 = r * r;
        const f32 v_cylinder = kPi * r2 * (2.0f * hh);
        const f32 v_hemisphere = (2.0f / 3.0f) * kPi * r2 * r;
        const f32 m_cylinder = density * v_cylinder;
        const f32 m_hemisphere = density * v_hemisphere;  // each of the two
        // About the axis (Y): the cylinder's m r^2 / 2 plus two hemispheres at 2/5 m r^2 each.
        const f32 iy = 0.5f * m_cylinder * r2 + 2.0f * (0.4f * m_hemisphere * r2);
        // About a perpendicular axis through the centre: the cylinder's m (3 r^2 + (2 hh)^2) / 12, and each
        // hemisphere's 83/320 m r^2 about its own centre of mass (3/8 r from the flat face) moved to the capsule's
        // centre by the parallel axis theorem (distance hh + 3 r / 8).
        const f32 d = hh + 0.375f * r;
        const f32 ix = m_cylinder * (3.0f * r2 + 4.0f * hh * hh) / 12.0f +
                       2.0f * (m_hemisphere * (83.0f / 320.0f) * r2 + m_hemisphere * d * d);
        out.inertia = Vec3(ix, iy, ix);
        break;
    }
    }
    // A huge but finite size overflows the volume or the inertia: refused like any other bad input.
    if (!IsFinite(out.volume) || !IsFinite(out.mass) || !IsFinite(out.inertia.x) || !IsFinite(out.inertia.y) ||
        !IsFinite(out.inertia.z))
    {
        return MassProperties();
    }
    return out;
}

Mat3 MassProperties::InverseInertiaBody() const
{
    const Mat3 r = principal_rotation.ToMat3();
    const Mat3 d = Diagonal(Vec3(InverseOrZero(inertia.x), InverseOrZero(inertia.y), InverseOrZero(inertia.z)));
    return r * d * r.Transpose();
}

Mat3 MassProperties::InverseInertiaWorld(const Quaternion& orientation) const
{
    const Mat3 q = orientation.ToMat3();
    return q * InverseInertiaBody() * q.Transpose();
}

Mat3 InertiaAbout(const MassProperties& properties, const Vec3& offset)
{
    const Mat3 r = properties.principal_rotation.ToMat3();
    const Mat3 body = r * Diagonal(properties.inertia) * r.Transpose();
    // The parallel axis theorem: I' = I + m (|d|^2 E - d d^T).
    const f32 d2 = offset.Dot(offset);
    const f32 m = properties.mass;
    Mat3 shift;
    for (int j = 0; j < 3; ++j)
    {
        for (int i = 0; i < 3; ++i)
        {
            const f32 e = i == j ? d2 : 0.0f;
            shift.col[j][i] = m * (e - offset[i] * offset[j]);
        }
    }
    Mat3 out;
    for (int j = 0; j < 3; ++j)
    {
        out.col[j] = body.col[j] + shift.col[j];
    }
    return out;
}

}  // namespace engine_placeholder::physics
