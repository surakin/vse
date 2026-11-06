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

bool VulkanDevice::RecreateSurface(const os::Window* window) {
    ENGINE_ASSERT(!swapchain_, "RecreateSurface: destroy the swapchain first");
    ENGINE_ASSERT(!frame_open_.load(std::memory_order_acquire), "RecreateSurface: called between BeginFrame and EndFrame");
    if (window == nullptr) {
        RhiLog(LogVerbosity::Error, "RecreateSurface: no window");
        return false;
    }
    NoteDeviceLost(fns_.DeviceWaitIdle(device_));
    if (surface_ != nullptr) {
        instance_.Fns().DestroySurfaceKHR(instance_.Handle(), surface_, nullptr);
        surface_ = nullptr;
    }
    window_ = window;
    if (!CreateSurface(window->GetNativeInfo())) {
        return false;
    }
    VkBool32 supported = VK_FALSE;
    const VkResult r = instance_.Fns().GetPhysicalDeviceSurfaceSupportKHR == nullptr ? VK_ERROR_EXTENSION_NOT_PRESENT : instance_.Fns().GetPhysicalDeviceSurfaceSupportKHR(physical_, static_cast<u32>(queues_.graphics_family), surface_, &supported);
    if (r != VK_SUCCESS || supported != VK_TRUE) {
        RhiLog(LogVerbosity::Error, "RecreateSurface: the adapter cannot present to the new surface; a full reset is needed");
        instance_.Fns().DestroySurfaceKHR(instance_.Handle(), surface_, nullptr);
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
    if (swapchain_) {
        RhiLog(LogVerbosity::Error, "CreateSwapchain: the device already has a swapchain");
        return nullptr;
    }
    swapchain_ = VulkanSwapchain::Create(this, desc);
    swapchain_live_.store(swapchain_ ? 1 : 0, std::memory_order_release);
    return swapchain_.Get();
}

void VulkanDevice::DestroySwapchain(Swapchain* swapchain) {
    if (swapchain == nullptr) {
        return;
    }
    ENGINE_ASSERT(swapchain == swapchain_.Get(), "DestroySwapchain: not this device's swapchain");
    swapchain_.Reset();
    swapchain_live_.store(0, std::memory_order_release);
}

} // namespace engine_placeholder::rhi::vulkan
