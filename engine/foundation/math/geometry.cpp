#include "math/geometry.h"

#include "cstr/mem_ops.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "log/assert.h"

namespace vse
{

namespace
{

// Row `i` of a column-major matrix.
Vec4 GeometryRow(const Mat4& m, u32 i)
{
    return Vec4(m.col[0][i], m.col[1][i], m.col[2][i], m.col[3][i]);
}

// The plane of a row combination, normalized, in the scalar `S` (the matrix is single precision; its entries widen
// exactly).
template <class S> PlaneT<S> GeometryPlane(const Vec4& v)
{
    const Vec3T<S> n(static_cast<S>(v.x), static_cast<S>(v.y), static_cast<S>(v.z));
    const S len = n.Length();
    ENGINE_ASSERT(len > S(0) && IsFinite(len), "Frustum: the view-projection is degenerate (zero or not finite)");
    PlaneT<S> p;
    p.normal = n * (S(1) / len);
    p.d = static_cast<S>(v.w) / len;
    return p;
}

template <class S> S MinOf(S a, S b)
{
    return a < b ? a : b;
}
template <class S> S MaxOf(S a, S b)
{
    return a > b ? a : b;
}

}  // namespace

template <class S> AabbT<S> AabbT<S>::Empty()
{
    const S inf = Limits<S>::Infinity();
    return FromMinMax(Vec3T<S>(inf, inf, inf), Vec3T<S>(-inf, -inf, -inf));
}

template <class S> PlaneT<S> PlaneT<S>::FromPointNormal(const Vec3T<S>& point, const Vec3T<S>& normal)
{
    PlaneT<S> p;
    p.normal = normal.Normalized();
    p.d = -p.normal.Dot(point);
    return p;
}

template <class S> RayT<S> RayT<S>::Make(const Vec3T<S>& origin, const Vec3T<S>& direction)
{
    ENGINE_ASSERT(direction.x != S(0) || direction.y != S(0) || direction.z != S(0),
                  "Ray::Make: the direction is zero");
    RayT<S> ray;
    ray.origin = origin;
    ray.direction = direction;
    // IEEE division gives plus or minus infinity for a zero component, with the zero's sign; the slab test does not use
    // it for that axis.
    for (u32 k = 0; k < 3; ++k)
    {
        ray.inverse_direction[k] = S(1) / direction[k];
    }
    return ray;
}

template <class S> FrustumT<S> FrustumT<S>::FromViewProjection(const Mat4& vp)
{
    // Gribb-Hartmann: with clip = vp * p, a point is inside when -w <= x <= w, -w <= y <= w and 0 <= z <= w (Vulkan
    // depth).
    const Vec4 r0 = GeometryRow(vp, 0);
    const Vec4 r1 = GeometryRow(vp, 1);
    const Vec4 r2 = GeometryRow(vp, 2);
    const Vec4 r3 = GeometryRow(vp, 3);
    FrustumT<S> f;
    f.planes[0] = GeometryPlane<S>(r3 + r0);  // left:   w + x >= 0
    f.planes[1] = GeometryPlane<S>(r3 - r0);  // right:  w - x >= 0
    f.planes[2] = GeometryPlane<S>(r3 + r1);  // bottom: w + y >= 0
    f.planes[3] = GeometryPlane<S>(r3 - r1);  // top:    w - y >= 0
    f.planes[4] = GeometryPlane<S>(r2);       // near:   z >= 0
    f.planes[5] = GeometryPlane<S>(r3 - r2);  // far:    w - z >= 0
    return f;
}

template <class S> FrustumT<S> FrustumT<S>::Translated(const Vec3T<S>& origin) const
{
    // A point p of the old space is p + origin in the new one: n . (q - origin) + d = n . q + (d - n . origin).
    FrustumT<S> f = *this;
    for (PlaneT<S>& plane : f.planes)
    {
        plane.d = plane.d - plane.normal.Dot(origin);
    }
    return f;
}

template <class S> Containment FrustumT<S>::TestSphere(const SphereT<S>& s) const
{
    bool straddles = false;
    for (const PlaneT<S>& plane : planes)
    {
        const S distance = plane.Distance(s.center);
        if (distance < -s.radius)
        {
            return Containment::Outside;
        }
        if (distance < s.radius)
        {
            straddles = true;
        }
    }
    return straddles ? Containment::Intersects : Containment::Inside;
}

template <class S> Containment FrustumT<S>::TestAabb(const AabbT<S>& box) const
{
    const Vec3T<S> center = box.Center();
    const Vec3T<S> half = box.HalfExtents();
    bool straddles = false;
    for (const PlaneT<S>& plane : planes)
    {
        // The box's extent along the plane normal: the farthest corner is `reach` away from the center in each
        // direction.
        const S reach = Abs(plane.normal.x) * half.x + Abs(plane.normal.y) * half.y + Abs(plane.normal.z) * half.z;
        const S distance = plane.Distance(center);
        if (distance < -reach)
        {
            return Containment::Outside;
        }
        if (distance < reach)
        {
            straddles = true;
        }
    }
    return straddles ? Containment::Intersects : Containment::Inside;
}

template <class S> bool FrustumT<S>::Visible(const BoundsT<S>& bounds) const
{
    switch (TestSphere(bounds.sphere))
    {
    case Containment::Outside: return false;
    case Containment::Inside: return true;
    case Containment::Intersects: break;
    }
    return TestAabb(bounds.box) != Containment::Outside;
}

template struct AabbT<f32>;
template struct AabbT<f64>;
template struct PlaneT<f32>;
template struct PlaneT<f64>;
template struct RayT<f32>;
template struct RayT<f64>;
template struct FrustumT<f32>;
template struct FrustumT<f64>;

Aabb ComputeAabb(const void* points, u32 count, u32 stride_bytes)
{
    ENGINE_ASSERT(points != nullptr && count > 0 && stride_bytes >= 12,
                  "ComputeAabb: need at least one point of at least 12 bytes");
    const auto* bytes = static_cast<const byte*>(points);
    Aabb box;
    for (u32 i = 0; i < count; ++i)
    {
        f32 p[3];
        MemCopy(p, bytes + static_cast<usize>(i) * stride_bytes, sizeof(p));
        if (i == 0)
        {
            box.min = box.max = Vec3(p[0], p[1], p[2]);
            continue;
        }
        for (u32 k = 0; k < 3; ++k)
        {
            if (p[k] < box.min[k])
            {
                box.min[k] = p[k];
            }
            if (p[k] > box.max[k])
            {
                box.max[k] = p[k];
            }
        }
    }
    return box;
}

Bounds ComputeBounds(const void* points, u32 count, u32 stride_bytes)
{
    Bounds bounds;
    bounds.box = ComputeAabb(points, count, stride_bytes);
    bounds.sphere.center = bounds.box.Center();
    f32 farthest = 0.0f;
    const auto* bytes = static_cast<const byte*>(points);
    for (u32 i = 0; i < count; ++i)
    {
        f32 p[3];
        MemCopy(p, bytes + static_cast<usize>(i) * stride_bytes, sizeof(p));
        const f32 d2 = (Vec3(p[0], p[1], p[2]) - bounds.sphere.center).LengthSquared();
        if (d2 > farthest)
        {
            farthest = d2;
        }
    }
    bounds.sphere.radius = Sqrt(farthest);
    return bounds;
}

Aabb TransformAabb(const Aabb& box, const Mat4& m)
{
    if (box.IsEmpty())
    {
        return Aabb::Empty();  // the infinite corners of Aabb::Empty() would give a NaN box, which is not empty
    }
    // Transform the center, and spread the half extents by the absolute value of the upper 3x3 (Arvo's method): each
    // new half extent is the largest reach of the transformed box along that axis, so the result is exactly the box of
    // the eight transformed corners.
    const Vec3 center = box.Center();
    const Vec3 half = box.HalfExtents();
    const Vec4 c = m * Vec4(center, 1.0f);
    Vec3 new_half(0, 0, 0);
    for (u32 row = 0; row < 3; ++row)
    {
        f32 sum = 0.0f;
        for (u32 col = 0; col < 3; ++col)
        {
            const f32 e = m.col[col][row];
            sum += (e < 0 ? -e : e) * half[col];
        }
        new_half[row] = sum;
    }
    return Aabb::FromMinMax(Vec3(c.x, c.y, c.z) - new_half, Vec3(c.x, c.y, c.z) + new_half);
}

namespace
{
// The largest singular value of the upper 3x3 of `m` (the most it stretches any vector), from the largest eigenvalue of
// the symmetric A = M^T M by the trigonometric solution. Exact for any rotation, scale, shear or product of them (the
// largest column length is only exact for rotate-then-scale).
f32 LargestStretch(const Mat4& m)
{
    f64 c[3][3];
    for (u32 i = 0; i < 3; ++i)
    {
        c[i][0] = static_cast<f64>(m.col[i].x);
        c[i][1] = static_cast<f64>(m.col[i].y);
        c[i][2] = static_cast<f64>(m.col[i].z);
    }
    f64 a[3][3];
    for (u32 i = 0; i < 3; ++i)
    {
        for (u32 j = 0; j < 3; ++j)
        {
            f64 sum = 0.0;
            for (u32 k = 0; k < 3; ++k)
            {
                sum += c[i][k] * c[j][k];
            }
            a[i][j] = sum;
        }
    }
    const f64 off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
    const f64 q = (a[0][0] + a[1][1] + a[2][2]) / 3.0;
    f64 largest;
    if (off == 0.0)
    {
        largest = a[0][0] > a[1][1] ? (a[0][0] > a[2][2] ? a[0][0] : a[2][2]) : (a[1][1] > a[2][2] ? a[1][1] : a[2][2]);
    }
    else
    {
        const f64 d0 = a[0][0] - q, d1 = a[1][1] - q, d2 = a[2][2] - q;
        const f64 p = Sqrt((d0 * d0 + d1 * d1 + d2 * d2 + 2.0 * off) / 6.0);
        const f64 b00 = d0 / p, b11 = d1 / p, b22 = d2 / p, b01 = a[0][1] / p, b02 = a[0][2] / p, b12 = a[1][2] / p;
        f64 r = (b00 * (b11 * b22 - b12 * b12) - b01 * (b01 * b22 - b12 * b02) + b02 * (b01 * b12 - b11 * b02)) / 2.0;
        r = r < -1.0 ? -1.0 : (r > 1.0 ? 1.0 : r);
        largest = q + 2.0 * p * Cos(Acos(r) / 3.0);
    }
    if (largest < 0.0)
    {
        largest = 0.0;
    }
    return static_cast<f32>(Sqrt(largest) *
                            (1.0 + 1e-6));  // a hair up: the sphere must never be smaller than the true one
}
}  // namespace

Bounds TransformBounds(const Bounds& local, const Mat4& m)
{
    Bounds out;
    out.box = TransformAabb(local.box, m);
    const Vec4 sc = m * Vec4(local.sphere.center, 1.0f);
    out.sphere.center = Vec3(sc.x, sc.y, sc.z);
    out.sphere.radius = local.sphere.radius * LargestStretch(m);
    return out;
}

template <class S> AabbT<S> Union(const AabbT<S>& a, const AabbT<S>& b)
{
    if (a.IsEmpty())
    {
        return b;
    }
    if (b.IsEmpty())
    {
        return a;
    }
    return AabbT<S>::FromMinMax(Vec3T<S>(MinOf(a.min.x, b.min.x), MinOf(a.min.y, b.min.y), MinOf(a.min.z, b.min.z)),
                                Vec3T<S>(MaxOf(a.max.x, b.max.x), MaxOf(a.max.y, b.max.y), MaxOf(a.max.z, b.max.z)));
}

template <class S> AabbT<S> Union(const AabbT<S>& box, const Vec3T<S>& point)
{
    return Union(box, AabbT<S>::FromMinMax(point, point));
}

template <class S> AabbT<S> Intersection(const AabbT<S>& a, const AabbT<S>& b)
{
    return AabbT<S>::FromMinMax(Vec3T<S>(MaxOf(a.min.x, b.min.x), MaxOf(a.min.y, b.min.y), MaxOf(a.min.z, b.min.z)),
                                Vec3T<S>(MinOf(a.max.x, b.max.x), MinOf(a.max.y, b.max.y), MinOf(a.max.z, b.max.z)));
}

template <class S> bool Contains(const AabbT<S>& outer, const AabbT<S>& inner)
{
    if (inner.IsEmpty())
    {
        return true;
    }
    return inner.min.x >= outer.min.x && inner.min.y >= outer.min.y && inner.min.z >= outer.min.z &&
           inner.max.x <= outer.max.x && inner.max.y <= outer.max.y && inner.max.z <= outer.max.z;
}

template <class S> bool Overlaps(const AabbT<S>& a, const AabbT<S>& b)
{
    if (a.IsEmpty() || b.IsEmpty())
    {
        return false;
    }
    return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y && a.min.z <= b.max.z &&
           b.min.z <= a.max.z;
}

template <class S> bool Overlaps(const AabbT<S>& box, const SphereT<S>& sphere)
{
    if (box.IsEmpty())
    {
        return false;
    }
    return Distance2(sphere.center, box) <= sphere.radius * sphere.radius;
}

template <class S> bool Overlaps(const SphereT<S>& a, const SphereT<S>& b)
{
    const S reach = a.radius + b.radius;
    return (a.center - b.center).LengthSquared() <= reach * reach;
}

template <class S> S SurfaceArea(const AabbT<S>& box)
{
    if (box.IsEmpty())
    {
        return S(0);
    }
    const Vec3T<S> e = box.Extents();
    return S(2) * (e.x * e.y + e.y * e.z + e.z * e.x);
}

template <class S> S Volume(const AabbT<S>& box)
{
    if (box.IsEmpty())
    {
        return S(0);
    }
    const Vec3T<S> e = box.Extents();
    return e.x * e.y * e.z;
}

template <class S> S Distance2(const Vec3T<S>& point, const AabbT<S>& box)
{
    S sum = S(0);
    for (u32 k = 0; k < 3; ++k)
    {
        const S v = point[k];
        S gap = S(0);
        if (v < box.min[k])
        {
            gap = box.min[k] - v;
        }
        else if (v > box.max[k])
        {
            gap = v - box.max[k];
        }
        sum += gap * gap;
    }
    return sum;
}

template <class S> bool Intersect(const RayT<S>& ray, const AabbT<S>& box, TypeIdentity<S> max_t, TypeIdentity<S>* t)
{
    if (box.IsEmpty() || max_t < S(0))
    {
        return false;
    }
    S enter = S(0);
    S leave = max_t;
    for (u32 k = 0; k < 3; ++k)
    {
        const S o = ray.origin[k];
        if (ray.direction[k] == S(0) || !IsFinite(ray.inverse_direction[k]))
        {  // (a subnormal direction has an infinite inverse: treated as parallel)
            // Parallel to this pair of faces: the ray is inside the slab for all t, or never (0 * infinity would be
            // NaN, so not computed).
            if (o < box.min[k] || o > box.max[k])
            {
                return false;
            }
            continue;
        }
        const S inv = ray.inverse_direction[k];
        S near_t = (box.min[k] - o) * inv;
        S far_t = (box.max[k] - o) * inv;
        if (near_t > far_t)
        {
            const S swap = near_t;
            near_t = far_t;
            far_t = swap;
        }
        enter = MaxOf(enter, near_t);
        leave = MinOf(leave, far_t);
        if (enter > leave)
        {
            return false;
        }
    }
    if (t != nullptr)
    {
        *t = enter;
    }
    return true;
}

template <class S>
bool Intersect(const RayT<S>& ray, const SphereT<S>& sphere, TypeIdentity<S> max_t, TypeIdentity<S>* t)
{
    if (max_t < S(0))
    {
        return false;
    }
    const Vec3T<S> m = ray.origin - sphere.center;
    const S c = m.LengthSquared() - sphere.radius * sphere.radius;
    if (c <= S(0))
    {  // the ray starts inside (or on) the sphere
        if (t != nullptr)
        {
            *t = S(0);
        }
        return true;
    }
    const S b = m.Dot(ray.direction);
    if (b > S(0))
    {
        return false;  // outside and pointing away
    }
    const S a = ray.direction.LengthSquared();
    const S discriminant = b * b - a * c;
    if (discriminant < S(0))
    {
        return false;
    }
    const S hit = (-b - Sqrt(discriminant)) / a;
    if (hit > max_t)
    {
        return false;
    }
    if (t != nullptr)
    {
        *t = hit < S(0) ? S(0) : hit;
    }
    return true;
}

template <class S> bool Intersect(const RayT<S>& ray, const PlaneT<S>& plane, TypeIdentity<S> max_t, TypeIdentity<S>* t)
{
    const S denominator = plane.normal.Dot(ray.direction);
    if (denominator == S(0))
    {
        return false;
    }
    const S hit = -plane.Distance(ray.origin) / denominator;
    if (hit < S(0) || hit > max_t)
    {
        return false;
    }
    if (t != nullptr)
    {
        *t = hit;
    }
    return true;
}

#define VSE_GEOMETRY_INSTANTIATE(S)                                                                                    \
    template AabbT<S> Union(const AabbT<S>&, const AabbT<S>&);                                                         \
    template AabbT<S> Union(const AabbT<S>&, const Vec3T<S>&);                                                         \
    template AabbT<S> Intersection(const AabbT<S>&, const AabbT<S>&);                                                  \
    template bool Contains(const AabbT<S>&, const AabbT<S>&);                                                          \
    template bool Overlaps(const AabbT<S>&, const AabbT<S>&);                                                          \
    template bool Overlaps(const AabbT<S>&, const SphereT<S>&);                                                        \
    template bool Overlaps(const SphereT<S>&, const SphereT<S>&);                                                      \
    template S SurfaceArea(const AabbT<S>&);                                                                           \
    template S Volume(const AabbT<S>&);                                                                                \
    template S Distance2(const Vec3T<S>&, const AabbT<S>&);                                                            \
    template bool Intersect(const RayT<S>&, const AabbT<S>&, TypeIdentity<S>, TypeIdentity<S>*);                       \
    template bool Intersect(const RayT<S>&, const SphereT<S>&, TypeIdentity<S>, TypeIdentity<S>*);                     \
    template bool Intersect(const RayT<S>&, const PlaneT<S>&, TypeIdentity<S>, TypeIdentity<S>*);
VSE_GEOMETRY_INSTANTIATE(f32)
VSE_GEOMETRY_INSTANTIATE(f64)
#undef VSE_GEOMETRY_INSTANTIATE

}  // namespace vse
