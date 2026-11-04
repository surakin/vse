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

} // namespace engine_placeholder::rhi::vulkan
