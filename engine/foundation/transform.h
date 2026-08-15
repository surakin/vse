#pragma once

#include "matrix.h"
#include "quaternion.h"
#include "vector.h"

// Transform: position/rotation/scale, the standard TRS representation.
// Compose(parent, child) follows the same column-vector convention as
// matrix.h/quaternion.h — child is expressed in parent's local space.
namespace engine_placeholder {

struct Transform {
    Vec3 position;
    Quaternion rotation = Quaternion::Identity();
    Vec3 scale = Vec3(1.0f, 1.0f, 1.0f);

    static Transform Identity() { return Transform{}; }

    Mat4 ToMatrix() const {
        Mat3 rot = rotation.ToMat3();
        return Mat4::FromBasis(rot.col[0] * scale.x, rot.col[1] * scale.y, rot.col[2] * scale.z, position);
    }
};

// child is expressed in parent's local space: its position is scaled and
// rotated into parent space before being offset by the parent's own
// position; rotations and scales combine independently. The standard
// simplified TRS composition (matches Unity/Unreal-style engines) — not
// exact for combined non-uniform-scale-plus-rotation hierarchies, a
// known limitation of this representation, not a bug.
inline Transform Compose(const Transform& parent, const Transform& child) {
    Transform result;
    result.scale = Vec3(parent.scale.x * child.scale.x, parent.scale.y * child.scale.y, parent.scale.z * child.scale.z);
    result.rotation = parent.rotation * child.rotation;
    Vec3 scaled_child_position(child.position.x * parent.scale.x, child.position.y * parent.scale.y,
                                child.position.z * parent.scale.z);
    result.position = parent.position + parent.rotation.RotateVector(scaled_child_position);
    return result;
}

} // namespace engine_placeholder
