#pragma once

#include "rhi/vulkan/vk_loader.h"

//! Plain tables of Vulkan function pointers, resolved once per global/instance/device scope.

namespace vse::rhi::vulkan
{

/// Entry points that exist before an instance: `vkCreateInstance` and the instance enumerations.
///
/// One `PFN_vk*` member per function, named without the `vk` prefix (`CreateInstance`); the list is
/// generated from `vk_functions.inc`. Optional members may be null after loading.
///
/// - Thread-safety: thread-compatible plain data; immutable once loaded.
struct VkGlobalFns
{
#define VK_GLOBAL_FN(name) PFN_vk##name name = nullptr;
#define VK_GLOBAL_FN_OPT(name) PFN_vk##name name = nullptr;
#include "rhi/vulkan/vk_functions.inc"
};

/// Entry points resolved against a `VkInstance`: physical-device queries, surfaces, debug utils, `vkCreateDevice`.
///
/// Same layout rules as [[rhi::vulkan::VkGlobalFns]]. Instance-level functions go through the loader trampoline.
///
/// - Thread-safety: thread-compatible plain data; immutable once loaded.
struct VkInstanceFns
{
#define VK_INSTANCE_FN(name) PFN_vk##name name = nullptr;
#define VK_INSTANCE_FN_OPT(name) PFN_vk##name name = nullptr;
#include "rhi/vulkan/vk_functions.inc"
};

/// Entry points resolved against a `VkDevice` through `vkGetDeviceProcAddr`, skipping the loader trampoline.
///
/// Covers everything the backend uses. Same layout rules as [[rhi::vulkan::VkGlobalFns]].
///
/// - Thread-safety: thread-compatible plain data; immutable once loaded.
struct VkDeviceFns
{
#define VK_DEVICE_FN(name) PFN_vk##name name = nullptr;
#define VK_DEVICE_FN_OPT(name) PFN_vk##name name = nullptr;
#include "rhi/vulkan/vk_functions.inc"
};

/// Resolves every [[rhi::vulkan::VkGlobalFns]] entry through `vkGetInstanceProcAddr(null, ...)`.
///
/// - Param loader: an opened loader.
/// - Param out: receives the table; must not be null. Reset to all-null first.
/// - Returns: true if every mandatory function resolved.
/// - Fails: returns false, logs every missing mandatory function by name on `LogRhi`, and leaves `*out` all-null.
/// - Thread-safety: thread-safe for distinct `out` tables.
bool LoadGlobalFns(const VulkanLoader& loader, VkGlobalFns* out);

/// Resolves every [[rhi::vulkan::VkInstanceFns]] entry against `instance`.
///
/// - Param loader: an opened loader.
/// - Param instance: a live instance created with the extensions the optional entries need.
/// - Param out: receives the table; must not be null. Reset to all-null first.
/// - Returns: true if every mandatory function resolved.
/// - Fails: returns false, logs every missing mandatory function by name on `LogRhi`, and leaves `*out` all-null.
/// - Thread-safety: thread-safe for distinct `out` tables.
bool LoadInstanceFns(const VulkanLoader& loader, VkInstance instance, VkInstanceFns* out);

/// Resolves every [[rhi::vulkan::VkDeviceFns]] entry against `device` using the instance's `vkGetDeviceProcAddr`.
///
/// - Param instance_fns: the instance table the device was created from.
/// - Param device: a live device.
/// - Param out: receives the table; must not be null. Reset to all-null first.
/// - Returns: true if every mandatory function resolved.
/// - Fails: returns false, logs every missing mandatory function by name on `LogRhi`, and leaves `*out` all-null.
/// - Thread-safety: thread-safe for distinct `out` tables.
bool LoadDeviceFns(const VkInstanceFns& instance_fns, VkDevice device, VkDeviceFns* out);

/// The lowest Vulkan version the backend runs on (1.3: dynamic rendering, synchronization2, timeline semaphores).
inline constexpr u32 kRequiredVulkanVersion = VK_API_VERSION_1_3;

/// Reads the instance-level Vulkan version and checks it against [[rhi::vulkan::kRequiredVulkanVersion]].
///
/// A loader without `vkEnumerateInstanceVersion` is Vulkan 1.0 and is rejected. The version is logged. This
/// checks the loader's instance version only; a physical device can still be older, so device selection
/// checks `VkPhysicalDeviceProperties::apiVersion` as well.
///
/// - Param fns: a loaded global table.
/// - Param out_version: receives the version (`VK_MAKE_API_VERSION` encoding) when the query succeeds, 1.0 for a 1.0
/// loader;
///   may be null.
/// - Returns: true if the version is at least 1.3.
/// - Fails: returns false and logs "driver too old" if the version is below 1.3, or logs the failed query
///   and returns false if `vkEnumerateInstanceVersion` itself fails.
bool CheckInstanceVersion(const VkGlobalFns& fns, u32* out_version);

/// Logs the instance extensions and layers the loader reports, one line each, at `Debug` verbosity.
///
/// Extension lists longer than 64 entries and layer lists longer than 32 are truncated in the log. An
/// enumeration failure is logged as a warning.
///
/// - Param fns: a loaded global table.
void LogInstanceCapabilities(const VkGlobalFns& fns);

}  // namespace vse::rhi::vulkan
