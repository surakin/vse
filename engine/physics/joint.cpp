#include "physics/joint.h"

#include "lang/scalar.h"
#include "physics/check.h"

namespace engine_placeholder::physics
{

Validation JointDesc::Validate() const
{
    if (!FiniteVec3(anchor_a) || !FiniteVec3(anchor_b) || !IsUnitQuaternion(frame_a) || !IsUnitQuaternion(frame_b) ||
        !IsFinite(length) || !IsFinite(min_length) || !IsFinite(max_length) || !IsFinite(hertz) ||
        !IsFinite(damping_ratio) || !IsFinite(break_force) || !IsFinite(break_torque))
    {
        return Validation::NotFinite;
    }
    if (type != JointType::Ball && type != JointType::Distance && type != JointType::Fixed)
    {
        return Validation::UnknownKind;
    }
    if (!body_a.IsValid() || body_a == body_b)
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
    if (hertz < 0.0f || damping_ratio < 0.0f || break_force < 0.0f || break_torque < 0.0f)
    {
        return Validation::OutOfRange;
    }
    return Validation::Ok;
}

}  // namespace engine_placeholder::physics
