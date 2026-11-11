// The only translation unit that sees X11 and the Xlib surface header.

#include "rhi/vulkan/vk_xlib_surface.h"

#define VK_USE_PLATFORM_XLIB_KHR
#include "unity/x11_enable.h"
#include <X11/Xlib.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>

namespace engine_placeholder::rhi::vulkan
{

i32 CreateXlibSurface(void* get_instance_proc_addr, void* instance, void* display, u64 window, u64* out_surface)
{
    PFN_vkGetInstanceProcAddr gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(get_instance_proc_addr);
    VkInstance vk_instance = static_cast<VkInstance>(instance);
    PFN_vkCreateXlibSurfaceKHR create = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
        gipa(vk_instance, "vkCreateXlibSurfaceKHR"));
    if (create == nullptr)
    {
        return static_cast<i32>(VK_ERROR_EXTENSION_NOT_PRESENT);
    }
    VkXlibSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    info.dpy = static_cast<Display*>(display);
    info.window = static_cast<Window>(window);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result = create(vk_instance, &info, nullptr, &surface);
    if (result == VK_SUCCESS)
    {
        *out_surface = reinterpret_cast<u64>(surface);
    }
    return static_cast<i32>(result);
}

}  // namespace engine_placeholder::rhi::vulkan

#include "unity/x11_disable.h"
