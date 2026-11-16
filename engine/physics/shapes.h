// The collision shapes of the physics module (Phase 25 Day 375): sphere, box and capsule as plain values with their
// bounds. Convex hulls, triangle meshes and compound shapes join on Days 381-383, each as a new kind at the end of
// `ShapeKind`.
#pragma once

#include "math/geometry.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/validation.h"
#include "types.h"

namespace engine_placeholder::physics
{

/// The kind of a shape. The numbers are stored and sent through the C API: never renumbered, new kinds go at the end.
///
/// - Thread-safety: thread-compatible plain data.
enum class ShapeKind : u8
{
    Sphere = 0,   ///< A ball: `radius`.
    Box = 1,      ///< A box with its centre at the shape's origin: `half_extents`.
    Capsule = 2,  ///< A cylinder with a hemisphere on each end, its axis along the shape's Y axis: `half_height` (of
                  ///< the cylinder part) and `radius`.
};

/// A shape as a tagged value: the kind says which fields count. The shape lives in the space of its collider (its
/// origin is the collider's offset).
///
/// - Thread-safety: thread-compatible plain data.
struct Shape
{
    /// Which fields are used.
    ShapeKind kind = ShapeKind::Sphere;
    /// For a sphere and a capsule, the radius, above zero.
    f32 radius = 0.5f;
    /// For a capsule, half the length of the cylinder part (zero makes it a sphere), at least zero.
    f32 half_height = 0.0f;
    /// For a box, the half extents on x, y and z, each above zero.
    Vec3 half_extents = Vec3(0.5f, 0.5f, 0.5f);

    /// Makes a sphere.
    ///
    /// - Param radius: its radius.
    /// - Returns: the shape (not checked: see `Validate`).
    /// - Fails: never.
    static Shape MakeSphere(f32 radius);

    /// Makes a box.
    ///
    /// - Param half_extents: half its size on each axis.
    /// - Returns: the shape (not checked: see `Validate`).
    /// - Fails: never.
    static Shape MakeBox(const Vec3& half_extents);

    /// Makes a capsule along Y.
    ///
    /// - Param half_height: half the length of the cylinder part.
    /// - Param radius: the radius of the cylinder and of the end caps.
    /// - Returns: the shape (not checked: see `Validate`).
    /// - Fails: never.
    static Shape MakeCapsule(f32 half_height, f32 radius);

    /// Checks the shape: finite numbers, positive sizes, a known kind. Host data is refused with a result, never
    /// aborted on.
    ///
    /// - Returns: `Ok`, or why the shape was refused.
    /// - Fails: as the return says.
    Validation Validate() const;

    /// Returns the smallest axis-aligned box around the shape after it is rotated and moved.
    ///
    /// - Param position: where the shape's origin is.
    /// - Param rotation: its orientation (a unit quaternion).
    /// - Returns: the box; for a shape that is not valid, an empty box at `position`.
    /// - Fails: never.
    Aabb Bounds(const Vec3& position, const Quaternion& rotation) const;

    /// Returns the half extents of the box around the shape in its own space (the capsule's are `radius, half_height +
    /// radius, radius`).
    ///
    /// - Returns: the half extents.
    /// - Fails: never.
    Vec3 LocalHalfExtents() const;
};

}  // namespace engine_placeholder::physics
