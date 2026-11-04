#pragma once

#include "rhi/desc.h"
#include "rhi/enums.h"
#include "rhi/handle.h"
#include "types.h"

//! Recording interface for GPU commands, and the structures its commands take.

namespace engine_placeholder::rhi {

/// A viewport in framebuffer pixels with a depth range.
///
/// Thread-safety: thread-compatible plain data.
struct Viewport {
    /// Left edge in pixels.
    f32 x = 0.0f;
    /// Top edge in pixels.
    f32 y = 0.0f;
    /// Width in pixels.
    f32 width = 0.0f;
    /// Height in pixels.
    f32 height = 0.0f;
    /// Depth mapped to the near plane.
    f32 min_depth = 0.0f;
    /// Depth mapped to the far plane.
    f32 max_depth = 1.0f;
};

/// A scissor rectangle in framebuffer pixels.
///
/// Thread-safety: thread-compatible plain data.
struct ScissorRect {
    /// Left edge in pixels.
    i32 x = 0;
    /// Top edge in pixels.
    i32 y = 0;
    /// Width in pixels.
    u32 width = 0;
    /// Height in pixels.
    u32 height = 0;
};

/// One color attachment of a rendering pass.
///
/// Thread-safety: thread-compatible plain data.
struct ColorAttachmentDesc {
    /// The texture rendered to; must be in `ResourceState::ColorAttachment` and created with `ColorAttachment` usage.
    TextureHandle texture;
    /// What happens to the contents at the start of the pass.
    LoadOp load = LoadOp::Clear;
    /// What happens to the contents at the end of the pass.
    StoreOp store = StoreOp::Store;
    /// The clear color (red, green, blue, alpha) used when `load` is `Clear`.
    f32 clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

/// The depth attachment of a rendering pass.
///
/// Thread-safety: thread-compatible plain data.
struct DepthAttachmentDesc {
    /// The depth texture; must be in `ResourceState::DepthAttachment` and created with `DepthAttachment` usage.
    TextureHandle texture;
    /// What happens to the contents at the start of the pass.
    LoadOp load = LoadOp::Clear;
    /// What happens to the contents at the end of the pass.
    StoreOp store = StoreOp::Store;
    /// The depth clear value used when `load` is `Clear`.
    f32 clear_depth = 1.0f;
};

/// Describes a rendering pass (dynamic rendering): its attachments.
///
/// All attachments must have the same extent, which becomes the default viewport and scissor extent.
///
/// Thread-safety: thread-compatible plain data.
struct RenderingDesc {
    /// Color attachments; the first `color_count` are used.
    ColorAttachmentDesc color[kMaxColorAttachments];
    /// Number of valid entries in `color`.
    u32 color_count = 0;
    /// The depth attachment; used when `depth.texture` is valid.
    DepthAttachmentDesc depth;
};

/// Describes how a buffer and one mip level of one array layer of a texture are copied to or from each other.
///
/// Thread-safety: thread-compatible plain data.
struct BufferTextureCopy {
    /// Byte offset of the first texel row in the buffer.
    u64 buffer_offset = 0;
    /// Bytes between consecutive texel rows (block rows for compressed formats) in the buffer; 0 means tightly packed.
    u32 buffer_row_pitch = 0;
    /// The mip level of the texture.
    u32 mip_level = 0;
    /// The array layer of the texture.
    u32 array_layer = 0;
};

/// Records GPU commands for later submission with [[rhi::Device::Submit]].
///
/// A list is obtained from [[rhi::Device::CreateCommandList]], recorded between `Begin` and `End` on one
/// thread, then submitted. Each command lists the preconditions whose violation is a programming error
/// (`Aborts:`). Real backends assert them in debug builds and under validation layers; the null backend
/// logs and counts each violation instead, so tests can assert on them, and skips the offending command.
///
/// Resource states are explicit: before a resource is used in a way that needs a state, a `Barrier`
/// moves it there from the state it is in. Buffers in `MemoryLocation::CpuToGpu` and `GpuToCpu` memory
/// are exempt: they have a fixed state and never need barriers.
///
/// Thread-safety: thread-compatible. Record one list from one thread; different lists can be recorded on
///   different threads at the same time.
/// Ownership: a list is owned by the device and stays valid until the next `Device::BeginFrame`.
class CommandList {
public:
    virtual ~CommandList() = default;

    /// Starts recording.
    /// Aborts: the list is already recording or has been submitted.
    virtual void Begin() = 0;

    /// Finishes recording; the list can then be submitted.
    /// Aborts: the list is not recording, a rendering pass is open, or debug labels are unbalanced.
    virtual void End() = 0;

    /// Copies `size` bytes from `src` to `dst`.
    ///
    /// Param src: source buffer, in state `CopySrc`, created with `CopySrc` usage.
    /// Param dst: destination buffer, in state `CopyDst`, created with `CopyDst` usage.
    /// Param src_offset: byte offset in `src`.
    /// Param dst_offset: byte offset in `dst`.
    /// Param size: bytes to copy; both ranges must lie inside their buffers.
    /// Aborts: outside `Begin`/`End`, inside a rendering pass, a stale handle, a wrong state or usage, or a range outside a buffer.
    virtual void CopyBuffer(BufferHandle src, BufferHandle dst, u64 src_offset, u64 dst_offset, u64 size) = 0;

    /// Copies the whole of one mip level of one layer from a buffer into a texture.
    ///
    /// Param src: source buffer, in state `CopySrc`.
    /// Param dst: destination texture, in state `CopyDst`.
    /// Param region: where in the buffer the texel rows are and which mip level and layer to write.
    /// Aborts: outside `Begin`/`End`, inside a rendering pass, a stale handle, a wrong state or usage, a mip level or
    ///   layer out of range, a buffer range too small for the level, or a `buffer_offset` or `buffer_row_pitch` that
    ///   is not a multiple of the texel (block) size.
    virtual void CopyBufferToTexture(BufferHandle src, TextureHandle dst, const BufferTextureCopy& region) = 0;

    /// Copies the whole of one mip level of one layer from a texture into a buffer.
    ///
    /// Param src: source texture, in state `CopySrc`.
    /// Param dst: destination buffer, in state `CopyDst`.
    /// Param region: where in the buffer the texel rows go and which mip level and layer to read.
    /// Aborts: as for [[rhi::CommandList::CopyBufferToTexture]], with the roles swapped.
    virtual void CopyTextureToBuffer(TextureHandle src, BufferHandle dst, const BufferTextureCopy& region) = 0;

    /// Moves a buffer from state `before` to state `after`, making earlier writes visible to later uses.
    ///
    /// `before` may be `Undefined`, which means the previous contents are not needed and is always accepted.
    ///
    /// Param buffer: a `GpuOnly` buffer.
    /// Param before: the state the buffer is in when the barrier executes.
    /// Param after: the state it is in afterwards.
    /// Aborts: outside `Begin`/`End`, inside a rendering pass, a stale handle, a non-`GpuOnly` buffer, or (at submit
    ///   on the null backend) `before` is not the buffer's actual state.
    virtual void Barrier(BufferHandle buffer, ResourceState before, ResourceState after) = 0;

    /// Moves a texture (all mips and layers) from state `before` to state `after`.
    ///
    /// `before` may be `Undefined`, which discards the texture's contents and is always accepted.
    ///
    /// Param texture: the texture.
    /// Param before: the state the texture is in when the barrier executes.
    /// Param after: the state it is in afterwards.
    /// Aborts: outside `Begin`/`End`, inside a rendering pass, a stale handle, or (at submit on the null backend)
    ///   `before` is not the texture's actual state.
    virtual void Barrier(TextureHandle texture, ResourceState before, ResourceState after) = 0;

    /// Begins a rendering pass on the given attachments.
    ///
    /// Param desc: the attachments; at least one is required, extents must match.
    /// Aborts: outside `Begin`/`End`, already inside a pass, no attachments, a stale handle, a wrong
    ///   usage, mismatched extents, or an attachment not in its required state.
    virtual void BeginRendering(const RenderingDesc& desc) = 0;

    /// Ends the current rendering pass.
    /// Aborts: no rendering pass is open.
    virtual void EndRendering() = 0;

    /// Binds a pipeline for the following draws or dispatches.
    ///
    /// Param pipeline: a graphics pipeline (inside a pass whose attachment formats match it) or a compute
    ///   pipeline (outside a pass).
    /// A graphics pipeline binding ends with the rendering pass; bind it again in the next pass.
    ///
    /// Aborts: outside `Begin`/`End`, a stale handle, a graphics pipeline outside a pass or with mismatching
    ///   attachment formats, or a compute pipeline inside a pass.
    virtual void SetPipeline(PipelineHandle pipeline) = 0;

    /// Binds a bind group to a slot.
    ///
    /// Param slot: the bind group index, below the device's `max_bind_groups`.
    /// Param group: the group; its layout must equal the layout the pipeline declares for `slot` when drawing.
    /// Aborts: outside `Begin`/`End`, a stale handle, or a slot out of range.
    virtual void SetBindGroup(u32 slot, BindGroupHandle group) = 0;

    /// Sets push-constant data for the bound pipeline.
    ///
    /// Param offset: byte offset into the push-constant range; a multiple of 4.
    /// Param data: the bytes to copy; copied during the call.
    /// Param size: number of bytes; a multiple of 4.
    /// Aborts: no pipeline bound, or `offset + size` exceeds the pipeline's push-constant range.
    virtual void SetPushConstants(u32 offset, const void* data, u32 size) = 0;

    /// Sets the viewport.
    ///
    /// Param viewport: the viewport; width and height must be positive.
    /// Aborts: outside `Begin`/`End`, or a non-positive width or height.
    virtual void SetViewport(const Viewport& viewport) = 0;

    /// Sets the scissor rectangle.
    ///
    /// Param scissor: the rectangle.
    /// Aborts: outside `Begin`/`End`.
    virtual void SetScissor(const ScissorRect& scissor) = 0;

    /// Binds a vertex buffer to a vertex binding slot.
    ///
    /// Param slot: the vertex binding index, below `kMaxVertexBindings`.
    /// Param buffer: a buffer created with `Vertex` usage, in state `VertexBuffer` at draw time.
    /// Param offset: byte offset of the first element.
    /// Aborts: outside `Begin`/`End`, a stale handle, a missing usage flag, a slot out of range, or an offset past the end.
    virtual void SetVertexBuffer(u32 slot, BufferHandle buffer, u64 offset) = 0;

    /// Binds the index buffer.
    ///
    /// Param buffer: a buffer created with `Index` usage, in state `IndexBuffer` at draw time.
    /// Param offset: byte offset of the first index; a multiple of the index size.
    /// Param type: the index width.
    /// Aborts: outside `Begin`/`End`, a stale handle, a missing usage flag, or a misaligned offset.
    virtual void SetIndexBuffer(BufferHandle buffer, u64 offset, IndexType type) = 0;

    /// Draws non-indexed primitives.
    ///
    /// Param vertex_count: vertices per instance.
    /// Param instance_count: number of instances.
    /// Param first_vertex: index of the first vertex.
    /// Param first_instance: index of the first instance.
    /// Aborts: outside a rendering pass, no graphics pipeline bound, a bind group or vertex buffer the
    ///   pipeline needs is not set, or a bound resource is not in the state the pipeline needs.
    virtual void Draw(u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance) = 0;

    /// Draws indexed primitives.
    ///
    /// Param index_count: indices per instance.
    /// Param instance_count: number of instances.
    /// Param first_index: index of the first index.
    /// Param vertex_offset: value added to each index before fetching the vertex.
    /// Param first_instance: index of the first instance.
    /// Aborts: as for [[rhi::CommandList::Draw]], and no index buffer is bound.
    virtual void DrawIndexed(u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset,
                             u32 first_instance) = 0;

    /// Dispatches compute work.
    ///
    /// Param x: workgroup count in X.
    /// Param y: workgroup count in Y.
    /// Param z: workgroup count in Z.
    /// Aborts: inside a rendering pass, no compute pipeline bound, a bind group the pipeline needs is not
    ///   set, a bound resource is not in the state the pipeline needs, or a count exceeds the device limit.
    virtual void Dispatch(u32 x, u32 y, u32 z) = 0;

    /// Opens a named debug region shown in GPU debuggers.
    ///
    /// Param name: the label; copied during the call.
    /// Aborts: outside `Begin`/`End`.
    virtual void PushDebugLabel(const char* name) = 0;

    /// Closes the innermost debug region.
    /// Aborts: outside `Begin`/`End`, or no region is open.
    virtual void PopDebugLabel() = 0;
};

} // namespace engine_placeholder::rhi
