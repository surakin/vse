#include "animation/pose.h"

#include "lang/scalar.h"
#include "log/assert.h"

namespace vse::animation
{
namespace
{

void PoseNlerp(const Array<f32, 4>& a, const Array<f32, 4>& b, f32 w, Array<f32, 4>* out)
{
    const f32 dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    const f32 sign = dot < 0.0f ? -1.0f : 1.0f;
    f32 q[4];
    f32 length_squared = 0;
    for (u32 c = 0; c < 4; ++c)
    {
        q[c] = a[c] * (1.0f - w) + sign * b[c] * w;
        length_squared += q[c] * q[c];
    }
    const f32 inv = length_squared > 0.0f ? 1.0f / Sqrt(length_squared) : 1.0f;
    for (u32 c = 0; c < 4; ++c)
    {
        (*out)[c] = q[c] * inv;
    }
}

Array<f32, 4> PoseMultiply(const Array<f32, 4>& a, const Array<f32, 4>& b)
{
    // Hamilton product a * b, stored (x, y, z, w).
    Array<f32, 4> r;
    r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    return r;
}

void BlendJoint(LocalPose* a, const LocalPose& b, u32 j, f32 w)
{
    if (w <= 0.0f)
    {
        return;
    }
    if (w >= 1.0f)
    {
        a->translation[j] = b.translation[j];
        a->rotation[j] = b.rotation[j];
        a->scale[j] = b.scale[j];
        return;
    }
    for (u32 c = 0; c < 3; ++c)
    {
        a->translation[j][c] = a->translation[j][c] * (1.0f - w) + b.translation[j][c] * w;
        a->scale[j][c] = a->scale[j][c] * (1.0f - w) + b.scale[j][c] * w;
    }
    PoseNlerp(a->rotation[j], b.rotation[j], w, &a->rotation[j]);
}

// The 3x4 matrix of a translation, rotation and scale: the rotation's columns scaled, then the translation.
Mat3x4 PoseMatrix(const Array<f32, 3>& t, const Array<f32, 4>& q, const Array<f32, 3>& s)
{
    const f32 x = q[0], y = q[1], z = q[2], w = q[3];
    Mat3x4 m;
    m[0] = (1.0f - 2.0f * (y * y + z * z)) * s[0];
    m[1] = (2.0f * (x * y - z * w)) * s[1];
    m[2] = (2.0f * (x * z + y * w)) * s[2];
    m[3] = t[0];
    m[4] = (2.0f * (x * y + z * w)) * s[0];
    m[5] = (1.0f - 2.0f * (x * x + z * z)) * s[1];
    m[6] = (2.0f * (y * z - x * w)) * s[2];
    m[7] = t[1];
    m[8] = (2.0f * (x * z - y * w)) * s[0];
    m[9] = (2.0f * (y * z + x * w)) * s[1];
    m[10] = (1.0f - 2.0f * (x * x + y * y)) * s[2];
    m[11] = t[2];
    return m;
}

// a * b of two affine 3x4 matrices (the bottom row (0 0 0 1) is implied).
Mat3x4 PoseCompose(const Mat3x4& a, const Mat3x4& b)
{
    Mat3x4 r;
    for (u32 row = 0; row < 3; ++row)
    {
        for (u32 col = 0; col < 3; ++col)
        {
            r[row * 4 + col] = a[row * 4 + 0] * b[0 * 4 + col] + a[row * 4 + 1] * b[1 * 4 + col] +
                               a[row * 4 + 2] * b[2 * 4 + col];
        }
        r[row * 4 + 3] = a[row * 4 + 0] * b[3] + a[row * 4 + 1] * b[7] + a[row * 4 + 2] * b[11] + a[row * 4 + 3];
    }
    return r;
}

}  // namespace

void SetBindPose(const asset::SkeletonView& skeleton, LocalPose* out)
{
    const u32 n = skeleton.JointCount();
    out->joint_count = n;
    for (u32 j = 0; j < n; ++j)
    {
        out->translation[j] = skeleton.Translation(j);
        out->rotation[j] = skeleton.Rotation(j);
        out->scale[j] = skeleton.Scale(j);
    }
}

void SetIdentityPose(u32 joint_count, LocalPose* out)
{
    ENGINE_ASSERT(joint_count <= kMaxPoseJoints, "SetIdentityPose: too many joints");
    out->joint_count = joint_count;
    for (u32 j = 0; j < joint_count; ++j)
    {
        out->translation[j] = {{0.0f, 0.0f, 0.0f}};
        out->rotation[j] = {{0.0f, 0.0f, 0.0f, 1.0f}};
        out->scale[j] = {{1.0f, 1.0f, 1.0f}};
    }
}

void Blend(LocalPose* a, const LocalPose& b, f32 weight)
{
    ENGINE_ASSERT(a->joint_count == b.joint_count, "Blend: the poses have different joint counts");
    ENGINE_ASSERT(weight >= 0.0f && weight <= 1.0f, "Blend: the weight is not in [0, 1]");
    for (u32 j = 0; j < a->joint_count; ++j)
    {
        BlendJoint(a, b, j, weight);
    }
}

void BlendMasked(LocalPose* a, const LocalPose& b, f32 weight, const JointMask& mask)
{
    ENGINE_ASSERT(a->joint_count == b.joint_count, "BlendMasked: the poses have different joint counts");
    ENGINE_ASSERT(weight >= 0.0f && weight <= 1.0f, "BlendMasked: the weight is not in [0, 1]");
    for (u32 j = 0; j < a->joint_count; ++j)
    {
        BlendJoint(a, b, j, weight * (static_cast<f32>(mask.weight[j]) / 255.0f));
    }
}

void ComposeJointRotation(LocalPose* pose, u32 joint, const Array<f32, 4>& rotation)
{
    ENGINE_ASSERT(joint < pose->joint_count, "ComposeJointRotation: the joint is not in the pose");
    Array<f32, 4> q = PoseMultiply(pose->rotation[joint], rotation);
    f32 length_squared = 0.0f;
    for (u32 c = 0; c < 4; ++c)
    {
        length_squared += q[c] * q[c];
    }
    const f32 inv = length_squared > 0.0f ? 1.0f / Sqrt(length_squared) : 1.0f;
    for (u32 c = 0; c < 4; ++c)
    {
        q[c] *= inv;
    }
    pose->rotation[joint] = q;
}

void AddAdditive(LocalPose* base, const LocalPose& additive, f32 weight)
{
    ENGINE_ASSERT(base->joint_count == additive.joint_count, "AddAdditive: the poses have different joint counts");
    ENGINE_ASSERT(weight >= 0.0f && weight <= 1.0f, "AddAdditive: the weight is not in [0, 1]");
    if (weight <= 0.0f)
    {
        return;
    }
    const Array<f32, 4> identity = {{0.0f, 0.0f, 0.0f, 1.0f}};
    for (u32 j = 0; j < base->joint_count; ++j)
    {
        Array<f32, 4> d;
        PoseNlerp(identity, additive.rotation[j], weight, &d);
        base->rotation[j] = PoseMultiply(d, base->rotation[j]);
        for (u32 c = 0; c < 3; ++c)
        {
            base->translation[j][c] += weight * additive.translation[j][c];
            base->scale[j][c] *= 1.0f + weight * (additive.scale[j][c] - 1.0f);
        }
    }
}

void ModelFromLocal(const asset::SkeletonView& skeleton, const LocalPose& pose, ModelPose* out)
{
    ENGINE_ASSERT(pose.joint_count == skeleton.JointCount(), "ModelFromLocal: the pose is for another skeleton");
    out->joint_count = pose.joint_count;
    for (u32 j = 0; j < pose.joint_count; ++j)
    {
        const Mat3x4 local = PoseMatrix(pose.translation[j], pose.rotation[j], pose.scale[j]);
        const u16 parent = skeleton.Parent(j);
        out->matrix[j] = parent == asset::kNoParent ? local : PoseCompose(out->matrix[parent], local);
    }
}

void SkinningMatrices(const asset::SkeletonView& skeleton, const ModelPose& pose, Span<Mat3x4> out)
{
    ENGINE_ASSERT(pose.joint_count == skeleton.JointCount(), "SkinningMatrices: the pose is for another skeleton");
    ENGINE_ASSERT(out.Size() >= pose.joint_count, "SkinningMatrices: the output is too small");
    for (u32 j = 0; j < pose.joint_count; ++j)
    {
        out[j] = PoseCompose(pose.matrix[j], skeleton.InverseBind(j));
    }
}

}  // namespace vse::animation
