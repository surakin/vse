#pragma once

#include "rhi/swapchain.h"
#include "rhi/vulkan/vk_functions.h"

//! The Vulkan implementation of [[rhi::Swapchain]].

namespace engine_placeholder::rhi::vulkan {

class VulkanDevice;

/// The presentable images of the window a [[rhi::vulkan::VulkanDevice]] was created for.
///
/// The images are registered as device textures (state tracked by the application with barriers: `Undefined` or
/// `Present` to `ColorAttachment` and back to `Present`). `Acquire` and `Present` each submit an empty batch that
/// carries the binary semaphores presentation needs, so the application's command lists only use the timeline fence.
///
/// Thread-safety: owner-thread-only, and not to be used while another thread submits to the same device.
/// Ownership: created by [[rhi::Device::CreateSwapchain]], destroyed with [[rhi::Device::DestroySwapchain]].
class VulkanSwapchain final : public Swapchain {
public:
    /// Waits for the device to go idle and releases the swapchain, its images and semaphores.
    ~VulkanSwapchain() override;

    SwapchainStatus Acquire() override;
    SwapchainStatus Present() override;
    SwapchainStatus Resize(u32 width, u32 height) override;
    Format ImageFormat() const override;
    u32 Width() const override { return extent_.width; }
    u32 Height() const override { return extent_.height; }
    TextureHandle CurrentTexture() const override;

    /// Creates a swapchain for the device's window.
    ///
    /// Param device: the device; must have been created with a window.
    /// Param desc: the presentation mode and initial size.
    /// Returns: the swapchain, or null.
    /// Fails: returns null, logging the reason, if the surface offers no usable format or cannot be rendered to.
    /// Ownership: the caller owns the result.
    static VulkanSwapchain* Create(VulkanDevice* device, const SwapchainDesc& desc);

private:
    explicit VulkanSwapchain(VulkanDevice* device) : device_(device) {}
    // (Re)builds the swapchain for the given size; keeps the old one if the new extent is empty.
    SwapchainStatus Build(u32 width, u32 height);
    void ReleaseImages();
    void DestroySemaphores();
    SwapchainStatus Recreate();

    static constexpr u32 kMaxImages = 16;

    VulkanDevice* device_;
    PresentMode mode_ = PresentMode::Fifo;
    VkSwapchainKHR swapchain_ = nullptr;
    VkSurfaceFormatKHR format_{};
    VkExtent2D extent_{0, 0};
    u32 image_count_ = 0;
    VkImage images_[kMaxImages] = {};
    TextureHandle textures_[kMaxImages];
    VkSemaphore render_done_[kMaxImages] = {};
    VkSemaphore acquire_[kMaxImages + 1] = {};
    u64 acquire_fence_[kMaxImages + 1] = {};
    u32 acquire_next_ = 0;
    u32 current_ = 0;
    bool acquired_ = false;
    bool minimized_ = false;
    bool needs_recreate_ = false;
    bool lost_ = false;
    u64 window_serial_ = 0;
};

} // namespace engine_placeholder::rhi::vulkan
