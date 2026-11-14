#pragma once

//! Poses and what is done with them: the local pose of every joint, the blends, the additive pose, the model-space walk
//! and the skinning palette. Phase 22 Day 321. A pose is a fixed structure of 256 joints (the palette limit): every
//! operation works in place on storage the caller owns and allocates nothing.

#include "asset/skeleton.h"
#include "containers/array.h"
#include "containers/span.h"
#include "types.h"

namespace engine_placeholder::animation
{

/// The most joints a pose holds (the skeleton format's limit).
inline constexpr u32 kMaxPoseJoints = 256;

/// One 3x4 row-major matrix (the rows of the upper 3x4 part of an affine 4x4): the palette layout, 48 bytes.
using Mat3x4 = Array<f32, 12>;

/// The local pose of a skeleton: per joint a translation, a unit rotation (x, y, z, w) and a scale, relative to the
/// parent. Structure of arrays; only the first `joint_count` entries mean anything.
///
/// - Thread-safety: thread-compatible plain data.
struct LocalPose
{
    u32 joint_count = 0;                                 ///< The number of joints in use.
    Array<Array<f32, 3>, kMaxPoseJoints> translation{};  ///< Local translations.
    Array<Array<f32, 4>, kMaxPoseJoints> rotation{};     ///< Local rotations, unit quaternions.
    Array<Array<f32, 3>, kMaxPoseJoints> scale{};        ///< Local scales.
};

/// The model-space matrices of a pose: joint to model, 3x4 row-major.
///
/// - Thread-safety: thread-compatible plain data.
struct ModelPose
{
    u32 joint_count = 0;                     ///< The number of joints in use.
    Array<Mat3x4, kMaxPoseJoints> matrix{};  ///< Joint to model.
};

/// A set of joints, for a masked blend.
///
/// - Thread-safety: thread-compatible plain data.
struct JointMask
{
    Array<u8, kMaxPoseJoints> weight{};  ///< 0 leaves the joint of the first pose, 255 blends it fully, between scales
                                         ///< the blend weight.
};

/// Fills `out` with the skeleton's bind pose.
///
/// - Aborts: never.
/// - Ownership: borrows its arguments for the call.
void SetBindPose(const asset::SkeletonView& skeleton, LocalPose* out);

/// Fills `out` with the identity pose (zero translation, identity rotation, unit scale): the neutral element of an
/// additive pose.
///
/// - Param joint_count: the number of joints, at most `kMaxPoseJoints`.
/// - Aborts: `joint_count` is above `kMaxPoseJoints`.
/// - Ownership: borrows its arguments for the call.
void SetIdentityPose(u32 joint_count, LocalPose* out);

/// Blends `b` into `a` in place: `a = a * (1 - weight) + b * weight`, translation and scale linearly, rotation by a
/// normalized lerp on the shorter arc. Weight 0 leaves `a` bit for bit, weight 1 makes it equal `b` bit for bit.
///
/// - Param weight: in [0, 1]; the caller normalizes.
/// - Aborts: the joint counts differ or `weight` is not in [0, 1].
/// - Ownership: borrows its arguments for the call.
void Blend(LocalPose* a, const LocalPose& b, f32 weight);

/// As [[animation::Blend]], with the weight of each joint scaled by `mask` (`weight * mask / 255`). A joint whose mask
/// is 0 is untouched.
///
/// - Aborts: as `Blend`.
/// - Ownership: borrows its arguments for the call.
void BlendMasked(LocalPose* a, const LocalPose& b, f32 weight, const JointMask& mask);

/// Applies an additive pose to `base` in place. The additive pose stores differences from the bind pose
/// (see [[asset::kClipAdditive]]): translation is added (`t += w * d`), rotation is composed (`q = nlerp(identity, d,
/// w)
/// * q`) and scale is multiplied (`s *= lerp(1, d, w)`).
///
/// - Param weight: in [0, 1].
/// - Aborts: the joint counts differ or `weight` is not in [0, 1].
/// - Ownership: borrows its arguments for the call.
void AddAdditive(LocalPose* base, const LocalPose& additive, f32 weight);

/// Walks the parent-first joint table once and writes each joint's model matrix.
///
/// - Aborts: `pose.joint_count` differs from the skeleton's.
/// - Ownership: borrows its arguments for the call.
void ModelFromLocal(const asset::SkeletonView& skeleton, const LocalPose& pose, ModelPose* out);

/// Multiplies each model matrix by its inverse bind matrix into the palette layout the GPU takes: 48 bytes per joint,
/// the same layout as the skeleton's inverse bind section.
///
/// - Param out: room for the joint count of `pose` (at most 256).
/// - Aborts: `out` is smaller than the joint count, or the counts differ from the skeleton's.
void SkinningMatrices(const asset::SkeletonView& skeleton, const ModelPose& pose, Span<Mat3x4> out);

}  // namespace engine_placeholder::animation
