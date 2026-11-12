#include "rhi/vulkan/vk_command_list.h"

#include "log/assert.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_barrier.h"
#include "rhi/vulkan/vk_format.h"
#include "rhi/vulkan/vk_result.h"
#include "rhi/vulkan/vulkan_device.h"
#include "rhi/vulkan/vk_pipeline_policy.h"

namespace engine_placeholder::rhi::vulkan
{

VulkanCommandList::VulkanCommandList(VulkanDevice* device, VkCommandBuffer buffer) : device_(device), buffer_(buffer) {}

void VulkanCommandList::Recycle(u64 frame)
{
    phase_ = Phase::Initial;
    frame_ = frame;
    label_depth_ = 0;
    failed_ = false;
    in_rendering_ = false;
    pass_color_count_ = 0;
    pass_depth_format_ = Format::Undefined;
    graphics_bound_ = false;
    compute_bound_ = false;
    index_bound_ = false;
    pipeline_layout_ = nullptr;
    push_bytes_ = 0;
    push_stages_ = ShaderStage::None;
    pipeline_layout_count_ = 0;
    for (u32 i = 0; i < kMaxBindGroups; ++i)
    {
        group_sets_[i] = nullptr;
        group_layout_ids_[i] = 0;
    }
    groups_dirty_ = 0;
}

bool VulkanCommandList::Recording(const char* what) const
{
    ENGINE_ASSERT(phase_ == Phase::Recording, what);
    return !failed_;
}

void VulkanCommandList::Unsupported(const char* what) const
{
    RhiLog(LogVerbosity::Error, "Vulkan: CommandList::%s is not implemented yet", what);
}

void VulkanCommandList::Begin()
{
    ENGINE_ASSERT(phase_ == Phase::Initial, "CommandList::Begin: the list is already recording or was submitted");
    VkCommandBufferBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const VkResult r = device_->Fns().BeginCommandBuffer(buffer_, &info);
    device_->ReportResult(r);
    failed_ = r != VK_SUCCESS;
    phase_ = Phase::Recording;
}

void VulkanCommandList::End()
{
    const bool usable = Recording("CommandList::End: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::End: a rendering pass is still open");
    ENGINE_ASSERT(label_depth_ == 0, "CommandList::End: debug labels are unbalanced");
    if (usable)
    {
        const VkResult r = device_->Fns().EndCommandBuffer(buffer_);
        device_->ReportResult(r);
        failed_ = r != VK_SUCCESS;
    }
    phase_ = Phase::Ended;
}

void VulkanCommandList::CopyBuffer(BufferHandle src, BufferHandle dst, u64 src_offset, u64 dst_offset, u64 size)
{
    const bool usable = Recording("CommandList::CopyBuffer: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::CopyBuffer: not allowed inside a rendering pass");
    VkBuffer vk_src = nullptr;
    VkBuffer vk_dst = nullptr;
    BufferDesc src_desc;
    BufferDesc dst_desc;
    ENGINE_ASSERT(device_->LookupBuffer(src, &vk_src, &src_desc),
                  "CommandList::CopyBuffer: stale or invalid source handle");
    ENGINE_ASSERT(device_->LookupBuffer(dst, &vk_dst, &dst_desc),
                  "CommandList::CopyBuffer: stale or invalid destination handle");
    ENGINE_ASSERT(Has(src_desc.usage, BufferUsage::CopySrc), "CommandList::CopyBuffer: the source lacks CopySrc usage");
    ENGINE_ASSERT(Has(dst_desc.usage, BufferUsage::CopyDst),
                  "CommandList::CopyBuffer: the destination lacks CopyDst usage");
    ENGINE_ASSERT(size <= src_desc.size && src_offset <= src_desc.size - size,
                  "CommandList::CopyBuffer: source range outside the buffer");
    ENGINE_ASSERT(size <= dst_desc.size && dst_offset <= dst_desc.size - size,
                  "CommandList::CopyBuffer: destination range outside the buffer");
    ENGINE_ASSERT(vk_src != vk_dst || size == 0 || src_offset + size <= dst_offset || dst_offset + size <= src_offset,
                  "CommandList::CopyBuffer: the ranges overlap within one buffer");
    if (size == 0 || !usable)
    {
        return;
    }
    const VkBufferCopy region{src_offset, dst_offset, size};
    device_->Fns().CmdCopyBuffer(buffer_, vk_src, vk_dst, 1, &region);
}

void VulkanCommandList::Barrier(BufferHandle buffer, ResourceState before, ResourceState after)
{
    const bool usable = Recording("CommandList::Barrier: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::Barrier: not allowed inside a rendering pass");
    VkBuffer vk_buffer = nullptr;
    BufferDesc desc;
    ENGINE_ASSERT(device_->LookupBuffer(buffer, &vk_buffer, &desc),
                  "CommandList::Barrier: stale or invalid buffer handle");
    ENGINE_ASSERT(desc.location == MemoryLocation::GpuOnly, "CommandList::Barrier: only GpuOnly buffers take barriers");
    BarrierScopes scopes;
    ENGINE_ASSERT(GetBarrierScopes(before, after, false, &scopes),
                  "CommandList::Barrier: a state is not valid for a buffer");

    if (!usable)
    {
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

// Records the copy between a buffer and one level of a texture, in either direction.
void VulkanCommandList::CopyLevel(BufferHandle buffer, TextureHandle texture, const BufferTextureCopy& region,
                                  bool to_texture)
{
    const char* what = to_texture ? "CommandList::CopyBufferToTexture" : "CommandList::CopyTextureToBuffer";
    const bool usable = Recording(what);
    ENGINE_ASSERT(!in_rendering_, "CommandList copy: not allowed inside a rendering pass");
    VkBuffer vk_buffer = nullptr;
    BufferDesc buffer_desc;
    VulkanDevice::TextureInfo tex;
    ENGINE_ASSERT(device_->LookupBuffer(buffer, &vk_buffer, &buffer_desc),
                  "CommandList copy: stale or invalid buffer handle");
    ENGINE_ASSERT(device_->LookupTexture(texture, &tex), "CommandList copy: stale or invalid texture handle");
    ENGINE_ASSERT(Has(buffer_desc.usage, to_texture ? BufferUsage::CopySrc : BufferUsage::CopyDst),
                  "CommandList copy: the buffer lacks its copy usage");
    ENGINE_ASSERT(Has(tex.desc.usage, to_texture ? TextureUsage::CopyDst : TextureUsage::CopySrc),
                  "CommandList copy: the texture lacks its copy usage");
    ENGINE_ASSERT(region.mip_level < tex.desc.mip_levels && region.array_layer < tex.desc.array_layers,
                  "CommandList copy: mip or layer out of range");

    const FormatProperties& fp = FormatInfo(tex.desc.format);
    const u32 width = MipExtent(tex.desc.width, region.mip_level);
    const u32 height = MipExtent(tex.desc.height, region.mip_level);
    const u32 depth = tex.desc.dimension == TextureDimension::Tex3D ? MipExtent(tex.desc.depth, region.mip_level) : 1;
    const u64 row_bytes = FormatRowBytes(tex.desc.format, width);
    const u64 pitch = region.buffer_row_pitch != 0 ? region.buffer_row_pitch : row_bytes;
    ENGINE_ASSERT(
        pitch >= row_bytes && pitch % fp.bytes_per_block == 0 && region.buffer_offset % fp.bytes_per_block == 0,
        "CommandList copy: buffer_offset or buffer_row_pitch is not a multiple of the texel size, or the pitch is below a row");
    ENGINE_ASSERT(region.buffer_offset % device_->Caps().buffer_texture_copy_offset_alignment == 0 &&
                      pitch % device_->Caps().buffer_texture_copy_row_pitch_alignment == 0,
                  "CommandList copy: buffer_offset or the row pitch breaks the device's copy alignment (DeviceCaps)");
    const u64 block_rows = (static_cast<u64>(height) + fp.block_height - 1) / fp.block_height;
    const u64 needed = (depth * block_rows - 1) * pitch + row_bytes;
    ENGINE_ASSERT(region.buffer_offset <= buffer_desc.size && needed <= buffer_desc.size - region.buffer_offset,
                  "CommandList copy: the buffer range is too small for the level");
    if (!usable)
    {
        return;
    }
    VkBufferImageCopy copy{};
    copy.bufferOffset = region.buffer_offset;
    copy.bufferRowLength = static_cast<u32>(pitch / fp.bytes_per_block * fp.block_width);
    copy.bufferImageHeight = 0;
    ENGINE_ASSERT(CopyAspectOf(tex.desc.format) != 0,
                  "CommandList copy: depth-stencil formats with a stencil aspect cannot be copied");
    copy.imageSubresource = {CopyAspectOf(tex.desc.format), region.mip_level, region.array_layer, 1};
    copy.imageExtent = {width, height, depth};
    if (to_texture)
    {
        device_->Fns().CmdCopyBufferToImage(buffer_, vk_buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                            &copy);
    }
    else
    {
        device_->Fns().CmdCopyImageToBuffer(buffer_, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_buffer, 1,
                                            &copy);
    }
}

void VulkanCommandList::CopyBufferToTexture(BufferHandle src, TextureHandle dst, const BufferTextureCopy& region)
{
    CopyLevel(src, dst, region, true);
}

void VulkanCommandList::CopyTextureToBuffer(TextureHandle src, BufferHandle dst, const BufferTextureCopy& region)
{
    CopyLevel(dst, src, region, false);
}

void VulkanCommandList::Barrier(TextureHandle texture, ResourceState before, ResourceState after)
{
    const bool usable = Recording("CommandList::Barrier: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::Barrier: not allowed inside a rendering pass");
    VulkanDevice::TextureInfo tex;
    ENGINE_ASSERT(device_->LookupTexture(texture, &tex), "CommandList::Barrier: stale or invalid texture handle");
    BarrierScopes scopes;
    ENGINE_ASSERT(GetBarrierScopes(before, after, true, &scopes),
                  "CommandList::Barrier: a state is not valid for a texture");
    if (!usable)
    {
        return;
    }
    CmdImageBarrier(device_->Fns(), buffer_, tex.image, tex.aspects, 0, tex.desc.mip_levels, 0, tex.desc.array_layers,
                    before, after);
}

void VulkanCommandList::GenerateMips(TextureHandle texture)
{
    const bool usable = Recording("CommandList::GenerateMips: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::GenerateMips: not allowed inside a rendering pass");
    VulkanDevice::TextureInfo tex;
    ENGINE_ASSERT(device_->LookupTexture(texture, &tex), "CommandList::GenerateMips: stale or invalid texture handle");
    ENGINE_ASSERT(Has(tex.desc.usage, TextureUsage::Sampled) && Has(tex.desc.usage, TextureUsage::CopySrc) &&
                      Has(tex.desc.usage, TextureUsage::CopyDst),
                  "CommandList::GenerateMips: the texture needs Sampled, CopySrc and CopyDst usage");
    ENGINE_ASSERT(tex.desc.mip_levels > 1, "CommandList::GenerateMips: the texture has a single mip level");
    ENGINE_ASSERT(device_->CanGenerateMips(tex.desc.format),
                  "CommandList::GenerateMips: the format cannot be blitted with a linear filter");
    if (!usable)
    {
        return;
    }
    const VkDeviceFns& fns = device_->Fns();
    const u32 levels = tex.desc.mip_levels;
    const u32 layers = tex.desc.array_layers;
    const bool is3d = tex.desc.dimension == TextureDimension::Tex3D;
    CmdImageBarrier(fns, buffer_, tex.image, tex.aspects, 0, 1, 0, layers, ResourceState::ShaderRead,
                    ResourceState::CopySrc);
    CmdImageBarrier(fns, buffer_, tex.image, tex.aspects, 1, levels - 1, 0, layers, ResourceState::Undefined,
                    ResourceState::CopyDst);
    for (u32 i = 1; i < levels; ++i)
    {
        VkImageBlit blit{};
        blit.srcSubresource = {tex.aspects, i - 1, 0, layers};
        blit.srcOffsets[1] = {static_cast<i32>(MipExtent(tex.desc.width, i - 1)),
                              static_cast<i32>(MipExtent(tex.desc.height, i - 1)),
                              static_cast<i32>(is3d ? MipExtent(tex.desc.depth, i - 1) : 1)};
        blit.dstSubresource = {tex.aspects, i, 0, layers};
        blit.dstOffsets[1] = {static_cast<i32>(MipExtent(tex.desc.width, i)),
                              static_cast<i32>(MipExtent(tex.desc.height, i)),
                              static_cast<i32>(is3d ? MipExtent(tex.desc.depth, i) : 1)};
        fns.CmdBlitImage(buffer_, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        if (i + 1 < levels)
        {
            CmdImageBarrier(fns, buffer_, tex.image, tex.aspects, i, 1, 0, layers, ResourceState::CopyDst,
                            ResourceState::CopySrc);
        }
    }
    CmdImageBarrier(fns, buffer_, tex.image, tex.aspects, 0, levels - 1, 0, layers, ResourceState::CopySrc,
                    ResourceState::ShaderRead);
    CmdImageBarrier(fns, buffer_, tex.image, tex.aspects, levels - 1, 1, 0, layers, ResourceState::CopyDst,
                    ResourceState::ShaderRead);
}

namespace
{

VkAttachmentLoadOp ToVkLoadOp(LoadOp op)
{
    switch (op)
    {
    case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

VkAttachmentStoreOp ToVkStoreOp(StoreOp op)
{
    return op == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

}  // namespace

void VulkanCommandList::BeginRendering(const RenderingDesc& desc)
{
    const bool usable = Recording("CommandList::BeginRendering: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::BeginRendering: a rendering pass is already open");
    ENGINE_ASSERT(desc.color_count <= kMaxColorAttachments, "CommandList::BeginRendering: too many color attachments");
    ENGINE_ASSERT(desc.color_count > 0 || desc.depth.texture.IsValid(), "CommandList::BeginRendering: no attachments");

    VkRenderingAttachmentInfo colors[kMaxColorAttachments]{};
    VkRenderingAttachmentInfo depth{};
    VkRenderingAttachmentInfo stencil{};
    bool has_stencil = false;
    u32 width = 0;
    u32 height = 0;
    auto note_extent = [&](const VulkanDevice::TextureInfo& tex)
    {
        if (width == 0)
        {
            width = tex.desc.width;
            height = tex.desc.height;
        }
        ENGINE_ASSERT(tex.desc.width == width && tex.desc.height == height,
                      "CommandList::BeginRendering: attachment extents differ");
    };
    for (u32 i = 0; i < desc.color_count; ++i)
    {
        VulkanDevice::TextureInfo tex;
        ENGINE_ASSERT(device_->LookupTexture(desc.color[i].texture, &tex),
                      "CommandList::BeginRendering: stale or invalid color attachment");
        ENGINE_ASSERT(Has(tex.desc.usage, TextureUsage::ColorAttachment) && tex.attachment_view != nullptr,
                      "CommandList::BeginRendering: a color attachment lacks ColorAttachment usage");
        note_extent(tex);
        pass_color_formats_[i] = tex.desc.format;
        VkRenderingAttachmentInfo& a = colors[i];
        a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        a.imageView = tex.attachment_view;
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp = ToVkLoadOp(desc.color[i].load);
        a.storeOp = ToVkStoreOp(desc.color[i].store);
        for (u32 c = 0; c < 4; ++c)
        {
            a.clearValue.color.float32[c] = desc.color[i].clear_color[c];
        }
    }
    if (desc.depth.texture.IsValid())
    {
        VulkanDevice::TextureInfo tex;
        ENGINE_ASSERT(device_->LookupTexture(desc.depth.texture, &tex),
                      "CommandList::BeginRendering: stale or invalid depth attachment");
        ENGINE_ASSERT(Has(tex.desc.usage, TextureUsage::DepthAttachment) && tex.attachment_view != nullptr,
                      "CommandList::BeginRendering: the depth attachment lacks DepthAttachment usage");
        note_extent(tex);
        pass_depth_format_ = tex.desc.format;
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = tex.attachment_view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = ToVkLoadOp(desc.depth.load);
        depth.storeOp = ToVkStoreOp(desc.depth.store);
        depth.clearValue.depthStencil = {desc.depth.clear_depth, 0};
        has_stencil = FormatInfo(tex.desc.format).has_stencil;
        stencil = depth;  // the stencil aspect follows the depth load/store ops and clears to 0
    }
    in_rendering_ = true;
    pass_color_count_ = desc.color_count;
    if (!desc.depth.texture.IsValid())
    {
        pass_depth_format_ = Format::Undefined;
    }
    graphics_bound_ = false;  // a graphics pipeline binding ends with the pass
    index_bound_ = false;
    if (!usable)
    {
        return;
    }
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, {width, height}};
    info.layerCount = 1;
    info.colorAttachmentCount = desc.color_count;
    info.pColorAttachments = colors;
    info.pDepthAttachment = desc.depth.texture.IsValid() ? &depth : nullptr;
    info.pStencilAttachment = has_stencil ? &stencil : nullptr;
    device_->Fns().CmdBeginRendering(buffer_, &info);
    // The pass's extent is the default viewport and scissor.
    const VkViewport viewport{0.0f, 0.0f, static_cast<f32>(width), static_cast<f32>(height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    device_->Fns().CmdSetViewport(buffer_, 0, 1, &viewport);
    device_->Fns().CmdSetScissor(buffer_, 0, 1, &scissor);
}

void VulkanCommandList::EndRendering()
{
    const bool usable = Recording("CommandList::EndRendering: the list is not recording");
    ENGINE_ASSERT(in_rendering_, "CommandList::EndRendering: no rendering pass is open");
    in_rendering_ = false;
    graphics_bound_ = false;
    if (usable)
    {
        device_->Fns().CmdEndRendering(buffer_);
    }
}

void VulkanCommandList::PushDebugLabel(const char* name)
{
    const bool usable = Recording("CommandList::PushDebugLabel: the list is not recording");
    ENGINE_ASSERT(name != nullptr, "CommandList::PushDebugLabel: name is null");
    ++label_depth_;
    if (usable && device_->Fns().CmdBeginDebugUtilsLabelEXT != nullptr)
    {
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        device_->Fns().CmdBeginDebugUtilsLabelEXT(buffer_, &label);
    }
}

void VulkanCommandList::PopDebugLabel()
{
    const bool usable = Recording("CommandList::PopDebugLabel: the list is not recording");
    ENGINE_ASSERT(label_depth_ > 0, "CommandList::PopDebugLabel: no label is open");
    --label_depth_;
    if (usable && device_->Fns().CmdEndDebugUtilsLabelEXT != nullptr)
    {
        device_->Fns().CmdEndDebugUtilsLabelEXT(buffer_);
    }
}

void VulkanCommandList::SetPipeline(PipelineHandle pipeline)
{
    const bool usable = Recording("CommandList::SetPipeline: the list is not recording");
    VulkanDevice::PipelineInfo info;
    ENGINE_ASSERT(device_->LookupPipeline(pipeline, &info),
                  "CommandList::SetPipeline: stale or invalid pipeline handle");
    if (info.compute)
    {
        ENGINE_ASSERT(!in_rendering_,
                      "CommandList::SetPipeline: a compute pipeline cannot be bound inside a rendering pass");
    }
    else
    {
        ENGINE_ASSERT(in_rendering_,
                      "CommandList::SetPipeline: a graphics pipeline can only be bound inside a rendering pass");
        bool formats_match = info.color_count == pass_color_count_ && info.depth_format == pass_depth_format_;
        for (u32 i = 0; formats_match && i < info.color_count; ++i)
        {
            formats_match = info.color_formats[i] == pass_color_formats_[i];
        }
        ENGINE_ASSERT(formats_match,
                      "CommandList::SetPipeline: the pipeline's attachment formats differ from the rendering pass's");
    }
    graphics_bound_ = !info.compute;
    compute_bound_ = info.compute;
    groups_dirty_ = (1u << kMaxBindGroups) - 1;  // a new pipeline layout: bind every group again before the next draw
    pipeline_layout_ = info.layout;
    push_bytes_ = info.push_bytes;
    push_stages_ = info.push_stages;
    pipeline_layout_count_ = info.layout_count;
    for (u32 i = 0; i < kMaxBindGroups; ++i)
    {
        pipeline_layout_ids_[i] = info.layout_ids[i];
    }
    if (usable)
    {
        device_->Fns().CmdBindPipeline(
            buffer_, info.compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, info.pipeline);
    }
}

void VulkanCommandList::SetBindGroup(u32 slot, BindGroupHandle group)
{
    Recording("CommandList::SetBindGroup: the list is not recording");
    ENGINE_ASSERT(slot < device_->Caps().max_bind_groups,
                  "CommandList::SetBindGroup: slot is outside the device limit");
    VulkanDevice::BindGroupInfo info;
    ENGINE_ASSERT(device_->LookupBindGroup(group, &info),
                  "CommandList::SetBindGroup: stale or invalid bind group handle");
    ENGINE_ASSERT(info.resources_live, "CommandList::SetBindGroup: a resource the bind group refers to was destroyed");
    ENGINE_ASSERT(!info.transient || info.frame == frame_,
                  "CommandList::SetBindGroup: a transient bind group is only valid in the frame it was created in");
    group_sets_[slot] = info.set;
    group_layout_ids_[slot] = info.layout_id;
    groups_dirty_ |= 1u << slot;
}

void VulkanCommandList::FlushBindGroups(const char* what)
{
    for (u32 slot = 0; slot < pipeline_layout_count_; ++slot)
    {
        ENGINE_ASSERT(group_sets_[slot] != nullptr, what);
        if ((groups_dirty_ & (1u << slot)) != 0)
        {  // SetPipeline marks every slot dirty, so a clean slot was checked already
            ENGINE_ASSERT(group_layout_ids_[slot] == pipeline_layout_ids_[slot],
                          "the bind group's layout differs from the pipeline's layout for that slot");
            device_->Fns().CmdBindDescriptorSets(
                buffer_, compute_bound_ ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline_layout_, slot, 1, &group_sets_[slot], 0, nullptr);
        }
    }
    groups_dirty_ = 0;
}

void VulkanCommandList::SetPushConstants(u32 offset, const void* data, u32 size)
{
    const bool usable = Recording("CommandList::SetPushConstants: the list is not recording");
    ENGINE_ASSERT(pipeline_layout_ != nullptr, "CommandList::SetPushConstants: no pipeline is bound");
    ENGINE_ASSERT(
        data != nullptr && offset % 4 == 0 && size % 4 == 0 && size > 0,
        "CommandList::SetPushConstants: offset and size must be non-zero multiples of 4 and data must not be null");
    ENGINE_ASSERT(offset <= push_bytes_ && size <= push_bytes_ - offset,
                  "CommandList::SetPushConstants: the range exceeds the pipeline's push-constant range");
    if (usable)
    {
        device_->Fns().CmdPushConstants(buffer_, pipeline_layout_, ToVkShaderStages(push_stages_), offset, size, data);
    }
}

void VulkanCommandList::SetViewport(const Viewport& viewport)
{
    const bool usable = Recording("CommandList::SetViewport: the list is not recording");
    ENGINE_ASSERT(viewport.width > 0.0f && viewport.height > 0.0f,
                  "CommandList::SetViewport: the viewport must have a positive size");
    if (usable)
    {
        const VkViewport vk{viewport.x,      viewport.y,         viewport.width,
                            viewport.height, viewport.min_depth, viewport.max_depth};
        device_->Fns().CmdSetViewport(buffer_, 0, 1, &vk);
    }
}

void VulkanCommandList::SetScissor(const ScissorRect& scissor)
{
    const bool usable = Recording("CommandList::SetScissor: the list is not recording");
    if (usable)
    {
        const VkRect2D vk{{scissor.x, scissor.y}, {scissor.width, scissor.height}};
        device_->Fns().CmdSetScissor(buffer_, 0, 1, &vk);
    }
}

void VulkanCommandList::SetVertexBuffer(u32 slot, BufferHandle buffer, u64 offset)
{
    const bool usable = Recording("CommandList::SetVertexBuffer: the list is not recording");
    ENGINE_ASSERT(slot < kMaxVertexBindings, "CommandList::SetVertexBuffer: slot out of range");
    VkBuffer vk_buffer = nullptr;
    BufferDesc desc;
    ENGINE_ASSERT(device_->LookupBuffer(buffer, &vk_buffer, &desc),
                  "CommandList::SetVertexBuffer: stale or invalid buffer handle");
    ENGINE_ASSERT(Has(desc.usage, BufferUsage::Vertex), "CommandList::SetVertexBuffer: the buffer lacks Vertex usage");
    ENGINE_ASSERT(offset < desc.size, "CommandList::SetVertexBuffer: the offset is past the end of the buffer");
    if (usable)
    {
        const VkDeviceSize vk_offset = offset;
        device_->Fns().CmdBindVertexBuffers(buffer_, slot, 1, &vk_buffer, &vk_offset);
    }
}

void VulkanCommandList::SetIndexBuffer(BufferHandle buffer, u64 offset, IndexType type)
{
    const bool usable = Recording("CommandList::SetIndexBuffer: the list is not recording");
    VkBuffer vk_buffer = nullptr;
    BufferDesc desc;
    ENGINE_ASSERT(device_->LookupBuffer(buffer, &vk_buffer, &desc),
                  "CommandList::SetIndexBuffer: stale or invalid buffer handle");
    ENGINE_ASSERT(Has(desc.usage, BufferUsage::Index), "CommandList::SetIndexBuffer: the buffer lacks Index usage");
    const u64 index_size = type == IndexType::UInt16 ? 2 : 4;
    ENGINE_ASSERT(offset % index_size == 0 && offset < desc.size,
                  "CommandList::SetIndexBuffer: misaligned offset or past the end");
    index_bound_ = true;
    if (usable)
    {
        device_->Fns().CmdBindIndexBuffer(buffer_, vk_buffer, offset, ToVkIndexType(type));
    }
}

void VulkanCommandList::Draw(u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance)
{
    const bool usable = Recording("CommandList::Draw: the list is not recording");
    ENGINE_ASSERT(in_rendering_ && graphics_bound_,
                  "CommandList::Draw: needs a rendering pass with a graphics pipeline bound");
    if (usable)
    {
        FlushBindGroups("CommandList::Draw: the pipeline declares a bind group for a slot that has none");
        device_->Fns().CmdDraw(buffer_, vertex_count, instance_count, first_vertex, first_instance);
    }
}

void VulkanCommandList::DrawIndexed(u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset,
                                    u32 first_instance)
{
    const bool usable = Recording("CommandList::DrawIndexed: the list is not recording");
    ENGINE_ASSERT(in_rendering_ && graphics_bound_,
                  "CommandList::DrawIndexed: needs a rendering pass with a graphics pipeline bound");
    ENGINE_ASSERT(index_bound_, "CommandList::DrawIndexed: no index buffer is bound");
    if (usable)
    {
        FlushBindGroups("CommandList::DrawIndexed: the pipeline declares a bind group for a slot that has none");
        device_->Fns().CmdDrawIndexed(buffer_, index_count, instance_count, first_index, vertex_offset, first_instance);
    }
}

void VulkanCommandList::Dispatch(u32 x, u32 y, u32 z)
{
    const bool usable = Recording("CommandList::Dispatch: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::Dispatch: not allowed inside a rendering pass");
    ENGINE_ASSERT(compute_bound_, "CommandList::Dispatch: no compute pipeline is bound");
    const DeviceCaps& caps = device_->Caps();
    ENGINE_ASSERT(x <= caps.max_compute_work_group_count[0] && y <= caps.max_compute_work_group_count[1] &&
                      z <= caps.max_compute_work_group_count[2],
                  "CommandList::Dispatch: a workgroup count exceeds the device limit");
    if (usable)
    {
        FlushBindGroups("CommandList::Dispatch: the pipeline declares a bind group for a slot that has none");
        device_->Fns().CmdDispatch(buffer_, x, y, z);
    }
}

}  // namespace engine_placeholder::rhi::vulkan
