#include "rhi/vulkan/vk_command_list.h"

#include "log/assert.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_barrier.h"
#include "rhi/vulkan/vk_result.h"
#include "rhi/vulkan/vulkan_device.h"

namespace engine_placeholder::rhi::vulkan {

VulkanCommandList::VulkanCommandList(VulkanDevice* device, VkCommandBuffer buffer) : device_(device), buffer_(buffer) {}

void VulkanCommandList::Recycle(u64 frame) {
    phase_ = Phase::Initial;
    frame_ = frame;
    label_depth_ = 0;
    failed_ = false;
}

bool VulkanCommandList::Recording(const char* what) const {
    ENGINE_ASSERT(phase_ == Phase::Recording, what);
    return !failed_;
}

void VulkanCommandList::Unsupported(const char* what) const {
    RhiLog(LogVerbosity::Error, "Vulkan: CommandList::%s is not implemented yet", what);
}

void VulkanCommandList::Begin() {
    ENGINE_ASSERT(phase_ == Phase::Initial, "CommandList::Begin: the list is already recording or was submitted");
    VkCommandBufferBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const VkResult r = device_->Fns().BeginCommandBuffer(buffer_, &info);
    device_->ReportResult(r);
    failed_ = r != VK_SUCCESS;
    phase_ = Phase::Recording;
}

void VulkanCommandList::End() {
    const bool usable = Recording("CommandList::End: the list is not recording");
    ENGINE_ASSERT(label_depth_ == 0, "CommandList::End: debug labels are unbalanced");
    if (usable) {
        const VkResult r = device_->Fns().EndCommandBuffer(buffer_);
        device_->ReportResult(r);
        failed_ = r != VK_SUCCESS;
    }
    phase_ = Phase::Ended;
}

void VulkanCommandList::CopyBuffer(BufferHandle src, BufferHandle dst, u64 src_offset, u64 dst_offset, u64 size) {
    const bool usable = Recording("CommandList::CopyBuffer: the list is not recording");
    VkBuffer vk_src = nullptr;
    VkBuffer vk_dst = nullptr;
    BufferDesc src_desc;
    BufferDesc dst_desc;
    ENGINE_ASSERT(device_->LookupBuffer(src, &vk_src, &src_desc), "CommandList::CopyBuffer: stale or invalid source handle");
    ENGINE_ASSERT(device_->LookupBuffer(dst, &vk_dst, &dst_desc), "CommandList::CopyBuffer: stale or invalid destination handle");
    ENGINE_ASSERT(Has(src_desc.usage, BufferUsage::CopySrc), "CommandList::CopyBuffer: the source lacks CopySrc usage");
    ENGINE_ASSERT(Has(dst_desc.usage, BufferUsage::CopyDst), "CommandList::CopyBuffer: the destination lacks CopyDst usage");
    ENGINE_ASSERT(size <= src_desc.size && src_offset <= src_desc.size - size, "CommandList::CopyBuffer: source range outside the buffer");
    ENGINE_ASSERT(size <= dst_desc.size && dst_offset <= dst_desc.size - size, "CommandList::CopyBuffer: destination range outside the buffer");
    ENGINE_ASSERT(vk_src != vk_dst || size == 0 || src_offset + size <= dst_offset || dst_offset + size <= src_offset,
                  "CommandList::CopyBuffer: the ranges overlap within one buffer");
    if (size == 0 || !usable) {
        return;
    }
    const VkBufferCopy region{src_offset, dst_offset, size};
    device_->Fns().CmdCopyBuffer(buffer_, vk_src, vk_dst, 1, &region);
}

void VulkanCommandList::Barrier(BufferHandle buffer, ResourceState before, ResourceState after) {
    const bool usable = Recording("CommandList::Barrier: the list is not recording");
    VkBuffer vk_buffer = nullptr;
    BufferDesc desc;
    ENGINE_ASSERT(device_->LookupBuffer(buffer, &vk_buffer, &desc), "CommandList::Barrier: stale or invalid buffer handle");
    ENGINE_ASSERT(desc.location == MemoryLocation::GpuOnly, "CommandList::Barrier: only GpuOnly buffers take barriers");
    BarrierScopes scopes;
    ENGINE_ASSERT(GetBarrierScopes(before, after, false, &scopes), "CommandList::Barrier: a state is not valid for a buffer");

    if (!usable) {
        return;
    }
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = scopes.src.stage;
    barrier.srcAccessMask = scopes.src.access;
    barrier.dstStageMask = scopes.dst.stage;
    barrier.dstAccessMask = scopes.dst.access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = vk_buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &barrier;
    device_->Fns().CmdPipelineBarrier2(buffer_, &dep);
}

void VulkanCommandList::PushDebugLabel(const char* name) {
    const bool usable = Recording("CommandList::PushDebugLabel: the list is not recording");
    ENGINE_ASSERT(name != nullptr, "CommandList::PushDebugLabel: name is null");
    ++label_depth_;
    if (usable && device_->Fns().CmdBeginDebugUtilsLabelEXT != nullptr) {
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        device_->Fns().CmdBeginDebugUtilsLabelEXT(buffer_, &label);
    }
}

void VulkanCommandList::PopDebugLabel() {
    const bool usable = Recording("CommandList::PopDebugLabel: the list is not recording");
    ENGINE_ASSERT(label_depth_ > 0, "CommandList::PopDebugLabel: no label is open");
    --label_depth_;
    if (usable && device_->Fns().CmdEndDebugUtilsLabelEXT != nullptr) {
        device_->Fns().CmdEndDebugUtilsLabelEXT(buffer_);
    }
}

// Commands that need textures, pipelines or bind groups arrive with those resources.
void VulkanCommandList::CopyBufferToTexture(BufferHandle, TextureHandle, const BufferTextureCopy&) { Unsupported("CopyBufferToTexture"); }
void VulkanCommandList::CopyTextureToBuffer(TextureHandle, BufferHandle, const BufferTextureCopy&) { Unsupported("CopyTextureToBuffer"); }
void VulkanCommandList::Barrier(TextureHandle, ResourceState, ResourceState) { Unsupported("Barrier(texture)"); }
void VulkanCommandList::BeginRendering(const RenderingDesc&) { Unsupported("BeginRendering"); }
void VulkanCommandList::EndRendering() { Unsupported("EndRendering"); }
void VulkanCommandList::SetPipeline(PipelineHandle) { Unsupported("SetPipeline"); }
void VulkanCommandList::SetBindGroup(u32, BindGroupHandle) { Unsupported("SetBindGroup"); }
void VulkanCommandList::SetPushConstants(u32, const void*, u32) { Unsupported("SetPushConstants"); }
void VulkanCommandList::SetViewport(const Viewport&) { Unsupported("SetViewport"); }
void VulkanCommandList::SetScissor(const ScissorRect&) { Unsupported("SetScissor"); }
void VulkanCommandList::SetVertexBuffer(u32, BufferHandle, u64) { Unsupported("SetVertexBuffer"); }
void VulkanCommandList::SetIndexBuffer(BufferHandle, u64, IndexType) { Unsupported("SetIndexBuffer"); }
void VulkanCommandList::Draw(u32, u32, u32, u32) { Unsupported("Draw"); }
void VulkanCommandList::DrawIndexed(u32, u32, u32, i32, u32) { Unsupported("DrawIndexed"); }
void VulkanCommandList::Dispatch(u32, u32, u32) { Unsupported("Dispatch"); }

} // namespace engine_placeholder::rhi::vulkan
