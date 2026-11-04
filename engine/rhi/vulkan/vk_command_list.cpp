#include "rhi/vulkan/vk_command_list.h"

#include "log/assert.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_barrier.h"
#include "rhi/vulkan/vk_format.h"
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

namespace {

u32 MipExtent(u32 base, u32 mip) {
    const u32 v = base >> mip;
    return v > 0 ? v : 1;
}

} // namespace

// Records the copy between a buffer and one level of a texture, in either direction.
void VulkanCommandList::CopyLevel(BufferHandle buffer, TextureHandle texture, const BufferTextureCopy& region, bool to_texture) {
    const char* what = to_texture ? "CommandList::CopyBufferToTexture" : "CommandList::CopyTextureToBuffer";
    const bool usable = Recording(what);
    VkBuffer vk_buffer = nullptr;
    BufferDesc buffer_desc;
    VulkanDevice::TextureInfo tex;
    ENGINE_ASSERT(device_->LookupBuffer(buffer, &vk_buffer, &buffer_desc), "CommandList copy: stale or invalid buffer handle");
    ENGINE_ASSERT(device_->LookupTexture(texture, &tex), "CommandList copy: stale or invalid texture handle");
    ENGINE_ASSERT(Has(buffer_desc.usage, to_texture ? BufferUsage::CopySrc : BufferUsage::CopyDst), "CommandList copy: the buffer lacks its copy usage");
    ENGINE_ASSERT(Has(tex.desc.usage, to_texture ? TextureUsage::CopyDst : TextureUsage::CopySrc), "CommandList copy: the texture lacks its copy usage");
    ENGINE_ASSERT(region.mip_level < tex.desc.mip_levels && region.array_layer < tex.desc.array_layers, "CommandList copy: mip or layer out of range");

    const FormatProperties& fp = FormatInfo(tex.desc.format);
    const u32 width = MipExtent(tex.desc.width, region.mip_level);
    const u32 height = MipExtent(tex.desc.height, region.mip_level);
    const u32 depth = tex.desc.dimension == TextureDimension::Tex3D ? MipExtent(tex.desc.depth, region.mip_level) : 1;
    const u64 row_bytes = FormatRowBytes(tex.desc.format, width);
    const u64 pitch = region.buffer_row_pitch != 0 ? region.buffer_row_pitch : row_bytes;
    ENGINE_ASSERT(pitch >= row_bytes && pitch % fp.bytes_per_block == 0 && region.buffer_offset % fp.bytes_per_block == 0,
                  "CommandList copy: buffer_offset or buffer_row_pitch is not a multiple of the texel size, or the pitch is below a row");
    const u64 block_rows = (static_cast<u64>(height) + fp.block_height - 1) / fp.block_height;
    const u64 needed = (depth * block_rows - 1) * pitch + row_bytes;
    ENGINE_ASSERT(region.buffer_offset <= buffer_desc.size && needed <= buffer_desc.size - region.buffer_offset,
                  "CommandList copy: the buffer range is too small for the level");
    if (!usable) {
        return;
    }
    VkBufferImageCopy copy{};
    copy.bufferOffset = region.buffer_offset;
    copy.bufferRowLength = static_cast<u32>(pitch / fp.bytes_per_block * fp.block_width);
    copy.bufferImageHeight = 0;
    ENGINE_ASSERT(CopyAspectOf(tex.desc.format) != 0, "CommandList copy: depth-stencil formats with a stencil aspect cannot be copied");
    copy.imageSubresource = {CopyAspectOf(tex.desc.format), region.mip_level, region.array_layer, 1};
    copy.imageExtent = {width, height, depth};
    if (to_texture) {
        device_->Fns().CmdCopyBufferToImage(buffer_, vk_buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    } else {
        device_->Fns().CmdCopyImageToBuffer(buffer_, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_buffer, 1, &copy);
    }
}

void VulkanCommandList::CopyBufferToTexture(BufferHandle src, TextureHandle dst, const BufferTextureCopy& region) {
    CopyLevel(src, dst, region, true);
}

void VulkanCommandList::CopyTextureToBuffer(TextureHandle src, BufferHandle dst, const BufferTextureCopy& region) {
    CopyLevel(dst, src, region, false);
}

void VulkanCommandList::Barrier(TextureHandle texture, ResourceState before, ResourceState after) {
    const bool usable = Recording("CommandList::Barrier: the list is not recording");
    VulkanDevice::TextureInfo tex;
    ENGINE_ASSERT(device_->LookupTexture(texture, &tex), "CommandList::Barrier: stale or invalid texture handle");
    BarrierScopes scopes;
    ENGINE_ASSERT(GetBarrierScopes(before, after, true, &scopes), "CommandList::Barrier: a state is not valid for a texture");
    if (!usable) {
        return;
    }
    CmdImageBarrier(device_->Fns(), buffer_, tex.image, tex.aspects, 0, tex.desc.mip_levels, 0, tex.desc.array_layers, before, after);
}

void VulkanCommandList::GenerateMips(TextureHandle texture) {
    const bool usable = Recording("CommandList::GenerateMips: the list is not recording");
    VulkanDevice::TextureInfo tex;
    ENGINE_ASSERT(device_->LookupTexture(texture, &tex), "CommandList::GenerateMips: stale or invalid texture handle");
    ENGINE_ASSERT(Has(tex.desc.usage, TextureUsage::Sampled) && Has(tex.desc.usage, TextureUsage::CopySrc) && Has(tex.desc.usage, TextureUsage::CopyDst),
                  "CommandList::GenerateMips: the texture needs Sampled, CopySrc and CopyDst usage");
    ENGINE_ASSERT(tex.desc.mip_levels > 1, "CommandList::GenerateMips: the texture has a single mip level");
    ENGINE_ASSERT(device_->CanGenerateMips(tex.desc.format), "CommandList::GenerateMips: the format cannot be blitted with a linear filter");
    if (!usable) {
        return;
    }
    const VkDeviceFns& fns = device_->Fns();
    const u32 levels = tex.desc.mip_levels;
    const u32 layers = tex.desc.array_layers;
    const bool is3d = tex.desc.dimension == TextureDimension::Tex3D;
    CmdImageBarrier(fns, buffer_, tex.image, tex.aspects, 0, 1, 0, layers, ResourceState::ShaderRead, ResourceState::CopySrc);
    CmdImageBarrier(fns, buffer_, tex.image, tex.aspects, 1, levels - 1, 0, layers, ResourceState::Undefined, ResourceState::CopyDst);
    for (u32 i = 1; i < levels; ++i) {
        VkImageBlit blit{};
        blit.srcSubresource = {tex.aspects, i - 1, 0, layers};
        blit.srcOffsets[1] = {static_cast<i32>(MipExtent(tex.desc.width, i - 1)), static_cast<i32>(MipExtent(tex.desc.height, i - 1)),
                              static_cast<i32>(is3d ? MipExtent(tex.desc.depth, i - 1) : 1)};
        blit.dstSubresource = {tex.aspects, i, 0, layers};
        blit.dstOffsets[1] = {static_cast<i32>(MipExtent(tex.desc.width, i)), static_cast<i32>(MipExtent(tex.desc.height, i)),
                              static_cast<i32>(is3d ? MipExtent(tex.desc.depth, i) : 1)};
        fns.CmdBlitImage(buffer_, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                         VK_FILTER_LINEAR);
        if (i + 1 < levels) {
            CmdImageBarrier(fns, buffer_, tex.image, tex.aspects, i, 1, 0, layers, ResourceState::CopyDst, ResourceState::CopySrc);
        }
    }
    CmdImageBarrier(fns, buffer_, tex.image, tex.aspects, 0, levels - 1, 0, layers, ResourceState::CopySrc, ResourceState::ShaderRead);
    CmdImageBarrier(fns, buffer_, tex.image, tex.aspects, levels - 1, 1, 0, layers, ResourceState::CopyDst, ResourceState::ShaderRead);
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
