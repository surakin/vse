#pragma once

#include <vulkan/vulkan_core.h>

// Phase 1 Day 17: proves the OS dynamic-loader primitives (os::LoadLibrary
// / os::GetSymbol, os.h) can locate the Vulkan driver and walk the
// vkGetInstanceProcAddr -> vkCreateInstance resolution chain that Phase
// 3's RHI builds real instance/device creation on top of. This is the
// loader smoke test only — not the RHI itself, which owns actual
// instance/device creation and the full Vulkan API surface.
namespace engine_placeholder::os {

// Returns the resolved vkCreateInstance function pointer, or nullptr if
// any step fails (driver not found, symbol not found) — never crashes.
// Logs which step failed via LogPlatform (log/log.h).
PFN_vkVoidFunction ProbeVulkanCreateInstance();

} // namespace engine_placeholder::os
