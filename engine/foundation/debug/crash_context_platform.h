#pragma once

#include "debug/crash_context.h"

// The per-platform half of the crash context (crash_context.h). Not part of the public API.
namespace vse::debug::detail
{

// Fills the platform fields: build id, executable, operating system, architecture, device, CPU, memory, start time.
void FillPlatformCrashContext(CrashContext& context);

}  // namespace vse::debug::detail
