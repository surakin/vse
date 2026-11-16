#include "physics/joint.h"

#include "lang/scalar.h"
#include "physics/check.h"

namespace engine_placeholder::physics
{

Validation JointDesc::Validate() const
{
    if (!FiniteVec3(anchor_a) || !FiniteVec3(anchor_b) || !IsUnitQuaternion(frame_a) || !IsUnitQuaternion(frame_b) ||
        !IsFinite(length) || !IsFinite(min_length) || !IsFinite(max_length) || !IsFinite(hertz) ||
        !IsFinite(damping_ratio) || !IsFinite(break_force) || !IsFinite(break_torque) || !IsFinite(lower_angle) ||
        !IsFinite(upper_angle) || !IsFinite(motor_speed) || !IsFinite(max_motor_torque))
    {
        return Validation::NotFinite;
    }
    if (type != JointType::Ball && type != JointType::Distance && type != JointType::Fixed && type != JointType::Hinge)
    {
        return Validation::UnknownKind;
    }
    if ((!body_a.IsValid() && !body_b.IsValid()) || (body_a.IsValid() && body_a == body_b))
    {
        return Validation::OutOfRange;
    }
    if (type == JointType::Distance)
    {
        if (length <= 0.0f)
        {
            return Validation::NotPositive;
        }
        if (min_length < 0.0f || min_length > length || max_length < length)
        {
            return Validation::OutOfRange;
        }
    }
    if (type == JointType::Hinge && enable_limit)
    {
        if (lower_angle > upper_angle || Abs(lower_angle) > 1000.0f || Abs(upper_angle) > 1000.0f)
        {
            return Validation::OutOfRange;
        }
    }
    if (max_motor_torque < 0.0f)
    {
        return Validation::OutOfRange;
    }
    if (hertz < 0.0f || damping_ratio < 0.0f || break_force < 0.0f || break_torque < 0.0f)
    {
        return Validation::OutOfRange;
    }
    return Validation::Ok;
}

f32 HingeRawAngle(const JointDesc& desc, const Quaternion& orientation_a, const Quaternion& orientation_b)
{
    const Quaternion fa = orientation_a * desc.frame_a;
    const Quaternion fb = orientation_b * desc.frame_b;
    const Quaternion q = fb * fa.Conjugate();
    const Vec3 axis = fa.RotateVector(Vec3(0.0f, 0.0f, 1.0f));
    return 2.0f * Atan2(q.x * axis.x + q.y * axis.y + q.z * axis.z, q.w);
}

f32 WrapAngle(f32 angle)
{
    constexpr f32 kTwoPi = 6.2831853f;
    constexpr f32 kPi = 3.14159265f;
    return angle - kTwoPi * Floor((angle + kPi) / kTwoPi);
}

}  // namespace engine_placeholder::physics
