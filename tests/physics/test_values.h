// Values shared by the physics tests.
#pragma once

#include "types.h"

namespace engine_placeholder::physics::testing
{

/// A quiet NaN, for the tests that feed bad numbers.
inline const f32 kNaN = __builtin_nanf("");

}  // namespace engine_placeholder::physics::testing
