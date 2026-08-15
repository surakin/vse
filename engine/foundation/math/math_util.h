#pragma once

#include <cmath>

#include "types.h"

// Scalar math helpers. These wrap <cmath> rather than hand-rolling —
// PLAN.md assumption #2: sqrt/trig are solved problems with no
// control/performance/learning upside to reimplementing; the foundation
// layer hand-rolls its allocators, containers, string handling, and the
// vector/matrix/quaternion types built on top of these scalars instead.
namespace engine_placeholder {

inline f32 Sqrt(f32 value) { return std::sqrt(value); }
inline f32 Sin(f32 radians) { return std::sin(radians); }
inline f32 Cos(f32 radians) { return std::cos(radians); }
inline f32 Acos(f32 value) { return std::acos(value); }

constexpr f32 kPi = 3.14159265358979323846f;

} // namespace engine_placeholder
