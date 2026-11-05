#pragma once

//! The view frustum and the tests that decide whether an object can be seen.

#include "math/matrix.h"
#include "math/vector.h"
#include "renderer/bounds.h"
#include "types.h"

namespace engine_placeholder::renderer {

/// A plane `dot(normal, p) + d = 0`; points with a non-negative value are on the inside.
///
/// Thread-safety: thread-compatible plain data.
struct Plane {
    /// The unit normal, pointing into the frustum.
    Vec3 normal = Vec3(0, 1, 0);
    /// The offset: `-dot(normal, point_on_plane)`.
    f32 d = 0.0f;

    /// Returns the signed distance of a point: positive inside, negative outside.
    f32 Distance(const Vec3& p) const { return normal.Dot(p) + d; }
};

/// How a volume relates to the frustum.
enum class Containment : u8 {
    Outside = 0, ///< Entirely outside at least one plane: cull it.
    Intersects,  ///< Straddles a plane (or the test could not rule it out): draw it.
    Inside,      ///< Entirely inside every plane.
};

/// Six planes bounding the visible volume, built from a view-projection matrix.
///
/// Thread-safety: thread-compatible plain data; the tests are thread-safe.
struct Frustum {
    /// Left, right, bottom, top, near, far. The vertical flip of the projection only swaps which of bottom and top is on
    /// screen, so the names are a convention.
    Plane planes[6];

    /// Extracts the planes of the volume that `view_projection` maps to clip space (x and y in -w..w, z in 0..w).
    ///
    /// Param view_projection: a matrix built with [[renderer::PerspectiveVulkan]] (depth 0..1).
    /// Returns: the frustum, planes normalized.
    /// Aborts: a plane is degenerate, as with a zero or non-finite matrix.
    static Frustum FromViewProjection(const Mat4& view_projection);

    /// Tests a sphere.
    ///
    /// Param sphere: the sphere in the frustum's space.
    /// Returns: `Outside` only if the sphere is entirely beyond one plane. Exact for single planes; near an edge of the
    ///   frustum a sphere can be reported `Intersects` although it is outside (conservative).
    /// Fails: never.
    Containment TestSphere(const BoundingSphere& sphere) const;

    /// Tests an axis-aligned box.
    ///
    /// Param box: the box in the frustum's space.
    /// Returns: `Outside` only if the box is entirely beyond one plane (conservative near frustum corners).
    /// Fails: never.
    Containment TestAabb(const Aabb& box) const;

    /// Decides whether an object with these bounds may be visible: the sphere first, and the box only when the sphere
    /// straddles a plane.
    ///
    /// Param bounds: the object's bounds in the frustum's space.
    /// Returns: false if the object can be culled.
    /// Fails: never.
    bool Visible(const Bounds& bounds) const;
};

} // namespace engine_placeholder::renderer
