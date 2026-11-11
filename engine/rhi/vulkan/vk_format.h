#pragma once

#include "rhi/format.h"
#include "rhi/vulkan/vk_loader.h"

//! Mapping between the portable [[rhi::Format]] and `VkFormat`.

namespace engine_placeholder::rhi::vulkan {

/// Returns the `VkFormat` for a portable format, or `VK_FORMAT_UNDEFINED` for `Format::Undefined` and `Format::Count`.
VkFormat ToVkFormat(Format format);

/// Returns the portable format for a `VkFormat`, or `Format::Undefined` if the RHI has no equivalent.
Format FromVkFormat(VkFormat format);

/// Returns the image aspects a format has: color, or depth and (if present) stencil.
VkImageAspectFlags AspectsOf(Format format);

/// Returns the single aspect a buffer/image copy of a format uses.
///
/// - Param format: the texel format.
/// - Returns: color, or depth for a depth-only format; 0 for a format with a stencil aspect, which copies would have to split.
VkImageAspectFlags CopyAspectOf(Format format);

} // namespace engine_placeholder::rhi::vulkan
