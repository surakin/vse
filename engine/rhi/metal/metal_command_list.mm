// The Metal command list (Phase 26 Day 320; metal_command_list.h).

#include "rhi/metal/metal_command_list.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"

#include "log/assert.h"
#include "rhi/metal/metal_format.h"
#include "rhi/rhi_log.h"

#import <Foundation/Foundation.h>

namespace engine_placeholder::rhi::metal
{

namespace
{

MTLLoadAction LoadOf(LoadOp op)
{
    return op == LoadOp::Load ? MTLLoadActionLoad : op == LoadOp::Clear ? MTLLoadActionClear : MTLLoadActionDontCare;
}

MTLStoreAction StoreOf(StoreOp op)
{
    return op == StoreOp::Store ? MTLStoreActionStore : MTLStoreActionDontCare;
}

}  // namespace

void MetalCommandList::Recycle(u64 frame)
{
    // What Metal autoreleases here is let go here: the caller (a job thread, a test) may have no pool that drains.
    @autoreleasepool
    {
        ENGINE_ASSERT(phase_ != Phase::Recording, "CreateCommandList: a list from this slot is still recording");
        phase_ = Phase::Idle;
        frame_ = frame;
        failed_ = false;
        buffer_ = nil;
    }
}

bool MetalCommandList::Recording()
{
    ENGINE_ASSERT(phase_ == Phase::Recording, "CommandList: a command outside Begin/End");
    return phase_ == Phase::Recording && !failed_;
}

void MetalCommandList::Fail(const char* what, const char* why)
{
    RhiLog(LogVerbosity::Error, "%s: %s; the list will not be submitted", what, why);
    failed_ = true;
}

void MetalCommandList::Begin()
{
    ENGINE_ASSERT(phase_ == Phase::Idle, "Begin: the list was already begun (lists are valid for one recording)");
    phase_ = Phase::Recording;
    failed_ = false;
    counts_ = Counts{};
    pipeline_ = {};
    pipeline_dirty_ = false;
    groups_ = {};
    groups_dirty_ = 0;
    push_size_ = 0;
    push_dirty_ = false;
    index_buffer_ = nil;
    label_count_ = 0;
    label_overflow_ = 0;
    labels_on_encoder_ = 0;
    // A retained command buffer: every resource it references stays alive until the GPU is done with it. Its own
    // autorelease pool: lists are recorded on long-lived job threads, whose pools never drain on their own.
    @autoreleasepool
    {
        buffer_ = [device_->Queue() commandBuffer];
    }
    if (buffer_ == nil)
    {
        Fail("Begin", "the queue gave no command buffer");
        return;
    }
    buffer_.label = @"engine command list";
}

void MetalCommandList::End()
{
    ENGINE_ASSERT(phase_ == Phase::Recording, "End: the list is not recording");
    ENGINE_ASSERT(!in_pass_, "End: a rendering pass is still open");
    ENGINE_ASSERT(label_count_ == 0 && label_overflow_ == 0, "End: a debug label is still open");
    EndEncoder();
    phase_ = Phase::Ended;
}

// ---- encoders ----

id<MTLCommandEncoder> MetalCommandList::CurrentEncoder() const
{
    switch (kind_)
    {
    case EncoderKind::Render: return render_;
    case EncoderKind::Blit: return blit_;
    case EncoderKind::Compute: return compute_;
    case EncoderKind::None: break;
    }
    return nil;
}

void MetalCommandList::EndEncoder()
{
    if (kind_ == EncoderKind::None)
    {
        return;
    }
    id<MTLCommandEncoder> encoder = CurrentEncoder();
    for (u32 i = 0; i < labels_on_encoder_; ++i)
    {
        [encoder popDebugGroup];
    }
    labels_on_encoder_ = 0;
    [encoder endEncoding];
    render_ = nil;
    blit_ = nil;
    compute_ = nil;
    kind_ = EncoderKind::None;
    // A new encoder starts with nothing bound: everything is applied again at the next draw or dispatch.
    binder_.Reset();
    pipeline_dirty_ = pipeline_.IsValid();
    groups_dirty_ = ~0u;
    push_dirty_ = push_size_ != 0;
}

void MetalCommandList::ReplayLabels()
{
    id<MTLCommandEncoder> encoder = CurrentEncoder();
    @autoreleasepool
    {
        for (u32 i = 0; i < label_count_; ++i)
        {
            [encoder pushDebugGroup:[NSString stringWithUTF8String:labels_[i].Data()]];
        }
    }
    labels_on_encoder_ = label_count_;
}

id<MTLBlitCommandEncoder> MetalCommandList::Blit()
{
    if (kind_ != EncoderKind::Blit)
    {
        EndEncoder();
        @autoreleasepool
        {
            blit_ = [buffer_ blitCommandEncoder];
        }
        kind_ = EncoderKind::Blit;
        ++counts_.blit_encoders;
        ReplayLabels();
    }
    return blit_;
}

id<MTLComputeCommandEncoder> MetalCommandList::Compute()
{
    if (kind_ != EncoderKind::Compute)
    {
        EndEncoder();
        @autoreleasepool
        {
            compute_ = [buffer_ computeCommandEncoder];
        }
        kind_ = EncoderKind::Compute;
        ++counts_.compute_encoders;
        ReplayLabels();
    }
    return compute_;
}

// ---- copies, barriers, mips ----

void MetalCommandList::CopyBuffer(BufferHandle src, BufferHandle dst, u64 src_offset, u64 dst_offset, u64 size)
{
    // What Metal autoreleases here is let go here: the caller (a job thread, a test) may have no pool that drains.
    @autoreleasepool
    {
        if (!Recording())
        {
            return;
        }
        ENGINE_ASSERT(!in_pass_, "CopyBuffer: inside a rendering pass");
        // Metal on macOS copies between buffers in 4-byte units (the RHI's contract lets a backend ask it).
        ENGINE_ASSERT(src_offset % 4 == 0 && dst_offset % 4 == 0 && size % 4 == 0,
                      "CopyBuffer: Metal needs offsets and a size that are multiples of 4");
        id<MTLBuffer> s = device_->BufferObject(src);
        id<MTLBuffer> d = device_->BufferObject(dst);
        if (s == nil || d == nil)
        {
            Fail("CopyBuffer", "a buffer is invalid or destroyed");
            return;
        }
        if (size == 0)
        {
            return;
        }
        [Blit() copyFromBuffer:s sourceOffset:src_offset toBuffer:d destinationOffset:dst_offset size:size];
    }
}

void MetalCommandList::CopyBufferToTexture(BufferHandle src, TextureHandle dst, const BufferTextureCopy& region)
{
    // What Metal autoreleases here is let go here: the caller (a job thread, a test) may have no pool that drains.
    @autoreleasepool
    {
        if (!Recording())
        {
            return;
        }
        ENGINE_ASSERT(!in_pass_, "CopyBufferToTexture: inside a rendering pass");
        id<MTLBuffer> s = device_->BufferObject(src);
        Format format = Format::Undefined;
        id<MTLTexture> t = device_->TextureObject(dst, &format);
        if (s == nil || t == nil)
        {
            Fail("CopyBufferToTexture", "a buffer or texture is invalid or destroyed");
            return;
        }
        ENGINE_ASSERT(region.mip_level < t.mipmapLevelCount && region.array_layer < t.arrayLength,
                      "CopyBufferToTexture: mip or layer out of range");
        const FormatProperties& fp = FormatInfo(format);
        const u32 width = MipExtent(static_cast<u32>(t.width), region.mip_level);
        const u32 height = MipExtent(static_cast<u32>(t.height), region.mip_level);
        const u32 depth = t.textureType == MTLTextureType3D ? MipExtent(static_cast<u32>(t.depth), region.mip_level)
                                                            : 1;
        const u64 row = FormatRowBytes(format, width);
        const u64 pitch = region.buffer_row_pitch != 0 ? region.buffer_row_pitch : row;
        const u32 block_rows = (height + fp.block_height - 1) / fp.block_height;
        ENGINE_ASSERT(pitch >= row && pitch % fp.bytes_per_block == 0 && region.buffer_offset % fp.bytes_per_block == 0,
                      "CopyBufferToTexture: a row pitch or offset that is not a multiple of the texel size");
        ENGINE_ASSERT(region.buffer_offset + pitch * block_rows * depth <= s.length,
                      "CopyBufferToTexture: the buffer is too small for the level");
        [Blit() copyFromBuffer:s
                   sourceOffset:region.buffer_offset
              sourceBytesPerRow:pitch
            sourceBytesPerImage:pitch * block_rows
                     sourceSize:MTLSizeMake(width, height, depth)
                      toTexture:t
               destinationSlice:t.textureType == MTLTextureType3D ? 0 : region.array_layer
               destinationLevel:region.mip_level
              destinationOrigin:MTLOriginMake(0, 0, 0)];
    }
}

void MetalCommandList::CopyTextureToBuffer(TextureHandle src, BufferHandle dst, const BufferTextureCopy& region)
{
    // What Metal autoreleases here is let go here: the caller (a job thread, a test) may have no pool that drains.
    @autoreleasepool
    {
        if (!Recording())
        {
            return;
        }
        ENGINE_ASSERT(!in_pass_, "CopyTextureToBuffer: inside a rendering pass");
        Format format = Format::Undefined;
        id<MTLTexture> t = device_->TextureObject(src, &format);
        id<MTLBuffer> d = device_->BufferObject(dst);
        if (t == nil || d == nil)
        {
            Fail("CopyTextureToBuffer", "a buffer or texture is invalid or destroyed");
            return;
        }
        ENGINE_ASSERT(region.mip_level < t.mipmapLevelCount && region.array_layer < t.arrayLength,
                      "CopyTextureToBuffer: mip or layer out of range");
        ENGINE_ASSERT(t.sampleCount == 1, "CopyTextureToBuffer: a multisampled texture cannot be copied (resolve it)");
        const FormatProperties& fp = FormatInfo(format);
        const u32 width = MipExtent(static_cast<u32>(t.width), region.mip_level);
        const u32 height = MipExtent(static_cast<u32>(t.height), region.mip_level);
        const u32 depth = t.textureType == MTLTextureType3D ? MipExtent(static_cast<u32>(t.depth), region.mip_level)
                                                            : 1;
        const u64 row = FormatRowBytes(format, width);
        const u64 pitch = region.buffer_row_pitch != 0 ? region.buffer_row_pitch : row;
        const u32 block_rows = (height + fp.block_height - 1) / fp.block_height;
        ENGINE_ASSERT(pitch >= row && pitch % fp.bytes_per_block == 0 && region.buffer_offset % fp.bytes_per_block == 0,
                      "CopyTextureToBuffer: a row pitch or offset that is not a multiple of the texel size");
        ENGINE_ASSERT(region.buffer_offset + pitch * block_rows * depth <= d.length,
                      "CopyTextureToBuffer: the buffer is too small for the level");
        [Blit() copyFromTexture:t
                         sourceSlice:t.textureType == MTLTextureType3D ? 0 : region.array_layer
                         sourceLevel:region.mip_level
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(width, height, depth)
                            toBuffer:d
                   destinationOffset:region.buffer_offset
              destinationBytesPerRow:pitch
            destinationBytesPerImage:pitch * block_rows];
    }
}

void MetalCommandList::Barrier(BufferHandle buffer, ResourceState before, ResourceState)
{
    if (!Recording())
    {
        return;
    }
    ENGINE_ASSERT(!in_pass_, "Barrier: inside a rendering pass");
    ENGINE_ASSERT(device_->BufferObject(buffer) != nil, "Barrier: the buffer is invalid or destroyed");
    // Metal orders encoders and tracks the resource's use; only writes by one dispatch read by the next one in the same
    // compute encoder need a barrier (it costs nothing when Metal would have inserted it anyway).
    if (kind_ == EncoderKind::Compute && before == ResourceState::ShaderWrite)
    {
        [compute_ memoryBarrierWithScope:MTLBarrierScopeBuffers];
        ++counts_.memory_barriers;
        return;
    }
    ++counts_.skipped_barriers;
}

void MetalCommandList::Barrier(TextureHandle texture, ResourceState before, ResourceState)
{
    if (!Recording())
    {
        return;
    }
    ENGINE_ASSERT(!in_pass_, "Barrier: inside a rendering pass");
    ENGINE_ASSERT(device_->TextureObject(texture) != nil, "Barrier: the texture is invalid or destroyed");
    if (kind_ == EncoderKind::Compute && before == ResourceState::ShaderWrite)
    {
        [compute_ memoryBarrierWithScope:MTLBarrierScopeTextures];
        ++counts_.memory_barriers;
        return;
    }
    ++counts_.skipped_barriers;
}

void MetalCommandList::GenerateMips(TextureHandle texture)
{
    // What Metal autoreleases here is let go here: the caller (a job thread, a test) may have no pool that drains.
    @autoreleasepool
    {
        if (!Recording())
        {
            return;
        }
        ENGINE_ASSERT(!in_pass_, "GenerateMips: inside a rendering pass");
        Format format = Format::Undefined;
        id<MTLTexture> t = device_->TextureObject(texture, &format);
        if (t == nil)
        {
            Fail("GenerateMips", "the texture is invalid or destroyed");
            return;
        }
        ENGINE_ASSERT(t.mipmapLevelCount > 1, "GenerateMips: the texture has one mip level");
        ENGINE_ASSERT(device_->CanGenerateMips(format),
                      "GenerateMips: the format cannot be filtered and rendered (Device::CanGenerateMips)");
        // Metal halves each level with a linear filter in a blit: no compute shader.
        [Blit() generateMipmapsForTexture:t];
    }
}

// ---- rendering ----

void MetalCommandList::BeginRendering(const RenderingDesc& desc)
{
    ENGINE_ASSERT(phase_ == Phase::Recording, "BeginRendering: outside Begin/End");
    ENGINE_ASSERT(!in_pass_, "BeginRendering: a pass is already open");
    ENGINE_ASSERT(desc.color_count != 0 || desc.depth.texture.IsValid(), "BeginRendering: no attachments");
    // The pass is open from here even when the list has failed, so EndRendering and End pair with it as on a good list.
    in_pass_ = true;
    if (pipeline_.IsValid() && !info_.compute)
    {
        pipeline_ = {};  // a graphics pipeline binding ends with the pass (the RHI's rule); a compute one stays
    }
    if (!Recording())
    {
        return;
    }
    EndEncoder();
    @autoreleasepool
    {
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        u32 width = 0;
        u32 height = 0;
        for (u32 c = 0; c < desc.color_count; ++c)
        {
            const ColorAttachmentDesc& a = desc.color[c];
            id<MTLTexture> t = device_->TextureObject(a.texture);
            if (t == nil)
            {
                Fail("BeginRendering", "a color attachment is invalid or destroyed");
                return;
            }
            ENGINE_ASSERT((t.usage & MTLTextureUsageRenderTarget) != 0,
                          "BeginRendering: a color attachment lacks ColorAttachment usage");
            ENGINE_ASSERT(c == 0 || (t.width == width && t.height == height), "BeginRendering: mismatched extents");
            width = static_cast<u32>(t.width);
            height = static_cast<u32>(t.height);
            pass.colorAttachments[c].texture = t;
            pass.colorAttachments[c].loadAction = LoadOf(a.load);
            pass.colorAttachments[c].storeAction = StoreOf(a.store);
            pass.colorAttachments[c].clearColor = MTLClearColorMake(a.clear_color[0], a.clear_color[1],
                                                                    a.clear_color[2], a.clear_color[3]);
        }
        if (desc.depth.texture.IsValid())
        {
            Format depth_format = Format::Undefined;
            id<MTLTexture> t = device_->TextureObject(desc.depth.texture, &depth_format);
            if (t == nil)
            {
                Fail("BeginRendering", "the depth attachment is invalid or destroyed");
                return;
            }
            ENGINE_ASSERT(desc.color_count == 0 || (t.width == width && t.height == height),
                          "BeginRendering: mismatched extents");
            width = static_cast<u32>(t.width);
            height = static_cast<u32>(t.height);
            pass.depthAttachment.texture = t;
            pass.depthAttachment.loadAction = LoadOf(desc.depth.load);
            pass.depthAttachment.storeAction = StoreOf(desc.depth.store);
            pass.depthAttachment.clearDepth = desc.depth.clear_depth;
            if (FormatInfo(depth_format).has_stencil)
            {
                // The stencil aspect follows the depth ops, and a clear sets it to 0 (the RHI's rule).
                pass.stencilAttachment.texture = t;
                pass.stencilAttachment.loadAction = LoadOf(desc.depth.load);
                pass.stencilAttachment.storeAction = StoreOf(desc.depth.store);
                pass.stencilAttachment.clearStencil = 0;
            }
        }
        render_ = [buffer_ renderCommandEncoderWithDescriptor:pass];
        if (render_ == nil)
        {
            Fail("BeginRendering", "Metal gave no render encoder");
            return;
        }
        kind_ = EncoderKind::Render;
        ++counts_.render_encoders;
        pass_width_ = width;
        pass_height_ = height;
        ReplayLabels();  // bind groups and push constants are applied at the first draw (EndEncoder marked them)
        // The default viewport and scissor are the attachments' extent (Metal's default too).
    }
}

void MetalCommandList::EndRendering()
{
    ENGINE_ASSERT(in_pass_, "EndRendering: no rendering pass is open");
    in_pass_ = false;
    EndEncoder();
    if (pipeline_.IsValid() && !info_.compute)
    {
        pipeline_ = {};  // the pass's graphics pipeline ends with it; a compute one bound before the pass stays
        pipeline_dirty_ = false;
    }
}

void MetalCommandList::SetPipeline(PipelineHandle pipeline)
{
    if (!Recording())
    {
        return;
    }
    if (pipeline == pipeline_ && !pipeline_dirty_)
    {
        return;
    }
    if (!device_->LookupPipeline(pipeline, &info_))
    {
        Fail("SetPipeline", "the pipeline is invalid or destroyed");
        return;
    }
    ENGINE_ASSERT(info_.compute != in_pass_, "SetPipeline: a graphics pipeline outside a pass or a compute one inside");
    pipeline_ = pipeline;
    pipeline_dirty_ = true;
    groups_dirty_ = ~0u;  // another pipeline may put the same groups at other slots
    push_dirty_ = push_size_ != 0;
}

void MetalCommandList::SetBindGroup(u32 slot, BindGroupHandle group)
{
    if (!Recording())
    {
        return;
    }
    ENGINE_ASSERT(slot < kMaxBindGroups, "SetBindGroup: slot out of range");
    groups_[slot] = group;
    groups_dirty_ |= 1u << slot;
}

void MetalCommandList::SetPushConstants(u32 offset, const void* data, u32 size)
{
    if (!Recording())
    {
        return;
    }
    ENGINE_ASSERT(pipeline_.IsValid(), "SetPushConstants: no pipeline bound");
    ENGINE_ASSERT(offset % 4 == 0 && size % 4 == 0 && offset + size <= kMaxPushConstantBytes && data != nullptr,
                  "SetPushConstants: a range that is not 4-byte aligned or exceeds the push-constant range");
    MemCopy(push_.Data() + offset, data, size);
    push_size_ = offset + size > push_size_ ? offset + size : push_size_;
    push_dirty_ = true;
}

void MetalCommandList::SetViewport(const Viewport& viewport)
{
    if (!Recording())
    {
        return;
    }
    ENGINE_ASSERT(viewport.width > 0.0f && viewport.height > 0.0f, "SetViewport: a non-positive width or height");
    ENGINE_ASSERT(in_pass_, "SetViewport: Metal sets the viewport on a render encoder: call it inside a pass");
    if (render_ != nil)
    {
        [render_ setViewport:(MTLViewport){viewport.x, viewport.y, viewport.width, viewport.height, viewport.min_depth,
                                           viewport.max_depth}];
    }
}

void MetalCommandList::SetScissor(const ScissorRect& scissor)
{
    if (!Recording())
    {
        return;
    }
    ENGINE_ASSERT(in_pass_, "SetScissor: Metal sets the scissor on a render encoder: call it inside a pass");
    if (render_ == nil)
    {
        return;
    }
    // Metal refuses a rectangle that leaves the attachments: it is clipped to them (an empty one draws nothing).
    const i64 x0 = scissor.x < 0 ? 0 : scissor.x;
    const i64 y0 = scissor.y < 0 ? 0 : scissor.y;
    i64 x1 = static_cast<i64>(scissor.x) + scissor.width;
    i64 y1 = static_cast<i64>(scissor.y) + scissor.height;
    x1 = x1 > pass_width_ ? pass_width_ : x1;
    y1 = y1 > pass_height_ ? pass_height_ : y1;
    MTLScissorRect r;
    r.x = static_cast<NSUInteger>(x0 < pass_width_ ? x0 : pass_width_);
    r.y = static_cast<NSUInteger>(y0 < pass_height_ ? y0 : pass_height_);
    r.width = x1 > static_cast<i64>(r.x) ? static_cast<NSUInteger>(x1 - static_cast<i64>(r.x)) : 0;
    r.height = y1 > static_cast<i64>(r.y) ? static_cast<NSUInteger>(y1 - static_cast<i64>(r.y)) : 0;
    [render_ setScissorRect:r];
}

void MetalCommandList::SetVertexBuffer(u32 slot, BufferHandle buffer, u64 offset)
{
    if (!Recording())
    {
        return;
    }
    ENGINE_ASSERT(slot < kMaxVertexBindings, "SetVertexBuffer: slot out of range");
    ENGINE_ASSERT(in_pass_, "SetVertexBuffer: outside a rendering pass");
    id<MTLBuffer> b = device_->BufferObject(buffer);
    if (b == nil)
    {
        Fail("SetVertexBuffer", "the buffer is invalid or destroyed");
        return;
    }
    ENGINE_ASSERT(offset < b.length, "SetVertexBuffer: an offset past the end");
    [render_ setVertexBuffer:b offset:offset atIndex:MetalVertexBufferSlot(slot)];
}

void MetalCommandList::SetIndexBuffer(BufferHandle buffer, u64 offset, IndexType type)
{
    if (!Recording())
    {
        return;
    }
    id<MTLBuffer> b = device_->BufferObject(buffer);
    if (b == nil)
    {
        Fail("SetIndexBuffer", "the buffer is invalid or destroyed");
        return;
    }
    ENGINE_ASSERT(offset % (type == IndexType::UInt16 ? 2 : 4) == 0, "SetIndexBuffer: a misaligned offset");
    index_buffer_ = b;
    index_offset_ = offset;
    index_type_ = type;
}

bool MetalCommandList::ApplyRender()
{
    ENGINE_ASSERT(in_pass_, "Draw: outside a rendering pass");
    ENGINE_ASSERT(pipeline_.IsValid() && !info_.compute, "Draw: no graphics pipeline bound");
    if (render_ == nil || !pipeline_.IsValid())
    {
        return false;
    }
    if (pipeline_dirty_)
    {
        [render_ setRenderPipelineState:info_.render];
        [render_ setDepthStencilState:info_.depth];
        [render_ setCullMode:info_.cull];
        [render_ setFrontFacingWinding:info_.winding];
        [render_ setTriangleFillMode:info_.fill];
        pipeline_dirty_ = false;
    }
    const MetalStageSlots* fragment = &info_.fragment;  // empty for a depth-only pipeline
    for (u32 set = 0; set < kMaxBindGroups; ++set)
    {
        if ((groups_dirty_ & (1u << set)) != 0 && groups_[set].IsValid())
        {
            if (!binder_.BindRender(*device_, render_, &info_.vertex, fragment, set, groups_[set]))
            {
                Fail("Draw", "a bind group could not be bound");
                return false;
            }
        }
    }
    groups_dirty_ = 0;
    if (push_dirty_)
    {
        binder_.PushRender(render_, &info_.vertex, fragment, push_.Data(), push_size_);
        push_dirty_ = false;
    }
    counts_.binder_calls = binder_.IssuedCalls();
    return true;
}

void MetalCommandList::Draw(u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance)
{
    // What Metal autoreleases here is let go here: the caller (a job thread, a test) may have no pool that drains.
    @autoreleasepool
    {
        if (!Recording() || !ApplyRender() || vertex_count == 0 || instance_count == 0)
        {
            return;
        }
        [render_ drawPrimitives:info_.primitive
                    vertexStart:first_vertex
                    vertexCount:vertex_count
                  instanceCount:instance_count
                   baseInstance:first_instance];
    }
}

void MetalCommandList::DrawIndexed(u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset,
                                   u32 first_instance)
{
    // What Metal autoreleases here is let go here: the caller (a job thread, a test) may have no pool that drains.
    @autoreleasepool
    {
        if (!Recording())
        {
            return;
        }
        ENGINE_ASSERT(index_buffer_ != nil, "DrawIndexed: no index buffer bound");
        if (!ApplyRender() || index_buffer_ == nil || index_count == 0 || instance_count == 0)
        {
            return;
        }
        const u64 size = index_type_ == IndexType::UInt16 ? 2 : 4;
        // Strips restart at the all-ones index on Metal always (the RHI's primitive_restart asks for the same).
        [render_ drawIndexedPrimitives:info_.primitive
                            indexCount:index_count
                             indexType:index_type_ == IndexType::UInt16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
                           indexBuffer:index_buffer_
                     indexBufferOffset:index_offset_ + first_index * size
                         instanceCount:instance_count
                            baseVertex:vertex_offset
                          baseInstance:first_instance];
    }
}

bool MetalCommandList::ApplyCompute()
{
    ENGINE_ASSERT(!in_pass_, "Dispatch: inside a rendering pass");
    ENGINE_ASSERT(pipeline_.IsValid() && info_.compute, "Dispatch: no compute pipeline bound");
    if (!pipeline_.IsValid() || !info_.compute)
    {
        return false;
    }
    id<MTLComputeCommandEncoder> encoder = Compute();
    if (pipeline_dirty_)
    {
        [encoder setComputePipelineState:info_.compute_state];
        pipeline_dirty_ = false;
    }
    for (u32 set = 0; set < kMaxBindGroups; ++set)
    {
        if ((groups_dirty_ & (1u << set)) != 0 && groups_[set].IsValid())
        {
            if (!binder_.BindCompute(*device_, encoder, info_.kernel, set, groups_[set]))
            {
                Fail("Dispatch", "a bind group could not be bound");
                return false;
            }
        }
    }
    groups_dirty_ = 0;
    if (push_dirty_)
    {
        binder_.PushCompute(encoder, info_.kernel, push_.Data(), push_size_);
        push_dirty_ = false;
    }
    counts_.binder_calls = binder_.IssuedCalls();
    return true;
}

void MetalCommandList::Dispatch(u32 x, u32 y, u32 z)
{
    // What Metal autoreleases here is let go here: the caller (a job thread, a test) may have no pool that drains.
    @autoreleasepool
    {
        if (!Recording() || !ApplyCompute() || x == 0 || y == 0 || z == 0)
        {
            return;
        }
        [compute_ dispatchThreadgroups:MTLSizeMake(x, y, z) threadsPerThreadgroup:info_.threadgroup];
    }
}

// ---- debug labels ----

void MetalCommandList::PushDebugLabel(const char* name)
{
    ENGINE_ASSERT(phase_ == Phase::Recording, "PushDebugLabel: outside Begin/End");
    if (label_count_ == kMaxLabels || label_overflow_ != 0)
    {
        ++label_overflow_;  // deeper than 16: not shown; its pop is consumed first
        return;
    }
    FormatTo(labels_[label_count_].Data(), labels_[label_count_].Size(), "%s", name != nullptr ? name : "");
    ++label_count_;
    if (kind_ != EncoderKind::None)
    {
        @autoreleasepool
        {
            [CurrentEncoder() pushDebugGroup:[NSString stringWithUTF8String:labels_[label_count_ - 1].Data()]];
        }
        ++labels_on_encoder_;
    }
}

void MetalCommandList::PopDebugLabel()
{
    ENGINE_ASSERT(phase_ == Phase::Recording, "PopDebugLabel: outside Begin/End");
    if (label_overflow_ != 0)
    {
        --label_overflow_;
        return;
    }
    ENGINE_ASSERT(label_count_ != 0, "PopDebugLabel: no region is open");
    if (label_count_ == 0)
    {
        return;
    }
    --label_count_;
    if (kind_ != EncoderKind::None && labels_on_encoder_ > label_count_)
    {
        id<MTLCommandEncoder> encoder = CurrentEncoder();
        [encoder popDebugGroup];
        --labels_on_encoder_;
    }
}

}  // namespace engine_placeholder::rhi::metal
