#pragma once

#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"

//! Position, rotation and scale (TRS) and composition of two of them.
//!
//! Uses the same column-vector convention as matrix.h and quaternion.h.
namespace vse
{

/// Position, rotation and scale of an object relative to its parent space.
///
/// - Thread-safety: thread-compatible plain data.
struct Transform
{
    /// Translation, applied last.
    Vec3 position;
    /// Orientation, applied after scale.
    Quaternion rotation = Quaternion::Identity();
    /// Per-axis scale, applied first; defaults to (1, 1, 1).
    Vec3 scale = Vec3(1.0f, 1.0f, 1.0f);

    /// Returns the transform that changes nothing: zero position, identity rotation, unit scale.
    static Transform Identity() { return Transform{}; }

    /// Builds the 4x4 matrix that scales, then rotates, then translates (column-vector convention).
    Mat4 ToMatrix() const
    {
        Mat3 rot = rotation.ToMat3();
        return Mat4::FromBasis(rot.col[0] * scale.x, rot.col[1] * scale.y, rot.col[2] * scale.z, position);
    }
};

/// Composes `child` (expressed in `parent`'s local space) into the parent's
/// space.
///
/// The child's position is scaled and rotated by the parent before being offset
/// by the parent's position; rotations multiply and scales multiply
/// component-wise. This is the usual simplified TRS composition and is not
/// exact for a non-uniform parent scale combined with a rotated child.
///
/// - Param parent: outer transform.
/// - Param child: transform relative to `parent`.
/// - Returns: the combined transform.
inline Transform Compose(const Transform& parent, const Transform& child)
{
    Transform result;
    result.scale = Vec3(parent.scale.x * child.scale.x, parent.scale.y * child.scale.y, parent.scale.z * child.scale.z);
    result.rotation = parent.rotation * child.rotation;
    Vec3 scaled_child_position(child.position.x * parent.scale.x, child.position.y * parent.scale.y,
                               child.position.z * parent.scale.z);
    result.position = parent.position + parent.rotation.RotateVector(scaled_child_position);
    return result;
}

}  // namespace vse
