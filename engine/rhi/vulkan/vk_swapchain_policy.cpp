#include "rhi/vulkan/vk_swapchain_policy.h"

#include "rhi/vulkan/vk_format.h"

namespace vse::rhi::vulkan
{

SurfaceFormatChoice ChooseSurfaceFormat(const VkSurfaceFormatKHR* formats, usize count)
{
    SurfaceFormatChoice choice;
    static const VkFormat kPreferred[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
    for (VkFormat wanted : kPreferred)
    {
        for (usize i = 0; i < count; ++i)
        {
            if (formats[i].format == wanted && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                choice.format = formats[i];
                choice.preferred = true;
                return choice;
            }
        }
    }
    // Fallback: a format the RHI can describe, in the sRGB-nonlinear color space if any is (so the output is not
    // taken for something else), otherwise in whatever color space the first usable entry has.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (usize i = 0; i < count; ++i)
        {
            if (FromVkFormat(formats[i].format) != Format::Undefined &&
                (pass == 1 || formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
            {
                choice.format = formats[i];
                return choice;
            }
        }
    }
    return choice;
}

RequestedFormatChoice ChooseRequestedSurfaceFormat(const VkSurfaceFormatKHR* formats, usize count, Format requested,
                                                   HdrMode hdr)
{
    RequestedFormatChoice out;
    if (hdr == HdrMode::ScRgbLinear)
    {
        for (usize i = 0; i < count; ++i)
        {
            if (formats[i].format == VK_FORMAT_R16G16B16A16_SFLOAT &&
                formats[i].colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
            {
                out.format = formats[i];
                out.space = ColorSpace::ScRgbLinear;
                return out;  // HDR decides the format; a conflicting `requested` is ignored
            }
        }
        out.fallback_note = "the surface offers no scRGB (16-bit float, extended sRGB linear) format; using SDR";
    }
    else if (hdr == HdrMode::Hdr10)
    {
        out.fallback_note = "HDR10 needs a 10-bit format the RHI does not have yet; using SDR";
    }
    if (requested != Format::Undefined)
    {
        for (usize i = 0; i < count; ++i)
        {
            if (FromVkFormat(formats[i].format) == requested &&
                formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                out.format = formats[i];
                return out;
            }
        }
        if (out.fallback_note == nullptr)
        {
            out.fallback_note = "the surface does not offer the requested image format; using the default";
        }
    }
    out.format = ChooseSurfaceFormat(formats, count).format;
    return out;
}

void FillSwapchainCaps(const VkSurfaceFormatKHR* formats, usize format_count, const VkPresentModeKHR* modes,
                       usize mode_count, SwapchainCaps* out)
{
    *out = SwapchainCaps{};
    for (usize i = 0; i < mode_count; ++i)
    {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            out->present_mode_supported[static_cast<usize>(PresentMode::Mailbox)] = true;
        }
        else if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            out->present_mode_supported[static_cast<usize>(PresentMode::Immediate)] = true;
        }
    }
    for (usize i = 0; i < format_count; ++i)
    {
        if (formats[i].format == VK_FORMAT_R16G16B16A16_SFLOAT &&
            formats[i].colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
        {
            out->hdr_scrgb = true;
        }
        const Format f = FromVkFormat(formats[i].format);
        if (f == Format::Undefined || formats[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
            out->format_count >= 8)
        {
            continue;
        }
        bool seen = false;
        for (u32 j = 0; j < out->format_count; ++j)
        {
            seen = seen || out->formats[j] == f;
        }
        if (!seen)
        {
            out->formats[out->format_count++] = f;
        }
    }
}

VkPresentModeKHR ChoosePresentMode(const VkPresentModeKHR* modes, usize count, PresentMode requested)
{
    VkPresentModeKHR wanted = VK_PRESENT_MODE_FIFO_KHR;
    if (requested == PresentMode::Mailbox)
    {
        wanted = VK_PRESENT_MODE_MAILBOX_KHR;
    }
    else if (requested == PresentMode::Immediate)
    {
        wanted = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    for (usize i = 0; i < count; ++i)
    {
        if (modes[i] == wanted)
        {
            return wanted;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

u32 ChooseImageCount(u32 min_images, u32 max_images)
{
    u32 count = min_images + 1;
    if (max_images != 0 && count > max_images)
    {
        count = max_images;
    }
    return count;
}

VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& caps, u32 width, u32 height)
{
    if (caps.currentExtent.width != 0xFFFFFFFFu)
    {
        return caps.currentExtent;
    }
    VkExtent2D extent{width, height};
    if (width == 0 || height == 0)
    {
        return VkExtent2D{0, 0};
    }
    if (extent.width < caps.minImageExtent.width)
    {
        extent.width = caps.minImageExtent.width;
    }
    if (extent.width > caps.maxImageExtent.width)
    {
        extent.width = caps.maxImageExtent.width;
    }
    if (extent.height < caps.minImageExtent.height)
    {
        extent.height = caps.minImageExtent.height;
    }
    if (extent.height > caps.maxImageExtent.height)
    {
        extent.height = caps.maxImageExtent.height;
    }
    return extent;
}

VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported)
{
    if (supported == 0 || (supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0)
    {
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    static const VkCompositeAlphaFlagBitsKHR kOthers[] = {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                                          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                                          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR};
    for (VkCompositeAlphaFlagBitsKHR bit : kOthers)
    {
        if ((supported & bit) != 0)
        {
            return bit;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

VkImageUsageFlags ChooseImageUsage(VkImageUsageFlags supported)
{
    if ((supported & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
    {
        return 0;
    }
    return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (supported & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
}

}  // namespace vse::rhi::vulkan
