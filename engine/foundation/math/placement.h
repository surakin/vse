#pragma once

//! A placement in a world that is larger than single precision can hold: a rotation-and-scale part in `f32` and a
//! translation in `f64`.

#include "math/geometry.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "types.h"

namespace vse
{

/// Where something is in the world: an `f32` linear part (rotation and scale, the matrix's upper 3x3; its translation
/// column is unused and zero) and an `f64` position. The position is what grows large, so only it is double precision.
///
/// - Thread-safety: thread-compatible plain data.
struct Placement
{
    /// The rotation and scale, as a matrix whose translation column is zero.
    Mat4 linear = Mat4::Identity();
    /// The world position of the origin of the placed object's space.
    Vec3d position = Vec3d(0, 0, 0);

    /// Makes a placement from a position and an affine matrix (its translation column is dropped).
    ///
    /// - Param position: the world position.
    /// - Param linear: the rotation and scale; the upper 3x3 is used.
    /// - Fails: never.
    static Placement From(const Vec3d& position, const Mat4& linear);

    /// Returns the world position of a point given in the placed object's space, computed in double precision.
    ///
    /// - Param local: the point in the object's own space.
    /// - Fails: never.
    Vec3d TransformPoint(const Vec3d& local) const;

    /// Returns this placement followed by `child`: the placement of an object whose own placement relative to this
    /// one is `child`. The linear parts multiply in `f32`; the position is `position + linear * child.position`
    /// in `f64`.
    ///
    /// - Param child: the placement in this one's space.
    /// - Fails: never.
    Placement Compose(const Placement& child) const;

    /// Returns the matrix of this placement relative to `origin`: the `f32` linear part with the translation
    /// `(position - origin)` computed in `f64` and then narrowed (the subtraction is where the precision is kept).
    ///
    /// - Param origin: the world position that becomes the origin of the result (the camera, for rendering).
    /// - Fails: never.
    Mat4 RelativeTo(const Vec3d& origin) const;
};

/// The world-space box of a box in a placed object's own space, in double precision: the center goes through
/// [[Placement::TransformPoint]], the half extents grow by the absolute values of the linear part (the tight box of the
/// transformed box, as `TransformAabb` does in single precision).
///
/// - Param local: the box in the object's space; not empty.
/// - Param placement: where the object is.
/// - Returns: the world box; exact to `f64` rounding however far from the origin the object is.
/// - Fails: never.
Aabbd PlaceAabb(const Aabb& local, const Placement& placement);

}  // namespace vse
