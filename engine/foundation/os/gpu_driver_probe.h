#pragma once

#include <vulkan/vulkan_core.h>

//! Loader smoke test that locates the Vulkan driver and resolves `vkCreateInstance`; not the RHI.

namespace engine_placeholder::os {

/// Loads `libvulkan.so.1` and resolves `vkCreateInstance` through `vkGetInstanceProcAddr`.
///
/// Each step is logged on the platform log channel, including which step failed.
/// Returns: the `vkCreateInstance` function pointer, or `nullptr` on failure.
/// Fails: returns `nullptr` if the driver library or either symbol cannot be found.
/// Ownership: the driver library is deliberately left loaded, so the returned pointer stays valid.
PFN_vkVoidFunction ProbeVulkanCreateInstance();

} // namespace engine_placeholder::os
