#include "physics/mass.h"

#include "lang/scalar.h"
#include "math/math_util.h"

namespace vse::physics
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

// The eigenvalues and the right-handed eigenvectors of a symmetric 3x3 matrix by Jacobi rotations (the sweeps use
// `+ - * /` and `Sqrt` only, so the result is the same on every machine).
void PrincipalAxes(const Mat3& a, Vec3* values, Mat3* vectors)
{
    f64 m[3][3];
    f64 v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (u32 i = 0; i < 3; ++i)
    {
        for (u32 j = 0; j < 3; ++j)
        {
            m[i][j] = a.col[j][i];
        }
    }
    for (u32 sweep = 0; sweep < 24; ++sweep)
    {
        f64 off = Abs(m[0][1]) + Abs(m[0][2]) + Abs(m[1][2]);
        f64 diag = Abs(m[0][0]) + Abs(m[1][1]) + Abs(m[2][2]);
        if (off <= 1e-14 * diag || off == 0.0)
        {
            break;
        }
        for (u32 p = 0; p < 2; ++p)
        {
            for (u32 q = p + 1; q < 3; ++q)
            {
                if (Abs(m[p][q]) < 1e-300)
                {
                    continue;
                }
                const f64 theta = (m[q][q] - m[p][p]) / (2.0 * m[p][q]);
                const f64 t = (theta >= 0.0 ? 1.0 : -1.0) / (Abs(theta) + Sqrt(theta * theta + 1.0));
                const f64 c = 1.0 / Sqrt(t * t + 1.0);
                const f64 s = t * c;
                for (u32 k = 0; k < 3; ++k)  // columns p and q of m
                {
                    const f64 mkp = m[k][p];
                    const f64 mkq = m[k][q];
                    m[k][p] = c * mkp - s * mkq;
                    m[k][q] = s * mkp + c * mkq;
                }
                for (u32 k = 0; k < 3; ++k)  // rows p and q of m
                {
                    const f64 mpk = m[p][k];
                    const f64 mqk = m[q][k];
                    m[p][k] = c * mpk - s * mqk;
                    m[q][k] = s * mpk + c * mqk;
                }
                for (u32 k = 0; k < 3; ++k)
                {
                    const f64 vkp = v[k][p];
                    const f64 vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    *values = Vec3(static_cast<f32>(m[0][0]), static_cast<f32>(m[1][1]), static_cast<f32>(m[2][2]));
    Vec3 e0(static_cast<f32>(v[0][0]), static_cast<f32>(v[1][0]), static_cast<f32>(v[2][0]));
    const Vec3 e1(static_cast<f32>(v[0][1]), static_cast<f32>(v[1][1]), static_cast<f32>(v[2][1]));
    const Vec3 e2(static_cast<f32>(v[0][2]), static_cast<f32>(v[1][2]), static_cast<f32>(v[2][2]));
    if (e0.Cross(e1).Dot(e2) < 0.0f)  // keep the frame right-handed
    {
        e0 = Vec3(-e0.x, -e0.y, -e0.z);
    }
    *vectors = Mat3::FromBasis(e0, e1, e2);
}

f32 ShapeVolume(const Shape& shape, const HullLibrary* hulls)
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
    case ShapeKind::Hull:
    {
        const ConvexHull* hull = hulls != nullptr ? hulls->Find(shape.hull) : nullptr;
        return hull != nullptr ? hull->Volume() : 0.0f;
    }
    case ShapeKind::Mesh:
    case ShapeKind::Compound: return 0.0f;  // a mesh has no mass; a compound's comes from its children
    }
    return 0.0f;
}

MassProperties MassFromDensity(const Shape& shape, f32 density, const HullLibrary* hulls)
{
    MassProperties out;
    if (shape.Validate() != Validation::Ok || !IsFinite(density) || !(density > 0.0f))
    {
        return out;
    }
    if (shape.kind == ShapeKind::Mesh || shape.kind == ShapeKind::Compound)
    {
        return out;  // see CompoundShape::Mass
    }
    out.volume = ShapeVolume(shape, hulls);
    if (shape.kind == ShapeKind::Hull && !(out.volume > 0.0f))
    {
        return MassProperties();  // the hull is not in the library
    }
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
    case ShapeKind::Mesh:
    case ShapeKind::Compound: break;
    case ShapeKind::Hull:
    {
        // The tensor about the centroid at density 1, scaled to this density, then turned to its principal frame.
        const ConvexHull* hull = hulls->Find(shape.hull);
        Vec3 values;
        Mat3 frame;
        PrincipalAxes(Mat3::FromBasis(hull->Inertia().col[0] * density, hull->Inertia().col[1] * density,
                                      hull->Inertia().col[2] * density),
                      &values, &frame);
        out.inertia = values;
        out.center = hull->Centroid();
        out.principal_rotation = Quaternion::FromMat3(frame).Normalized();
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

}  // namespace vse::physics
