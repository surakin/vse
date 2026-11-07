// The only translation unit of the Vulkan backend that sees <windows.h> and the Win32 surface header (Phase 13 Day 160).

#include "rhi/vulkan/windows/vk_win32_surface.h"

#include "os/windows/win32.h"

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

namespace engine_placeholder::rhi::vulkan {

i32 CreateWin32Surface(void* get_instance_proc_addr, void* instance, void* hinstance, void* hwnd, u64* out_surface) {
    PFN_vkGetInstanceProcAddr gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(get_instance_proc_addr);
    VkInstance vk_instance = static_cast<VkInstance>(instance);
    PFN_vkCreateWin32SurfaceKHR create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(gipa(vk_instance, "vkCreateWin32SurfaceKHR"));
    if (create == nullptr) {
        return static_cast<i32>(VK_ERROR_EXTENSION_NOT_PRESENT);
    }
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = static_cast<HINSTANCE>(hinstance);
    info.hwnd = static_cast<HWND>(hwnd);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result = create(vk_instance, &info, nullptr, &surface);
    if (result == VK_SUCCESS) {
        *out_surface = reinterpret_cast<u64>(surface);
    }
    return static_cast<i32>(result);
}

} // namespace engine_placeholder::rhi::vulkan
