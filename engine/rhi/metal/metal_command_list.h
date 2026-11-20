#pragma once

// The Metal command list (Phase 21 Day 304): one MTLCommandBuffer per recording, with render, blit and compute encoders
// opened as the commands need them. Objective-C++.
//
// A rendering pass is one render encoder. Outside a pass, copies and mips share a blit encoder and dispatches a compute
// encoder; the open one is ended when the kind changes. The pipeline, the bind groups and the push constants are
// applied at draw and dispatch time, so a new encoder (which starts with nothing bound) gets them again. Metal tracks
// hazards on the resources (decision 9): a barrier is a memory barrier inside a compute encoder and nothing elsewhere,
// since encoders are ordered.

#include "containers/array.h"
#include "rhi/command_list.h"
#include "rhi/metal/metal_device.h"

#import <Metal/Metal.h>

namespace vse::rhi::metal
{

class MetalCommandList final : public CommandList
{
public:
    // Where a list is in its life; Submit checks it.
    enum class Phase : u8
    {
        Idle,       // handed out, not begun
        Recording,  // between Begin and End
        Ended,      // ready to submit
        Submitted,  // taken by Submit: committed, or refused with its batch
    };

    explicit MetalCommandList(MetalDevice* device) : device_(device) {}

    void Begin() override;
    void End() override;
    void CopyBuffer(BufferHandle src, BufferHandle dst, u64 src_offset, u64 dst_offset, u64 size) override;
    void CopyBufferToTexture(BufferHandle src, TextureHandle dst, const BufferTextureCopy& region) override;
    void CopyTextureToBuffer(TextureHandle src, BufferHandle dst, const BufferTextureCopy& region) override;
    void Barrier(BufferHandle buffer, ResourceState before, ResourceState after) override;
    void Barrier(TextureHandle texture, ResourceState before, ResourceState after) override;
    void GenerateMips(TextureHandle texture) override;
    void BeginRendering(const RenderingDesc& desc) override;
    void EndRendering() override;
    void SetPipeline(PipelineHandle pipeline) override;
    void SetBindGroup(u32 slot, BindGroupHandle group) override;
    void SetPushConstants(u32 offset, const void* data, u32 size) override;
    void SetViewport(const Viewport& viewport) override;
    void SetScissor(const ScissorRect& scissor) override;
    void SetVertexBuffer(u32 slot, BufferHandle buffer, u64 offset) override;
    void SetIndexBuffer(BufferHandle buffer, u64 offset, IndexType type) override;
    void Draw(u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance) override;
    void DrawIndexed(u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset,
                     u32 first_instance) override;
    void Dispatch(u32 x, u32 y, u32 z) override;
    void PushDebugLabel(const char* name) override;
    void PopDebugLabel() override;

    // The device's side (metal_device_commands.mm): the list's frame, phase and command buffer.
    MetalDevice* Owner() const { return device_; }
    u64 Frame() const { return frame_; }
    Phase GetPhase() const { return phase_; }
    void SetPhase(Phase phase) { phase_ = phase; }
    // Whether a command failed while recording (logged); Submit then refuses the batch.
    bool Failed() const { return failed_; }
    // Makes the list ready to record again in `frame` (its slot's earlier frame has finished).
    void Recycle(u64 frame);
    // The recorded command buffer, for Submit to commit; nil before Begin.
    id<MTLCommandBuffer> Buffer() const { return buffer_; }

    // What was recorded, for tests: encoders opened of each kind, and barriers that became a memory barrier or nothing.
    struct Counts
    {
        u32 render_encoders = 0;
        u32 blit_encoders = 0;
        u32 compute_encoders = 0;
        u32 memory_barriers = 0;
        u32 skipped_barriers = 0;
        u32 binder_calls = 0;  // set*Buffer/Texture/Sampler and push calls the binder issued
    };
    const Counts& GetCounts() const { return counts_; }

private:
    enum class EncoderKind : u8
    {
        None,
        Render,
        Blit,
        Compute,
    };
    bool Recording();  // asserts Begin/End; false also once a command failed
    void EndEncoder();
    id<MTLCommandEncoder> CurrentEncoder() const;  // nil when none is open
    id<MTLBlitCommandEncoder> Blit();
    id<MTLComputeCommandEncoder> Compute();
    void Fail(const char* what, const char* why);
    bool ApplyRender();
    bool ApplyCompute();
    void ReplayLabels();

    MetalDevice* device_;
    Phase phase_ = Phase::Idle;
    u64 frame_ = 0;
    bool failed_ = false;
    id<MTLCommandBuffer> buffer_ = nil;
    EncoderKind kind_ = EncoderKind::None;
    id<MTLRenderCommandEncoder> render_ = nil;
    id<MTLBlitCommandEncoder> blit_ = nil;
    id<MTLComputeCommandEncoder> compute_ = nil;
    bool in_pass_ = false;
    u32 pass_width_ = 0;
    u32 pass_height_ = 0;

    // The bound state, applied at draw or dispatch time: one per kind of pipeline, as Vulkan's and Direct3D 12's bind
    // points, so a compute pipeline and its groups bound before a pass are still there after it (Phase 21 Day 312).
    // SetBindGroup and SetPushConstants go to the kind of the pipeline bound last, as on Vulkan.
    struct BindPoint
    {
        PipelineHandle pipeline;
        MetalPipelineInfo info;  // pipeline's, looked up when it is set
        bool pipeline_dirty = false;
        Array<BindGroupHandle, kMaxBindGroups> groups = {};
        u32 groups_dirty = 0;  // a bit per set
        Array<u8, kMaxPushConstantBytes> push = {};
        u32 push_size = 0;
        bool push_dirty = false;
    };
    BindPoint graphics_;
    BindPoint compute_bind_;
    bool compute_bound_ = false;  // the kind of the pipeline bound last
    BindPoint& Current() { return compute_bound_ ? compute_bind_ : graphics_; }
    id<MTLBuffer>
        index_buffer_ = nil;  // held: a WaitIdle in the frame could collect a destroyed buffer before the draw
    u64 index_offset_ = 0;
    IndexType index_type_ = IndexType::UInt16;
    MetalBinder binder_;
    Counts counts_;

    // Debug labels: Metal's groups belong to an encoder, so the open ones are closed at an encoder's end and opened
    // again in the next.
    static constexpr u32 kMaxLabels = 16;
    Array<Array<char, 64>, kMaxLabels> labels_ = {};
    u32 label_count_ = 0;
    u32 label_overflow_ = 0;  // pushes past kMaxLabels, not shown
    u32 labels_on_encoder_ = 0;
};

}  // namespace vse::rhi::metal
