#pragma once

//! Shapes and the tests between them: boxes, spheres, planes, rays and the view frustum, shared by
//! the renderer's culling and the spatial module. Moved here from `engine/renderer/bounds.h` and `frustum.h`, which
//! keep `using` aliases. Boxes and spheres are closed sets: shapes that only touch overlap, and a ray that grazes a
//! face or an edge hits it.

#include "lang/traits.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "types.h"

namespace vse
{

/// A sphere, over the scalar `S` (`f32` or `f64`).
///
/// - Thread-safety: thread-compatible plain data.
template <class S> struct SphereT
{
    /// The center.
    Vec3T<S> center = Vec3T<S>(0, 0, 0);
    /// The radius; never negative.
    S radius = S(0);
};

/// An axis-aligned box over the scalar `S`. A box with `min` above `max` on some axis is empty ([[AabbT::Empty]],
/// [[AabbT::IsEmpty]]).
///
/// - Thread-safety: thread-compatible plain data.
template <class S> struct AabbT
{
    /// The corner with the smallest coordinates.
    Vec3T<S> min = Vec3T<S>(0, 0, 0);
    /// The corner with the largest coordinates; at least `min` in every component for a box that is not empty.
    Vec3T<S> max = Vec3T<S>(0, 0, 0);

    /// Returns the center of the box.
    Vec3T<S> Center() const { return (min + max) * S(0.5); }
    /// Returns half the size of the box along each axis.
    Vec3T<S> HalfExtents() const { return (max - min) * S(0.5); }
    /// Returns the size of the box along each axis.
    Vec3T<S> Extents() const { return max - min; }
    /// Returns whether the box is empty (`min` above `max` on some axis). A member, because `lang/traits.h` has a trait
    /// named `IsEmpty`.
    bool IsEmpty() const { return min.x > max.x || min.y > max.y || min.z > max.z; }

    /// Returns the empty box: `min` at plus infinity and `max` at minus infinity, so the union with any box is that
    /// box.
    static AabbT Empty();
    /// Returns the box with these corners.
    ///
    /// - Param min: the smallest corner.
    /// - Param max: the largest corner.
    /// - Returns: the box (not checked: `min` above `max` gives an empty box).
    static AabbT FromMinMax(const Vec3T<S>& min, const Vec3T<S>& max)
    {
        AabbT box;
        box.min = min;
        box.max = max;
        return box;
    }
    /// Widens a single-precision box (exact); only for the double-precision form.
    template <class T>
        requires(IsSame<S, f64> && IsSame<T, f32>)
    AabbT(const AabbT<T>& o) : min(o.min), max(o.max)
    {
    }
    /// Creates the box with both corners at the origin.
    AabbT() = default;
    /// Creates the box from its two corners (not checked, like `FromMinMax`).
    constexpr AabbT(const Vec3T<S>& lo, const Vec3T<S>& hi) : min(lo), max(hi) {}
    /// Narrows to single precision (rounds each corner to the nearest `f32`: the narrowed box may be a hair smaller
    /// or larger than the exact one); only for the double-precision form.
    ///
    /// - Fails: never.
    AabbT<f32> Narrow() const
        requires(IsSame<S, f64>)
    {
        return AabbT<f32>::FromMinMax(min.Narrow(), max.Narrow());
    }
};

/// A sphere and a box for the same object: the sphere is tested first and the box refines the result.
///
/// - Thread-safety: thread-compatible plain data.
template <class S> struct BoundsT
{
    /// The enclosing sphere.
    SphereT<S> sphere;
    /// The enclosing box.
    AabbT<S> box;
};

/// A plane `dot(normal, p) + d = 0`; points with a non-negative value are on the inside (the side the normal points
/// to). Over the scalar `S`.
///
/// - Thread-safety: thread-compatible plain data.
template <class S> struct PlaneT
{
    /// The unit normal, pointing to the inside.
    Vec3T<S> normal = Vec3T<S>(0, 1, 0);
    /// The offset: `-dot(normal, point_on_plane)`.
    S d = S(0);

    /// Returns the signed distance of a point: positive inside, negative outside.
    S Distance(const Vec3T<S>& p) const { return normal.Dot(p) + d; }

    /// Returns the plane through `point` with the normal `normal`.
    ///
    /// - Param normal: the normal; normalized here.
    /// - Param point: a point on the plane.
    /// - Returns: the plane.
    /// - Aborts: `normal` has zero length.
    static PlaneT FromPointNormal(const Vec3T<S>& point, const Vec3T<S>& normal);
};

/// A ray: the points `origin + t * direction` for `t` from 0 up. `direction` need not have unit length; distances along
/// the ray are in units of its length. The inverse direction is kept for the box (slab) test of `Intersect`, so make
/// rays with [[RayT::Make]]. Over the scalar `S`.
///
/// - Thread-safety: thread-compatible plain data.
template <class S> struct RayT
{
    /// Where the ray starts.
    Vec3T<S> origin = Vec3T<S>(0, 0, 0);
    /// The direction.
    Vec3T<S> direction = Vec3T<S>(0, 0, 1);
    /// `1 / direction` per component, infinite (with the component's sign) where the component is zero.
    Vec3T<S> inverse_direction = Vec3T<S>(0, 0, 1);

    /// Returns the ray from `origin` along `direction`, its inverse direction filled in.
    ///
    /// - Param origin: the start point.
    /// - Param direction: the direction; any length but zero.
    /// - Returns: the ray.
    /// - Aborts: `direction` is zero.
    static RayT Make(const Vec3T<S>& origin, const Vec3T<S>& direction);

    /// Returns the point at parameter `t`.
    Vec3T<S> At(S t) const { return origin + direction * t; }
};

/// How a volume relates to a frustum.
enum class Containment : u8
{
    Outside = 0,  ///< Entirely outside at least one plane: cull it.
    Intersects,   ///< Straddles a plane (or the test could not rule it out): draw it.
    Inside,       ///< Entirely inside every plane.
};

/// Six planes bounding a visible volume, built from a view-projection matrix. Over the scalar `S`: the
/// double-precision form holds the planes in world space (for a scene stored in world coordinates).
///
/// - Thread-safety: thread-compatible plain data; the tests are thread-safe.
template <class S> struct FrustumT
{
    /// Left, right, bottom, top, near, far. The vertical flip of the projection only swaps which of bottom and top is
    /// on screen, so the names are a convention.
    PlaneT<S> planes[6];

    /// Extracts the planes of the volume that `view_projection` maps to clip space (x and y in -w..w, z in 0..w:
    /// Vulkan's depth range, the one `renderer::PerspectiveVulkan` builds). The matrix is single precision; the
    /// planes are computed in `S`.
    ///
    /// - Param view_projection: the matrix.
    /// - Returns: the frustum, planes normalized.
    /// - Aborts: a plane is degenerate, as with a zero or non-finite matrix.
    static FrustumT FromViewProjection(const Mat4& view_projection);

    /// Returns the same volume in a space whose origin is `origin` of this one: a frustum built from a
    /// camera-relative matrix (the eye at the origin) becomes the world-space frustum when `origin` is the camera's
    /// world position. Only the offsets of the planes change.
    ///
    /// - Param origin: where this frustum's origin is, in the new space.
    /// - Returns: the frustum in the new space.
    /// - Fails: never.
    FrustumT Translated(const Vec3T<S>& origin) const;

    /// Widens a single-precision frustum (exact); only for the double-precision form.
    template <class T>
        requires(IsSame<S, f64> && IsSame<T, f32>)
    explicit FrustumT(const FrustumT<T>& o)
    {
        for (u32 i = 0; i < 6; ++i)
        {
            planes[i].normal = Vec3T<S>(o.planes[i].normal);
            planes[i].d = static_cast<S>(o.planes[i].d);
        }
    }
    /// Creates a frustum of six default planes.
    FrustumT() = default;

    /// Tests a sphere.
    ///
    /// - Param sphere: the sphere in the frustum's space.
    /// - Returns: `Outside` only if the sphere is entirely beyond one plane. Exact for single planes; near an edge of
    /// the frustum a sphere can be
    ///   reported `Intersects` although it is outside (conservative).
    /// - Fails: never.
    Containment TestSphere(const SphereT<S>& sphere) const;

    /// Tests an axis-aligned box.
    ///
    /// - Param box: the box in the frustum's space.
    /// - Returns: `Outside` only if the box is entirely beyond one plane (conservative near frustum corners).
    /// - Fails: never.
    Containment TestAabb(const AabbT<S>& box) const;

    /// Decides whether an object with these bounds may be visible: the sphere first, and the box only when the sphere
    /// straddles a plane.
    ///
    /// - Param bounds: the object's bounds in the frustum's space.
    /// - Returns: false if the object can be culled.
    /// - Fails: never.
    bool Visible(const BoundsT<S>& bounds) const;
};

/// The single-precision sphere.
using Sphere = SphereT<f32>;
/// The double-precision sphere: world space.
using Sphered = SphereT<f64>;
/// A sphere that encloses an object (the renderer's name for [[Sphere]]): cheap to test, loose for long thin shapes.
using BoundingSphere = Sphere;
/// The single-precision axis-aligned box.
using Aabb = AabbT<f32>;
/// The double-precision axis-aligned box: world space.
using Aabbd = AabbT<f64>;
/// The single-precision sphere-and-box bounds.
using Bounds = BoundsT<f32>;
/// The double-precision sphere-and-box bounds.
using Boundsd = BoundsT<f64>;
/// The single-precision plane.
using Plane = PlaneT<f32>;
/// The double-precision plane.
using Planed = PlaneT<f64>;
/// The single-precision ray.
using Ray = RayT<f32>;
/// The double-precision ray.
using Rayd = RayT<f64>;
/// The single-precision frustum.
using Frustum = FrustumT<f32>;
/// The double-precision frustum.
using Frustumd = FrustumT<f64>;

/// Returns the smallest box around a set of points.
///
/// - Param points: `count` points of `stride_bytes` bytes apart, each starting with three `f32`.
/// - Param count: number of points; at least 1.
/// - Param stride_bytes: distance between points in bytes, at least 12.
/// - Returns: the box.
/// - Aborts: `points` is null, `count` is 0 or `stride_bytes` is below 12.
/// - Ownership: `points` is borrowed for the call.
/// - Thread-safety: thread-safe. A pure function of its arguments.
Aabb ComputeAabb(const void* points, u32 count, u32 stride_bytes);

/// Returns bounds for a set of points: their box, and the sphere around the box center that reaches the farthest point.
///
/// The sphere encloses every point but is not the smallest possible one.
///
/// - Param points: as for [[ComputeAabb]].
/// - Param count: number of points; at least 1.
/// - Param stride_bytes: distance between points in bytes, at least 12.
/// - Returns: the bounds.
/// - Aborts: as for [[ComputeAabb]].
/// - Ownership: `points` is borrowed for the call.
/// - Thread-safety: thread-safe. A pure function of its arguments.
Bounds ComputeBounds(const void* points, u32 count, u32 stride_bytes);

/// Returns the box around a box after a transform: exactly the box of the eight transformed corners (Arvo's method). An
/// empty box stays empty.
///
/// Named `TransformAabb`, not `Transform`, because `math/transform.h` has a type of that name.
///
/// - Param box: the box in its own space.
/// - Param transform: an affine transform (the last row is 0 0 0 1).
/// - Returns: the box in the transformed space.
/// - Thread-safety: thread-safe. A pure function of its arguments.
Aabb TransformAabb(const Aabb& box, const Mat4& transform);

/// Returns the bounds of an object after a transform.
///
/// The box is [[TransformAabb]] of the box (exact for rotations by multiples of 90 degrees, otherwise larger than the
/// object); the sphere keeps its center transformed and its radius scaled by the largest axis scale.
///
/// - Param local: bounds in the object's own space.
/// - Param transform: an affine transform (the last row is 0 0 0 1).
/// - Returns: bounds in the transformed space.
/// - Thread-safety: thread-safe. A pure function of its arguments.
Bounds TransformBounds(const Bounds& local, const Mat4& transform);

/// Returns the center of a box.
template <class S> inline Vec3T<S> Center(const AabbT<S>& box)
{
    return box.Center();
}

/// Returns the smallest box containing both boxes (an empty box adds nothing).
template <class S> AabbT<S> Union(const AabbT<S>& a, const AabbT<S>& b);

/// Returns the smallest box containing the box and the point.
template <class S> AabbT<S> Union(const AabbT<S>& box, const Vec3T<S>& point);

/// Returns the common part of two boxes; it is empty ([[AabbT::IsEmpty]]) when they do not overlap.
template <class S> AabbT<S> Intersection(const AabbT<S>& a, const AabbT<S>& b);

/// Returns the box grown by `margin` on every side (a negative margin shrinks it, and may leave it empty).
template <class S> inline AabbT<S> Expand(const AabbT<S>& box, TypeIdentity<S> margin)
{
    const Vec3T<S> m(margin, margin, margin);
    return AabbT<S>::FromMinMax(box.min - m, box.max + m);
}

/// Returns whether the point is in the box (the faces included).
template <class S> inline bool Contains(const AabbT<S>& box, const Vec3T<S>& point)
{
    return point.x >= box.min.x && point.x <= box.max.x && point.y >= box.min.y && point.y <= box.max.y &&
           point.z >= box.min.z && point.z <= box.max.z;
}

/// Returns whether `inner` lies entirely inside `outer` (the faces included); an empty `inner` is inside every box.
template <class S> bool Contains(const AabbT<S>& outer, const AabbT<S>& inner);

/// Returns whether the point is in the sphere (its surface included).
template <class S> inline bool Contains(const SphereT<S>& sphere, const Vec3T<S>& point)
{
    return (point - sphere.center).LengthSquared() <= sphere.radius * sphere.radius;
}

/// Returns whether two boxes share a point (touching counts); an empty box overlaps nothing.
template <class S> bool Overlaps(const AabbT<S>& a, const AabbT<S>& b);

/// Returns whether the box and the sphere share a point (touching counts).
template <class S> bool Overlaps(const AabbT<S>& box, const SphereT<S>& sphere);

/// Returns whether two spheres share a point (touching counts).
template <class S> bool Overlaps(const SphereT<S>& a, const SphereT<S>& b);

/// Returns whether the box may be inside the frustum: false only when it is entirely beyond one plane (conservative
/// near the frustum's corners and edges, as [[FrustumT::TestAabb]]), and false for an empty box.
template <class S> inline bool Overlaps(const FrustumT<S>& frustum, const AabbT<S>& box)
{
    return !box.IsEmpty() && frustum.TestAabb(box) != Containment::Outside;
}

/// Returns whether the sphere may be inside the frustum (conservative, as [[FrustumT::TestSphere]]).
template <class S> inline bool Overlaps(const FrustumT<S>& frustum, const SphereT<S>& sphere)
{
    return frustum.TestSphere(sphere) != Containment::Outside;
}

/// Returns the surface area of the box (0 for an empty box): the cost the bounding-volume hierarchies minimize.
template <class S> S SurfaceArea(const AabbT<S>& box);

/// Returns the volume of the box (0 for an empty box).
template <class S> S Volume(const AabbT<S>& box);

/// Returns the squared distance from the point to the nearest point of the box (0 when the point is inside).
template <class S> S Distance2(const Vec3T<S>& point, const AabbT<S>& box);

/// Finds where a ray enters a box (the slab test).
///
/// - Param ray: the ray, made with [[RayT::Make]].
/// - Param box: the box.
/// - Param max_t: the farthest parameter that counts (use a large value for an unbounded ray).
/// - Param t: receives the parameter at which the ray enters the box, 0 when it starts inside; may be null.
/// - Returns: whether the ray meets the box between parameter 0 and `max_t` (a graze of a face or an edge counts).
/// - Fails: returns false for an empty box, a box entirely behind the origin, or one beyond `max_t`.
template <class S> bool Intersect(const RayT<S>& ray, const AabbT<S>& box, TypeIdentity<S> max_t, TypeIdentity<S>* t);

/// Finds where a ray first meets a sphere.
///
/// - Param ray: the ray.
/// - Param sphere: the sphere.
/// - Param max_t: the farthest parameter that counts.
/// - Param t: receives the parameter of the first point of the sphere on the ray, 0 when the ray starts inside; may be
/// null.
/// - Returns: whether the ray meets the sphere between 0 and `max_t` (a tangent counts).
/// - Fails: returns false when it does not.
template <class S>
bool Intersect(const RayT<S>& ray, const SphereT<S>& sphere, TypeIdentity<S> max_t, TypeIdentity<S>* t);

/// Finds where a ray crosses a plane.
///
/// - Param ray: the ray.
/// - Param plane: the plane.
/// - Param max_t: the farthest parameter that counts.
/// - Param t: receives the parameter of the crossing; may be null.
/// - Returns: whether the ray crosses the plane between 0 and `max_t`.
/// - Fails: returns false for a ray parallel to the plane (even one lying in it) or one that crosses it behind the
/// origin or beyond `max_t`.
template <class S>
bool Intersect(const RayT<S>& ray, const PlaneT<S>& plane, TypeIdentity<S> max_t, TypeIdentity<S>* t);

}  // namespace vse
