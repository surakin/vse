#pragma once

#include <cmath>

#include "types.h"

//! Scalar math helpers: thin wrappers over `<cmath>` plus constants.
namespace engine_placeholder {

/// Returns the square root of `value`; NaN for negative input.
inline f32 Sqrt(f32 value) { return std::sqrt(value); }
/// Returns the sine of an angle given in radians.
inline f32 Sin(f32 radians) { return std::sin(radians); }
/// Returns the cosine of an angle given in radians.
inline f32 Cos(f32 radians) { return std::cos(radians); }
/// Returns the arc cosine of `value` in radians, in [0, pi]; NaN outside [-1, 1].
inline f32 Acos(f32 value) { return std::acos(value); }

/// The constant pi.
constexpr f32 kPi = 3.14159265358979323846f;

} // namespace engine_placeholder
