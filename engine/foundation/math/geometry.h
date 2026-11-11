#pragma once

//! Shapes and the tests between them (Phase 18 Day 238): boxes, spheres, planes, rays and the view frustum, shared by
//! the renderer's culling and the spatial module. Moved here from `engine/renderer/bounds.h` and `frustum.h`, which
//! keep `using` aliases. Boxes and spheres are closed sets: shapes that only touch overlap, and a ray that grazes a
//! face or an edge hits it.

#include "math/matrix.h"
#include "math/vector.h"
#include "types.h"

namespace engine_placeholder
{

/// A sphere.
///
/// - Thread-safety: thread-compatible plain data.
struct Sphere
{
    /// The center.
    Vec3 center = Vec3(0, 0, 0);
    /// The radius; never negative.
    f32 radius = 0.0f;
};

/// A sphere that encloses an object (the renderer's name for [[Sphere]]): cheap to test, loose for long thin shapes.
using BoundingSphere = Sphere;

/// An axis-aligned box. A box with `min` above `max` on some axis is empty ([[Aabb::Empty]], [[Aabb::IsEmpty]]).
///
/// - Thread-safety: thread-compatible plain data.
struct Aabb
{
    /// The corner with the smallest coordinates.
    Vec3 min = Vec3(0, 0, 0);
    /// The corner with the largest coordinates; at least `min` in every component for a box that is not empty.
    Vec3 max = Vec3(0, 0, 0);

    /// Returns the center of the box.
    Vec3 Center() const { return (min + max) * 0.5f; }
    /// Returns half the size of the box along each axis.
    Vec3 HalfExtents() const { return (max - min) * 0.5f; }
    /// Returns the size of the box along each axis.
    Vec3 Extents() const { return max - min; }
    /// Returns whether the box is empty (`min` above `max` on some axis). A member, because `lang/traits.h` has a trait
    /// named `IsEmpty`.
    bool IsEmpty() const { return min.x > max.x || min.y > max.y || min.z > max.z; }

    /// Returns the empty box: `min` at plus infinity and `max` at minus infinity, so the union with any box is that
    /// box.
    static Aabb Empty();
    /// Returns the box with these corners.
    ///
    /// - Param min: the smallest corner.
    /// - Param max: the largest corner.
    /// - Returns: the box (not checked: `min` above `max` gives an empty box).
    static Aabb FromMinMax(const Vec3& min, const Vec3& max)
    {
        Aabb box;
        box.min = min;
        box.max = max;
        return box;
    }
};

/// A sphere and a box for the same object: the sphere is tested first and the box refines the result.
///
/// - Thread-safety: thread-compatible plain data.
struct Bounds
{
    /// The enclosing sphere.
    Sphere sphere;
    /// The enclosing box.
    Aabb box;
};

/// A plane `dot(normal, p) + d = 0`; points with a non-negative value are on the inside (the side the normal points
/// to).
///
/// - Thread-safety: thread-compatible plain data.
struct Plane
{
    /// The unit normal, pointing to the inside.
    Vec3 normal = Vec3(0, 1, 0);
    /// The offset: `-dot(normal, point_on_plane)`.
    f32 d = 0.0f;

    /// Returns the signed distance of a point: positive inside, negative outside.
    f32 Distance(const Vec3& p) const { return normal.Dot(p) + d; }

    /// Returns the plane through `point` with the normal `normal`.
    ///
    /// - Param normal: the normal; normalized here.
    /// - Param point: a point on the plane.
    /// - Returns: the plane.
    /// - Aborts: `normal` has zero length.
    static Plane FromPointNormal(const Vec3& point, const Vec3& normal);
};

/// A ray: the points `origin + t * direction` for `t` from 0 up. `direction` need not have unit length; distances along
/// the ray are in units of its length. The inverse direction is kept for the box (slab) test of `Intersect`, so make
/// rays with [[Ray::Make]].
///
/// - Thread-safety: thread-compatible plain data.
struct Ray
{
    /// Where the ray starts.
    Vec3 origin = Vec3(0, 0, 0);
    /// The direction.
    Vec3 direction = Vec3(0, 0, 1);
    /// `1 / direction` per component, infinite (with the component's sign) where the component is zero.
    Vec3 inverse_direction = Vec3(0, 0, 1);

    /// Returns the ray from `origin` along `direction`, its inverse direction filled in.
    ///
    /// - Param origin: the start point.
    /// - Param direction: the direction; any length but zero.
    /// - Returns: the ray.
    /// - Aborts: `direction` is zero.
    static Ray Make(const Vec3& origin, const Vec3& direction);

    /// Returns the point at parameter `t`.
    Vec3 At(f32 t) const { return origin + direction * t; }
};

/// How a volume relates to a frustum.
enum class Containment : u8
{
    Outside = 0,  ///< Entirely outside at least one plane: cull it.
    Intersects,   ///< Straddles a plane (or the test could not rule it out): draw it.
    Inside,       ///< Entirely inside every plane.
};

/// Six planes bounding a visible volume, built from a view-projection matrix.
///
/// - Thread-safety: thread-compatible plain data; the tests are thread-safe.
struct Frustum
{
    /// Left, right, bottom, top, near, far. The vertical flip of the projection only swaps which of bottom and top is
    /// on screen, so the names are a convention.
    Plane planes[6];

    /// Extracts the planes of the volume that `view_projection` maps to clip space (x and y in -w..w, z in 0..w:
    /// Vulkan's depth range, the one `renderer::PerspectiveVulkan` builds).
    ///
    /// - Param view_projection: the matrix.
    /// - Returns: the frustum, planes normalized.
    /// - Aborts: a plane is degenerate, as with a zero or non-finite matrix.
    static Frustum FromViewProjection(const Mat4& view_projection);

    /// Tests a sphere.
    ///
    /// - Param sphere: the sphere in the frustum's space.
    /// - Returns: `Outside` only if the sphere is entirely beyond one plane. Exact for single planes; near an edge of
    /// the frustum a sphere can be
    ///   reported `Intersects` although it is outside (conservative).
    /// - Fails: never.
    Containment TestSphere(const Sphere& sphere) const;

    /// Tests an axis-aligned box.
    ///
    /// - Param box: the box in the frustum's space.
    /// - Returns: `Outside` only if the box is entirely beyond one plane (conservative near frustum corners).
    /// - Fails: never.
    Containment TestAabb(const Aabb& box) const;

    /// Decides whether an object with these bounds may be visible: the sphere first, and the box only when the sphere
    /// straddles a plane.
    ///
    /// - Param bounds: the object's bounds in the frustum's space.
    /// - Returns: false if the object can be culled.
    /// - Fails: never.
    bool Visible(const Bounds& bounds) const;
};

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
inline Vec3 Center(const Aabb& box)
{
    return box.Center();
}

/// Returns the smallest box containing both boxes (an empty box adds nothing).
Aabb Union(const Aabb& a, const Aabb& b);

/// Returns the smallest box containing the box and the point.
Aabb Union(const Aabb& box, const Vec3& point);

/// Returns the common part of two boxes; it is empty ([[Aabb::IsEmpty]]) when they do not overlap.
Aabb Intersection(const Aabb& a, const Aabb& b);

/// Returns the box grown by `margin` on every side (a negative margin shrinks it, and may leave it empty).
inline Aabb Expand(const Aabb& box, f32 margin)
{
    const Vec3 m(margin, margin, margin);
    return Aabb::FromMinMax(box.min - m, box.max + m);
}

/// Returns whether the point is in the box (the faces included).
inline bool Contains(const Aabb& box, const Vec3& point)
{
    return point.x >= box.min.x && point.x <= box.max.x && point.y >= box.min.y && point.y <= box.max.y &&
           point.z >= box.min.z && point.z <= box.max.z;
}

/// Returns whether `inner` lies entirely inside `outer` (the faces included); an empty `inner` is inside every box.
bool Contains(const Aabb& outer, const Aabb& inner);

/// Returns whether the point is in the sphere (its surface included).
inline bool Contains(const Sphere& sphere, const Vec3& point)
{
    return (point - sphere.center).LengthSquared() <= sphere.radius * sphere.radius;
}

/// Returns whether two boxes share a point (touching counts); an empty box overlaps nothing.
bool Overlaps(const Aabb& a, const Aabb& b);

/// Returns whether the box and the sphere share a point (touching counts).
bool Overlaps(const Aabb& box, const Sphere& sphere);

/// Returns whether two spheres share a point (touching counts).
bool Overlaps(const Sphere& a, const Sphere& b);

/// Returns whether the box may be inside the frustum: false only when it is entirely beyond one plane (conservative
/// near the frustum's corners and edges, as [[Frustum::TestAabb]]), and false for an empty box.
inline bool Overlaps(const Frustum& frustum, const Aabb& box)
{
    return !box.IsEmpty() && frustum.TestAabb(box) != Containment::Outside;
}

/// Returns whether the sphere may be inside the frustum (conservative, as [[Frustum::TestSphere]]).
inline bool Overlaps(const Frustum& frustum, const Sphere& sphere)
{
    return frustum.TestSphere(sphere) != Containment::Outside;
}

/// Returns the surface area of the box (0 for an empty box): the cost the bounding-volume hierarchies minimize.
f32 SurfaceArea(const Aabb& box);

/// Returns the volume of the box (0 for an empty box).
f32 Volume(const Aabb& box);

/// Returns the squared distance from the point to the nearest point of the box (0 when the point is inside).
f32 Distance2(const Vec3& point, const Aabb& box);

/// Finds where a ray enters a box (the slab test).
///
/// - Param ray: the ray, made with [[Ray::Make]].
/// - Param box: the box.
/// - Param max_t: the farthest parameter that counts (use a large value for an unbounded ray).
/// - Param t: receives the parameter at which the ray enters the box, 0 when it starts inside; may be null.
/// - Returns: whether the ray meets the box between parameter 0 and `max_t` (a graze of a face or an edge counts).
/// - Fails: returns false for an empty box, a box entirely behind the origin, or one beyond `max_t`.
bool Intersect(const Ray& ray, const Aabb& box, f32 max_t, f32* t);

/// Finds where a ray first meets a sphere.
///
/// - Param ray: the ray.
/// - Param sphere: the sphere.
/// - Param max_t: the farthest parameter that counts.
/// - Param t: receives the parameter of the first point of the sphere on the ray, 0 when the ray starts inside; may be
/// null.
/// - Returns: whether the ray meets the sphere between 0 and `max_t` (a tangent counts).
/// - Fails: returns false when it does not.
bool Intersect(const Ray& ray, const Sphere& sphere, f32 max_t, f32* t);

/// Finds where a ray crosses a plane.
///
/// - Param ray: the ray.
/// - Param plane: the plane.
/// - Param max_t: the farthest parameter that counts.
/// - Param t: receives the parameter of the crossing; may be null.
/// - Returns: whether the ray crosses the plane between 0 and `max_t`.
/// - Fails: returns false for a ray parallel to the plane (even one lying in it) or one that crosses it behind the
/// origin or beyond `max_t`.
bool Intersect(const Ray& ray, const Plane& plane, f32 max_t, f32* t);

}  // namespace engine_placeholder
