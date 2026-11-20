#pragma once

#include "types.h"

//! Creating a Win32 presentation surface (Phase 13 Day 160), kept in its own translation unit under a Windows
//! directory.
//!
//! `vulkan_win32.h` needs `<windows.h>`'s types, which only the Windows directories may include
//! (`platform_neutral_check` rule 4), so only `vk_win32_surface.cpp` includes it. This header uses plain types, as
//! `vk_xlib_surface.h` does for X11. Whether a queue family can present to the surface is asked through the surface
//! (`vkGetPhysicalDeviceSurfaceSupportKHR`), as on Linux, so the Win32-specific presentation query is not used.

namespace vse::rhi::vulkan
{

/// Creates a `VkSurfaceKHR` for a Win32 window with `vkCreateWin32SurfaceKHR`.
///
/// - Param get_instance_proc_addr: the loader's `vkGetInstanceProcAddr` (a `PFN_vkGetInstanceProcAddr`).
/// - Param instance: the `VkInstance`, created with `VK_KHR_win32_surface` enabled.
/// - Param hinstance: the `HINSTANCE` of the module that registered the window's class.
/// - Param hwnd: the window's `HWND`; it must outlive the surface.
/// - Param out_surface: receives the `VkSurfaceKHR` handle value; must not be null.
/// - Returns: the `VkResult` as an integer; 0 is `VK_SUCCESS`.
/// - Fails: returns `VK_ERROR_EXTENSION_NOT_PRESENT` (-7) if the entry point cannot be resolved, or the driver's error.
i32 CreateWin32Surface(void* get_instance_proc_addr, void* instance, void* hinstance, void* hwnd, u64* out_surface);

}  // namespace vse::rhi::vulkan
