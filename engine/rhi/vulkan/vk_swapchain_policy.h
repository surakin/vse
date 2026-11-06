#pragma once

#include "rhi/swapchain.h"
#include "rhi/vulkan/vk_loader.h"

//! The choices made when creating a swapchain, as pure functions over plain capability data so they can be tested
//! without a GPU or a window.

namespace engine_placeholder::rhi::vulkan {

/// A surface format choice and whether it was the preferred kind.
///
/// Thread-safety: thread-compatible plain data.
struct SurfaceFormatChoice {
    /// The format and color space to create the swapchain with; `format` is `VK_FORMAT_UNDEFINED` if nothing usable was offered.
    VkSurfaceFormatKHR format = {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    /// True if a sRGB 8-bit format with the sRGB-nonlinear color space was found; false if a fallback was taken.
    bool preferred = false;
};

/// Chooses the swapchain surface format.
///
/// Prefers `B8G8R8A8_SRGB`, then `R8G8B8A8_SRGB`, both with the sRGB-nonlinear color space. Otherwise takes the first
/// offered format that the RHI has a [[rhi::Format]] for, preferring one in the sRGB-nonlinear color space (and says
/// so through `preferred`), so the swapchain images can always be described to the user.
///
/// Param formats: the formats the surface supports.
/// Param count: number of entries.
/// Returns: the choice; `format.format` is `VK_FORMAT_UNDEFINED` if none of the offered formats is usable.
SurfaceFormatChoice ChooseSurfaceFormat(const VkSurfaceFormatKHR* formats, usize count);

/// A surface format choice made for a request that may ask for a format and for HDR.
///
/// Thread-safety: thread-compatible plain data.
struct RequestedFormatChoice {
    /// The format and color space to create the swapchain with; `VK_FORMAT_UNDEFINED` if nothing usable was offered.
    VkSurfaceFormatKHR format = {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    /// The RHI color space of the choice.
    ColorSpace space = ColorSpace::SrgbNonlinear;
    /// What was not granted: null if the request was met, otherwise a static sentence for the log.
    const char* fallback_note = nullptr;
};

/// Chooses the surface format for a request.
///
/// `hdr` first: `ScRgbLinear` needs `R16G16B16A16_SFLOAT` in `EXTENDED_SRGB_LINEAR`; `Hdr10` is never granted (the RHI has
/// no 10-bit format yet). Then `requested` (`Undefined` means no preference) must be offered in the sRGB-nonlinear color
/// space. Whatever cannot be granted falls back to [[rhi::vulkan::ChooseSurfaceFormat]] and sets `fallback_note`.
///
/// Param formats: the formats the surface supports.
/// Param count: number of entries.
/// Param requested: the preferred [[rhi::Format]].
/// Param hdr: the dynamic range wanted.
/// Returns: the choice.
RequestedFormatChoice ChooseRequestedSurfaceFormat(const VkSurfaceFormatKHR* formats, usize count, Format requested, HdrMode hdr);

/// Fills the capability description from what the surface offers.
///
/// Param formats: the surface formats.
/// Param format_count: number of entries.
/// Param modes: the present modes.
/// Param mode_count: number of entries.
/// Param out: receives the capabilities.
/// Ownership: `out` belongs to the caller.
void FillSwapchainCaps(const VkSurfaceFormatKHR* formats, usize format_count, const VkPresentModeKHR* modes, usize mode_count,
                       SwapchainCaps* out);

/// Chooses the presentation mode.
///
/// `Fifo` is always available and the default. `Mailbox` and `Immediate` are used only when requested and offered.
///
/// Param modes: the modes the surface supports.
/// Param count: number of entries.
/// Param requested: the mode the caller prefers.
/// Returns: the Vulkan present mode to use.
VkPresentModeKHR ChoosePresentMode(const VkPresentModeKHR* modes, usize count, PresentMode requested);

/// Chooses the number of swapchain images: one more than the minimum, so the application can render while the
/// driver holds one, capped by the maximum (0 means no maximum).
///
/// Param min_images: `minImageCount` of the surface.
/// Param max_images: `maxImageCount` of the surface, or 0 for unlimited.
/// Returns: the count.
u32 ChooseImageCount(u32 min_images, u32 max_images);

/// Chooses the image extent.
///
/// If the surface reports a fixed `currentExtent` it is used. Otherwise (Wayland reports 0xFFFFFFFF) the requested
/// size is clamped into the surface's minimum and maximum.
///
/// Param caps: the surface capabilities.
/// Param width: the wanted width, normally the window's.
/// Param height: the wanted height.
/// Returns: the extent; it may be 0 in a dimension, which means the window is minimized.
VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& caps, u32 width, u32 height);

/// Chooses the composite alpha: opaque if the compositor allows it, otherwise the first supported mode.
///
/// Param supported: `supportedCompositeAlpha` of the surface.
/// Returns: one composite-alpha bit; opaque when `supported` is 0.
VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported);

/// Chooses the image usage: color attachment, plus transfer-source when the surface allows it (for screenshots).
///
/// Param supported: `supportedUsageFlags` of the surface.
/// Returns: the usage flags, or 0 if the surface cannot be rendered to.
VkImageUsageFlags ChooseImageUsage(VkImageUsageFlags supported);

} // namespace engine_placeholder::rhi::vulkan
