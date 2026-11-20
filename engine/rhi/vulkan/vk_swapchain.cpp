#include "rhi/vulkan/vk_swapchain.h"

#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_format.h"
#include "rhi/vulkan/vk_result.h"
#include "rhi/vulkan/vk_swapchain_policy.h"
#include "rhi/vulkan/vulkan_device.h"
#include "sync/mutex.h"

namespace vse::rhi::vulkan
{

namespace
{

// How long Acquire may wait for a free image. A window the compositor stopped drawing never frees one (Wayland
// reports no minimized state), so this bounds the wait and the caller sees Minimized.
constexpr u64 kAcquireTimeoutNs = 500ull * 1000 * 1000;
constexpr u64 kSemaphoreWaitNs = 5ull * 1000 * 1000 * 1000;

}  // namespace

UniquePtr<VulkanSwapchain> VulkanSwapchain::Create(VulkanDevice* device, const SwapchainDesc& desc)
{
    UniquePtr<VulkanSwapchain> swapchain = MakeUnique<VulkanSwapchain>(device);
    if (!swapchain)
    {
        return UniquePtr<VulkanSwapchain>();
    }
    swapchain->desc_ = desc;
    const u32 width = desc.width != 0 ? desc.width : device->window_->Width();
    const u32 height = desc.height != 0 ? desc.height : device->window_->Height();
    const SwapchainStatus status = swapchain->Build(width, height);
    if (status == SwapchainStatus::Lost || swapchain->swapchain_ == nullptr)
    {
        // A window that is minimized right now cannot get a swapchain yet; that is not an error for the caller, who
        // will see Minimized from Acquire, but there is nothing to present to without one.
        if (status != SwapchainStatus::Minimized)
        {
            return UniquePtr<VulkanSwapchain>();
        }
    }
    return swapchain;
}

VulkanSwapchain::~VulkanSwapchain()
{
    device_->fns_.DeviceWaitIdle(device_->device_);
    ReleaseImages();
    if (swapchain_ != nullptr)
    {
        device_->fns_.DestroySwapchainKHR(device_->device_, swapchain_, nullptr);
        swapchain_ = nullptr;
    }
    DestroySemaphores();
}

void VulkanSwapchain::DestroySemaphores()
{
    const VkDeviceFns& fns = device_->fns_;
    for (u32 i = 0; i < kMaxImages; ++i)
    {
        if (render_done_[i] != nullptr)
        {
            fns.DestroySemaphore(device_->device_, render_done_[i], nullptr);
            render_done_[i] = nullptr;
        }
    }
    for (u32 i = 0; i < kMaxImages + 1; ++i)
    {
        if (acquire_[i] != nullptr)
        {
            fns.DestroySemaphore(device_->device_, acquire_[i], nullptr);
            acquire_[i] = nullptr;
        }
        acquire_fence_[i] = 0;
    }
    acquire_next_ = 0;
}

// Drops the device textures that stand for the swapchain images, at once (their views must go before the swapchain).
// The device must be idle. Only these: a rebuild may run inside a frame (Acquire), and what that frame destroyed may
// still be used by its lists that are not submitted yet, so nothing else retired is collected here (Phase 13 Day 169).
void VulkanSwapchain::ReleaseImages()
{
    {
        LockGuard<Mutex> lock(device_->resource_mutex_);
        for (u32 i = 0; i < image_count_; ++i)
        {
            if (VulkanDevice::TextureRes* t = device_->textures_.Get(textures_[i]))
            {
                device_->DestroyTextureRes(t);
                device_->textures_.Free(textures_[i]);
            }
            textures_[i] = TextureHandle{};
        }
    }
    for (u32 i = 0; i < kMaxImages; ++i)
    {
        images_[i] = nullptr;
    }
    image_count_ = 0;
    acquired_ = false;
}

SwapchainStatus VulkanSwapchain::Build(u32 width, u32 height)
{
    VulkanDevice& d = *device_;
    const VkInstanceFns& ifns = d.instance_.Fns();

    VkSurfaceCapabilitiesKHR caps{};
    VkResult r = ifns.GetPhysicalDeviceSurfaceCapabilitiesKHR(d.physical_, d.surface_, &caps);
    if (r != VK_SUCCESS)
    {
        d.NoteDeviceLost(r);
        ENGINE_VK_CHECK(r);
        lost_ = true;
        return SwapchainStatus::Lost;
    }
    VkSurfaceFormatKHR formats[64];
    u32 format_count = 64;
    r = ifns.GetPhysicalDeviceSurfaceFormatsKHR(d.physical_, d.surface_, &format_count, formats);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE)
    {
        ENGINE_VK_CHECK(r);
        lost_ = true;
        return SwapchainStatus::Lost;
    }
    VkPresentModeKHR modes[16];
    u32 mode_count = 16;
    r = ifns.GetPhysicalDeviceSurfacePresentModesKHR(d.physical_, d.surface_, &mode_count, modes);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE)
    {
        ENGINE_VK_CHECK(r);
        lost_ = true;
        return SwapchainStatus::Lost;
    }

    FillSwapchainCaps(formats, format_count, modes, mode_count, &caps_);
    const RequestedFormatChoice requested = ChooseRequestedSurfaceFormat(formats, format_count, desc_.format,
                                                                         desc_.hdr);
    if (requested.fallback_note != nullptr)
    {
        RhiLog(LogVerbosity::Warn, "Vulkan: swapchain request not granted: %s", requested.fallback_note);
    }
    SurfaceFormatChoice format;
    format.format = requested.format;
    format.preferred = (desc_.format != Format::Undefined && requested.fallback_note == nullptr) ||
                       requested.space != ColorSpace::SrgbNonlinear ||
                       ChooseSurfaceFormat(formats, format_count).preferred;
    const VkExtent2D extent = ChooseExtent(caps, width, height);
    if (extent.width == 0 || extent.height == 0)
    {
        // Nothing to draw to yet; the request is remembered in desc_ and applied when the window returns (the old
        // images keep their format until then, so ImageFormat() keeps matching them).
        minimized_ = true;
        return SwapchainStatus::Minimized;
    }
    if (format.format.format == VK_FORMAT_UNDEFINED)
    {
        RhiLog(LogVerbosity::Error, "Vulkan: the surface offers no format the RHI can describe");
        lost_ = true;
        return SwapchainStatus::Lost;
    }
    if (!format.preferred)
    {
        RhiLog(LogVerbosity::Warn, "Vulkan: no sRGB 8-bit surface format; using VkFormat %d",
               static_cast<int>(format.format.format));
    }
    const VkPresentModeKHR chosen_mode = ChoosePresentMode(modes, mode_count, desc_.present_mode);
    const VkImageUsageFlags usage = ChooseImageUsage(caps.supportedUsageFlags);
    if (usage == 0)
    {
        RhiLog(LogVerbosity::Error, "Vulkan: the surface cannot be rendered to");
        lost_ = true;
        return SwapchainStatus::Lost;
    }
    u32 count = ChooseImageCount(caps.minImageCount, caps.maxImageCount);
    if (caps.minImageCount > kMaxImages)
    {
        RhiLog(LogVerbosity::Error, "Vulkan: the surface needs at least %u images; the backend supports %u",
               caps.minImageCount, kMaxImages);
        lost_ = true;
        return SwapchainStatus::Lost;
    }
    if (count > kMaxImages)
    {
        count = kMaxImages;
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = d.surface_;
    info.minImageCount = count;
    info.imageFormat = format.format.format;
    info.imageColorSpace = format.format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = usage;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = ChooseCompositeAlpha(caps.supportedCompositeAlpha);
    info.presentMode = chosen_mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;
    VkSwapchainKHR created = nullptr;
    r = d.fns_.CreateSwapchainKHR(d.device_, &info, nullptr, &created);
    if (r != VK_SUCCESS)
    {
        d.NoteDeviceLost(r);
        ENGINE_VK_CHECK(r);
        // A rejected request (a format, image count or mode the surface refuses) leaves the old swapchain intact; only
        // a lost device or surface makes this one unusable.
        if (r == VK_ERROR_DEVICE_LOST || r == VK_ERROR_SURFACE_LOST_KHR)
        {
            lost_ = true;
        }
        return SwapchainStatus::Lost;
    }

    // The old images may still be in flight: wait, then retire them and the old swapchain.
    d.fns_.DeviceWaitIdle(d.device_);
    ReleaseImages();
    if (swapchain_ != nullptr)
    {
        d.fns_.DestroySwapchainKHR(d.device_, swapchain_, nullptr);
    }
    DestroySemaphores();
    swapchain_ = created;
    actual_mode_ = chosen_mode == VK_PRESENT_MODE_MAILBOX_KHR     ? PresentMode::Mailbox
                   : chosen_mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? PresentMode::Immediate
                                                                  : PresentMode::Fifo;
    color_space_ = requested.space;
    format_ = format.format;
    extent_ = extent;
    minimized_ = false;
    needs_recreate_ = false;

    u32 image_count = kMaxImages;
    r = d.fns_.GetSwapchainImagesKHR(d.device_, swapchain_, &image_count, images_);
    if (r == VK_INCOMPLETE)
    {
        RhiLog(LogVerbosity::Error, "Vulkan: the swapchain has more than %u images", kMaxImages);
    }
    if (r != VK_SUCCESS)
    {
        ENGINE_VK_CHECK(r);
        lost_ = true;
        return SwapchainStatus::Lost;
    }
    image_count_ = image_count;

    TextureDesc desc;
    desc.width = extent.width;
    desc.height = extent.height;
    desc.format = FromVkFormat(format_.format);
    desc.usage = TextureUsage::ColorAttachment |
                 ((usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0 ? TextureUsage::CopySrc : TextureUsage::None);
    for (u32 i = 0; i < image_count_; ++i)
    {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = images_[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format_.format;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VulkanDevice::TextureRes res;
        r = d.fns_.CreateImageView(d.device_, &view_info, nullptr, &res.attachment_view);
        if (r != VK_SUCCESS)
        {
            d.NoteDeviceLost(r);
            ENGINE_VK_CHECK(r);
            lost_ = true;
            return SwapchainStatus::Lost;
        }
        res.image = images_[i];
        res.external = true;
        res.desc = desc;
        LockGuard<Mutex> lock(d.resource_mutex_);
        textures_[i] = d.textures_.Allocate(VulkanDevice::TextureRes(res));
    }

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (u32 i = 0; i < image_count_; ++i)
    {
        if (d.fns_.CreateSemaphore(d.device_, &sem_info, nullptr, &render_done_[i]) != VK_SUCCESS)
        {
            lost_ = true;
            return SwapchainStatus::Lost;
        }
    }
    for (u32 i = 0; i < image_count_ + 1; ++i)
    {
        if (d.fns_.CreateSemaphore(d.device_, &sem_info, nullptr, &acquire_[i]) != VK_SUCCESS)
        {
            lost_ = true;
            return SwapchainStatus::Lost;
        }
    }
    window_serial_ = d.window_->ResizeSerial();
    return SwapchainStatus::Ok;
}

SwapchainStatus VulkanSwapchain::Recreate()
{
    const SwapchainStatus status = Build(device_->window_->Width(), device_->window_->Height());
    return status == SwapchainStatus::Ok ? SwapchainStatus::Recreated : status;
}

SwapchainStatus VulkanSwapchain::Resize(u32 width, u32 height)
{
    if (lost_ || device_->DeviceLost())
    {
        return SwapchainStatus::Lost;
    }
    if (width == 0 || height == 0)
    {
        minimized_ = true;
        return SwapchainStatus::Minimized;
    }
    SwapchainDesc desc = desc_;
    desc.width = width;
    desc.height = height;
    return Reconfigure(desc);
}

SwapchainStatus VulkanSwapchain::Reconfigure(const SwapchainDesc& desc)
{
    ENGINE_ASSERT(!acquired_, "Swapchain::Reconfigure: the previous image was not presented");
    if (lost_ || device_->DeviceLost())
    {
        return SwapchainStatus::Lost;
    }
    const SwapchainDesc previous = desc_;
    desc_ = desc;  // remembered even when the window has no area now: the rebuild on return uses it
    const u32 width = desc.width != 0 ? desc.width : device_->window_->Width();
    const u32 height = desc.height != 0 ? desc.height : device_->window_->Height();
    const SwapchainStatus status = Build(width, height);
    if (status == SwapchainStatus::Lost && !lost_)
    {
        desc_ = previous;  // the request was refused and the old swapchain still stands: a rollback can reconfigure it
                           // again
    }
    return status;
}

SwapchainStatus VulkanSwapchain::Acquire()
{
    VulkanDevice& d = *device_;
    if (lost_ || d.DeviceLost())
    {
        return SwapchainStatus::Lost;
    }
    ENGINE_ASSERT(!acquired_, "Swapchain::Acquire: the previous image was not presented");
    bool recreated = false;
    if (needs_recreate_ && !d.window_->IsMinimized())
    {
        const SwapchainStatus status = Recreate();
        if (status != SwapchainStatus::Recreated)
        {
            return status;
        }
        recreated = true;
    }
    else if (minimized_ || d.window_->IsMinimized())
    {
        // A window that came back (or a Resize(0, 0) that is over) needs its swapchain built before it can be used.
        if (d.window_->IsMinimized())
        {
            minimized_ = true;
            return SwapchainStatus::Minimized;
        }
        const SwapchainStatus status = Build(d.window_->Width(), d.window_->Height());
        if (status != SwapchainStatus::Ok)
        {
            return status;
        }
        recreated = true;
    }
    else if (d.window_->ResizeSerial() != window_serial_)
    {
        const SwapchainStatus status = Recreate();
        if (status != SwapchainStatus::Recreated)
        {
            return status;
        }
        recreated = true;
    }

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const u32 slot = acquire_next_;
        if (acquire_fence_[slot] != 0 && !d.WaitTimeline(acquire_fence_[slot], kSemaphoreWaitNs))
        {
            RhiLog(
                LogVerbosity::Error,
                "Vulkan: the GPU did not finish with an earlier image acquisition within %llu s; treating the swapchain as lost",
                static_cast<unsigned long long>(kSemaphoreWaitNs / 1000000000ull));
            lost_ = true;
            return SwapchainStatus::Lost;
        }
        u32 index = 0;
        const VkResult r = d.fns_.AcquireNextImageKHR(d.device_, swapchain_, kAcquireTimeoutNs, acquire_[slot], nullptr,
                                                      &index);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
        {
            // Suboptimal still delivers a usable image; a change of window size is caught by the resize serial.
            VkSemaphoreSubmitInfo wait{};
            wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            wait.semaphore = acquire_[slot];
            wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            u64 value = 0;
            {
                LockGuard<Mutex> lock(d.submit_mutex_);
                value = d.SubmitRawLocked([](void*, VkCommandBuffer) {}, nullptr, &wait, 1, nullptr, 0);
            }
            if (value == 0)
            {
                return SwapchainStatus::Lost;
            }
            acquire_fence_[slot] = value;
            acquire_next_ = (slot + 1) % (image_count_ + 1);
            current_ = index;
            acquired_ = true;
            return recreated ? SwapchainStatus::Recreated : SwapchainStatus::Ok;
        }
        if (r == VK_TIMEOUT || r == VK_NOT_READY)
        {
            return SwapchainStatus::Minimized;
        }
        if (r == VK_ERROR_OUT_OF_DATE_KHR)
        {
            const SwapchainStatus status = Recreate();
            if (status != SwapchainStatus::Recreated)
            {
                return status;
            }
            recreated = true;
            continue;
        }
        d.NoteDeviceLost(r);
        ENGINE_VK_CHECK(r);
        lost_ = true;
        return SwapchainStatus::Lost;
    }
    return SwapchainStatus::Minimized;
}

SwapchainStatus VulkanSwapchain::Present()
{
    VulkanDevice& d = *device_;
    ENGINE_ASSERT(acquired_, "Swapchain::Present: no image has been acquired");
    acquired_ = false;
    if (lost_ || d.DeviceLost() || d.CountPresent())
    {
        return SwapchainStatus::Lost;
    }
    VkResult r;
    {
        LockGuard<Mutex> lock(d.submit_mutex_);
        // An empty batch after everything submitted so far: it signals the binary semaphore the present waits on.
        VkSemaphoreSubmitInfo signal{};
        signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal.semaphore = render_done_[current_];
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        if (d.SubmitRawLocked([](void*, VkCommandBuffer) {}, nullptr, nullptr, 0, &signal, 1) == 0)
        {
            return SwapchainStatus::Lost;
        }
        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_done_[current_];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &current_;
        r = d.fns_.QueuePresentKHR(d.queue_, &present);
    }
    if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
    {
        return SwapchainStatus::Ok;
    }
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // Rebuilding here would destroy the semaphore the present just queued a wait on; the next Acquire rebuilds.
        needs_recreate_ = true;
        return SwapchainStatus::Recreated;
    }
    d.NoteDeviceLost(r);
    ENGINE_VK_CHECK(r);
    lost_ = true;
    return SwapchainStatus::Lost;
}

Format VulkanSwapchain::ImageFormat() const
{
    return FromVkFormat(format_.format);
}

TextureHandle VulkanSwapchain::CurrentTexture() const
{
    return acquired_ ? textures_[current_] : TextureHandle{};
}

}  // namespace vse::rhi::vulkan
