#pragma once

#include "os/os.h"
#include "types.h"
#include "build_defs.h"

//! Vulkan loader access: the platform's loader (`libvulkan.so.1`, `vulkan-1.dll`) opened at run time, so nothing links against Vulkan.

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
// Platform surface types come from the system headers per platform. Linux enables Wayland (the header only
// forward-declares wl_display/wl_surface); the per-platform entries in vk_functions.inc are guarded on the
// extension macro the platform header defines, so another platform adds its define here and its line there.
// The Xlib surface header is deliberately NOT enabled: it includes <X11/Xlib.h>, whose `Success`/`None`/
// `Bool` macros break ordinary C++ enums. Day 56 adds vkCreateXlibSurfaceKHR from an isolated translation unit. Windows enables
// nothing here either: `vulkan_win32.h` needs <windows.h>, so vkCreateWin32SurfaceKHR comes from windows/vk_win32_surface.cpp.
#if ENGINE_PLATFORM_LINUX && !defined(VK_USE_PLATFORM_WAYLAND_KHR)
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#include <vulkan/vulkan.h>

namespace engine_placeholder::rhi::vulkan {

/// The environment variable that overrides the Vulkan loader library path (development and tests).
inline constexpr const char* kVulkanLibraryEnvVar = "ENGINE_VULKAN_LIBRARY";

/// Outcome of [[rhi::vulkan::LoadVulkanLoader]].
enum class LoaderStatus : u8 {
    Ok = 0,       ///< The loader library was opened and `vkGetInstanceProcAddr` was found.
    NoLoader,     ///< No Vulkan loader library could be opened: Vulkan is not installed.
    BadLoader,    ///< The library opened but does not export `vkGetInstanceProcAddr`.
};

/// Returns a short stable name for a loader status, for logs and tests.
/// - Ownership: the string is static.
const char* LoaderStatusName(LoaderStatus status);

/// A Vulkan loader library that has been opened.
///
/// The library handle is intentionally never unloaded: Vulkan drivers and layers
/// keep threads and global state alive past `vkDestroyInstance`, and unloading
/// their code from under them crashes at process exit. The handle therefore
/// has no owner and no destructor. Opening the same library again returns the
/// same mapping, so repeated loads do not accumulate.
///
/// - Thread-safety: thread-compatible plain data; the function pointer is immutable once set.
struct VulkanLoader {
    /// The `dlopen` handle of the loader library, or null if not loaded.
    os::DynamicLibraryHandle library = nullptr;
    /// The loader's `vkGetInstanceProcAddr`, the root from which every other entry point is resolved.
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
};

/// Opens the Vulkan loader library and resolves `vkGetInstanceProcAddr`.
///
/// The library is [[os::VulkanLoaderName]] (`libvulkan.so.1` on Linux, `vulkan-1.dll` on Windows) unless
/// [[rhi::vulkan::kVulkanLibraryEnvVar]] names another path, which is
/// how tests simulate a machine without Vulkan. The override is a development aid and grants nothing the
/// process environment does not already (`LD_PRELOAD` and `VK_ICD_FILENAMES` redirect the loader the same way). The reason for a failure is logged on `LogRhi`.
///
/// - Param out: receives the loader on success; left untouched otherwise. Must not be null.
/// - Returns: `LoaderStatus::Ok` on success.
/// - Fails: `NoLoader` if the library cannot be opened; `BadLoader` if it lacks `vkGetInstanceProcAddr`.
/// - Thread-safety: thread-safe against other calls; it reads the environment, so do not change the environment
///   concurrently. May be called repeatedly.
LoaderStatus LoadVulkanLoader(VulkanLoader* out);

} // namespace engine_placeholder::rhi::vulkan
