#pragma once

#include "types.h"

namespace engine_placeholder::physics_test
{

/// The probe compiled with the project's flags.
bool ProbeProject(const volatile f32* inputs);
/// The probe compiled with contraction allowed (and FMA instructions on x86-64): fused where the machine has the
/// instruction.
bool ProbeControl(const volatile f32* inputs);

}  // namespace engine_placeholder::physics_test
