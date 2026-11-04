#pragma once

#include "rhi/enums.h"
#include "rhi/vulkan/vk_functions.h"

//! The one table that turns an RHI resource state into Vulkan stages, accesses and image layouts.

namespace engine_placeholder::rhi::vulkan {

/// The Vulkan synchronization scope and layout of one [[rhi::ResourceState]].
///
/// Thread-safety: thread-compatible plain data.
struct StateScope {
    /// Pipeline stages that touch the resource in this state (`VK_PIPELINE_STAGE_2_*`).
    VkPipelineStageFlags2 stage = 0;
    /// Memory accesses made in this state (`VK_ACCESS_2_*`).
    VkAccessFlags2 access = 0;
    /// The image layout of this state; `VK_IMAGE_LAYOUT_UNDEFINED` for buffers and for `Undefined`.
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

/// The source and destination halves of one barrier.
///
/// Thread-safety: thread-compatible plain data.
struct BarrierScopes {
    /// Stages, accesses and layout to wait for.
    StateScope src;
    /// Stages, accesses and layout to make visible to.
    StateScope dst;
};

/// Looks up the barrier scopes for a state transition of a buffer or an image.
///
/// A transition from `Undefined` waits on all earlier work and, for an image, discards the contents (old layout
/// `UNDEFINED`). `Undefined` is never a valid destination. Buffers support `VertexBuffer`, `IndexBuffer`,
/// `UniformBuffer`, `ShaderRead`, `ShaderWrite`, `CopySrc` and `CopyDst`; images support every state except the first three.
///
/// Param before: the state the resource is in.
/// Param after: the state it moves to.
/// Param is_image: whether the resource is an image rather than a buffer.
/// Param out: receives the scopes; must not be null.
/// Returns: true if the transition is valid for that kind of resource.
/// Fails: returns false, leaving `out` unchanged, for an unsupported state or an `Undefined` destination.
bool GetBarrierScopes(ResourceState before, ResourceState after, bool is_image, BarrierScopes* out);

/// Records an image memory barrier for a state transition on a range of subresources.
///
/// Param fns: the device function table.
/// Param cb: a command buffer in the recording state.
/// Param image: the image.
/// Param aspects: the aspects the barrier covers.
/// Param base_mip: first mip level.
/// Param mip_count: number of mip levels.
/// Param base_layer: first array layer.
/// Param layer_count: number of array layers.
/// Param before: the state the range is in (`Undefined` discards its contents).
/// Param after: the state it moves to.
/// Returns: true if the barrier was recorded.
/// Fails: returns false, recording nothing, if [[rhi::vulkan::GetBarrierScopes]] rejects the transition for an image.
bool CmdImageBarrier(const VkDeviceFns& fns, VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspects, u32 base_mip, u32 mip_count,
                     u32 base_layer, u32 layer_count, ResourceState before, ResourceState after);

} // namespace engine_placeholder::rhi::vulkan
