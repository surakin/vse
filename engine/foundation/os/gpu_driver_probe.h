#pragma once

#include <vulkan/vulkan_core.h>

//! Loader smoke test that locates the Vulkan driver and resolves `vkCreateInstance`; not the RHI.

namespace engine_placeholder::os {

/// Returns the file name of this platform's Vulkan loader library: `libvulkan.so.1` on Linux, `vulkan-1.dll` on Windows.
///
/// The one place the name is chosen; the GPU driver probe and the Vulkan backend's loader both use it.
/// Ownership: a static string, never freed.
/// Thread-safety: thread-safe.
const char* VulkanLoaderName();

/// Loads the Vulkan loader ([[os::VulkanLoaderName]]) and resolves `vkCreateInstance` through `vkGetInstanceProcAddr`.
///
/// Each step is logged on the platform log channel, including which step failed.
/// Returns: the `vkCreateInstance` function pointer, or `nullptr` on failure.
/// Fails: returns `nullptr` if the driver library or either symbol cannot be found.
/// Ownership: the driver library is deliberately left loaded, so the returned pointer stays valid.
PFN_vkVoidFunction ProbeVulkanCreateInstance();

} // namespace engine_placeholder::os
