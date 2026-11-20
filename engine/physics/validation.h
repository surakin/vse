// What a physics value can be refused for (Phase 25 Day 387): host data (a shape, a body, a collider) is checked and
// refused with a status, never aborted on.
#pragma once

#include "types.h"

namespace vse::physics
{

/// Why a value was refused, or `Ok`.
///
/// - Thread-safety: thread-compatible plain data.
enum class Validation : u8
{
    Ok = 0,         ///< The value is usable.
    NotFinite,      ///< A number is NaN or infinite.
    NotPositive,    ///< A size, mass or density that must be above zero is zero or negative.
    OutOfRange,     ///< A number is outside its range (a fraction above 1, a negative damping, a layer of 32 or more).
    UnknownKind,    ///< A shape or body kind this build does not know.
    NotNormalized,  ///< A quaternion that must be a unit rotation is not.
};

/// Returns the name of a validation result (`ok`, `not_finite`, ...), static, never empty.
///
/// - Param value: the result.
/// - Returns: its name; `unknown` for a number that is not a result.
/// - Thread-safety: thread-safe.
/// - Ownership: the string is static; the caller does not free it.
/// - Fails: never.
const char* ValidationName(Validation value);

}  // namespace vse::physics
