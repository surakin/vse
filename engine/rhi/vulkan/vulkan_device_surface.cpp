// VulkanDevice: the presentation surface and the swapchain entry points.

#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_result.h"
#include "rhi/vulkan/vk_swapchain.h"
#include "rhi/vulkan/vk_xlib_surface.h"
#include "rhi/vulkan/vulkan_device.h"

namespace engine_placeholder::rhi::vulkan {

bool VulkanDevice::CreateSurface(const os::NativeWindowInfo& native) {
    VkResult result = VK_ERROR_EXTENSION_NOT_PRESENT;
    switch (native.kind) {
    case os::NativeWindowKind::Wayland: {
#ifdef VK_KHR_wayland_surface
        if (instance_.Fns().CreateWaylandSurfaceKHR == nullptr) {
            RhiLog(LogVerbosity::Error, "Vulkan: VK_KHR_wayland_surface is not available");
            return false;
        }
        VkWaylandSurfaceCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        info.display = static_cast<wl_display*>(native.display);
        info.surface = static_cast<wl_surface*>(native.surface);
        result = instance_.Fns().CreateWaylandSurfaceKHR(instance_.Handle(), &info, nullptr, &surface_);
#endif
        break;
    }
    case os::NativeWindowKind::Xlib: {
        u64 handle = 0;
        result = static_cast<VkResult>(CreateXlibSurface(reinterpret_cast<void*>(instance_.Loader().get_instance_proc_addr), instance_.Handle(),
                                                         native.display, native.window_id, &handle));
        if (result == VK_SUCCESS) {
            surface_ = HandleFromU64<VkSurfaceKHR>(handle);
        }
        break;
    }
    default:
        RhiLog(LogVerbosity::Error, "Vulkan: the window kind %d cannot be presented to by this build", static_cast<int>(native.kind));
        return false;
    }
    if (result != VK_SUCCESS) {
        ENGINE_VK_CHECK(result);
        RhiLog(LogVerbosity::Error, "Vulkan: cannot create a presentation surface for the window (%s)", VkResultName(result));
        surface_ = nullptr;
        return false;
    }
    return true;
}

Swapchain* VulkanDevice::CreateSwapchain(const SwapchainDesc& desc) {
    if (surface_ == nullptr || window_ == nullptr) {
        RhiLog(LogVerbosity::Error, "CreateSwapchain: the device was created without a window");
        return nullptr;
    }
    if (swapchain_ != nullptr) {
        RhiLog(LogVerbosity::Error, "CreateSwapchain: the device already has a swapchain");
        return nullptr;
    }
    swapchain_ = VulkanSwapchain::Create(this, desc);
    return swapchain_;
}

void VulkanDevice::DestroySwapchain(Swapchain* swapchain) {
    if (swapchain == nullptr) {
        return;
    }
    ENGINE_ASSERT(swapchain == swapchain_, "DestroySwapchain: not this device's swapchain");
    delete swapchain_; // its destructor clears swapchain_
}

} // namespace engine_placeholder::rhi::vulkan
