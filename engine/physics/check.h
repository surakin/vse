// Small checks of host numbers shared by the physics module's files (Phase 25 Day 376). Internal: not part of the
// documented API.
#pragma once

#include "lang/scalar.h"
#include "math/quaternion.h"
#include "math/vector.h"

namespace engine_placeholder::physics
{

/// Returns whether all three numbers are finite.
///
/// - Fails: never.
inline bool FiniteVec3(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

/// Returns whether the numbers are finite and the length is 1 within 1e-3.
///
/// - Fails: never.
inline bool IsUnitQuaternion(const Quaternion& q)
{
    if (!IsFinite(q.x) || !IsFinite(q.y) || !IsFinite(q.z) || !IsFinite(q.w))
    {
        return false;
    }
    return Abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.0f) <= 2e-3f;
}

}  // namespace engine_placeholder::physics
