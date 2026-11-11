#pragma once

#include "types.h"

//! Creating an Xlib presentation surface, kept in its own translation unit.
//!
//! `vulkan_xlib.h` includes `<X11/Xlib.h>`, whose `Success`, `None` and `Bool` macros break ordinary C++ enums, so
//! only `vk_xlib_surface.cpp` may include it. This header uses plain types so nothing else sees X11.

namespace engine_placeholder::rhi::vulkan {

/// Creates a `VkSurfaceKHR` for an X11 window with `vkCreateXlibSurfaceKHR`.
///
/// - Param get_instance_proc_addr: the loader's `vkGetInstanceProcAddr` (a `PFN_vkGetInstanceProcAddr`).
/// - Param instance: the `VkInstance`, created with `VK_KHR_xlib_surface` enabled.
/// - Param display: the `Display*` the window belongs to.
/// - Param window: the X11 `Window` (XID).
/// - Param out_surface: receives the `VkSurfaceKHR` handle value; must not be null.
/// - Returns: the `VkResult` as an integer; 0 is `VK_SUCCESS`.
/// - Fails: returns `VK_ERROR_EXTENSION_NOT_PRESENT` (-7) if the entry point cannot be resolved, or the driver's error.
i32 CreateXlibSurface(void* get_instance_proc_addr, void* instance, void* display, u64 window, u64* out_surface);

} // namespace engine_placeholder::rhi::vulkan
