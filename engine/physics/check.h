// Small checks of host numbers shared by the physics module's files (Phase 25 Day 388). Internal: not part of the
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

/// The largest coordinate a body may be put at, in metres (10,000 km): beyond it the boxes of the broad phase and the
/// arithmetic of the step lose all meaning, so a position past it is refused like a position that is not finite.
inline constexpr f32 kMaxCoordinate = 1.0e7f;

/// The largest size a shape, a collider's offset or a joint's anchor may have, in metres (100 km): the arithmetic of
/// the step multiplies such a number by masses and velocities, so one beyond it leaves the world in a single step.
inline constexpr f32 kMaxExtent = 1.0e5f;

/// Returns whether all three numbers are within `kMaxExtent` of zero (and so finite).
///
/// - Fails: never.
inline bool InsideExtent(const Vec3& v)
{
    return Abs(v.x) <= kMaxExtent && Abs(v.y) <= kMaxExtent && Abs(v.z) <= kMaxExtent;
}

/// The lightest dynamic body, in kilograms: below it the inverse mass overflows in the step's arithmetic.
inline constexpr f32 kMinMass = 1.0e-9f;

/// The heaviest dynamic body, in kilograms: above it the inertia overflows.
inline constexpr f32 kMaxMass = 1.0e15f;

/// Returns whether all three numbers are finite and within `kMaxCoordinate` of the origin.
///
/// - Fails: never.
inline bool InsideWorld(const Vec3& v)
{
    return Abs(v.x) <= kMaxCoordinate && Abs(v.y) <= kMaxCoordinate && Abs(v.z) <= kMaxCoordinate;
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
