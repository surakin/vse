#pragma once

//! Bounding volumes used for culling: a sphere and an axis-aligned box, and how they follow a transform.

#include "math/matrix.h"
#include "math/vector.h"
#include "types.h"

namespace engine_placeholder::renderer {

/// A sphere that encloses an object; cheap to test, loose for long thin shapes.
///
/// Thread-safety: thread-compatible plain data.
struct BoundingSphere {
    /// The center.
    Vec3 center = Vec3(0, 0, 0);
    /// The radius; never negative.
    f32 radius = 0.0f;
};

/// An axis-aligned box.
///
/// Thread-safety: thread-compatible plain data.
struct Aabb {
    /// The corner with the smallest coordinates.
    Vec3 min = Vec3(0, 0, 0);
    /// The corner with the largest coordinates; at least `min` in every component.
    Vec3 max = Vec3(0, 0, 0);

    /// Returns the center of the box.
    Vec3 Center() const { return (min + max) * 0.5f; }
    /// Returns half the size of the box along each axis.
    Vec3 HalfExtents() const { return (max - min) * 0.5f; }
};

/// A sphere and a box for the same object: the sphere is tested first and the box refines the result.
///
/// Thread-safety: thread-compatible plain data.
struct Bounds {
    /// The enclosing sphere.
    BoundingSphere sphere;
    /// The enclosing box.
    Aabb box;
};

/// Returns the smallest box around a set of points.
///
/// Param points: `count` points of `stride_bytes` bytes apart, each starting with three `f32`.
/// Param count: number of points; at least 1.
/// Param stride_bytes: distance between points in bytes, at least 12.
/// Returns: the box.
/// Aborts: `points` is null, `count` is 0 or `stride_bytes` is below 12.
/// Ownership: `points` is borrowed for the call.
/// Thread-safety: thread-safe. A pure function of its arguments.
Aabb ComputeAabb(const void* points, u32 count, u32 stride_bytes);

/// Returns bounds for a set of points: their box, and the sphere around the box center that reaches the farthest point.
///
/// The sphere encloses every point but is not the smallest possible one.
///
/// Param points: as for [[renderer::ComputeAabb]].
/// Param count: number of points; at least 1.
/// Param stride_bytes: distance between points in bytes, at least 12.
/// Returns: the bounds.
/// Aborts: as for [[renderer::ComputeAabb]].
/// Ownership: `points` is borrowed for the call.
/// Thread-safety: thread-safe. A pure function of its arguments.
Bounds ComputeBounds(const void* points, u32 count, u32 stride_bytes);

/// Returns the bounds of an object after a transform.
///
/// The box is the box around the transformed box (exact for rotations by multiples of 90 degrees, otherwise larger);
/// the sphere keeps its center transformed and its radius scaled by the largest axis scale.
///
/// Param local: bounds in the object's own space.
/// Param transform: an affine transform (the last row is 0 0 0 1).
/// Returns: bounds in the transformed space.
/// Thread-safety: thread-safe. A pure function of its arguments.
Bounds TransformBounds(const Bounds& local, const Mat4& transform);

} // namespace engine_placeholder::renderer
