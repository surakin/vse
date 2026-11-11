#pragma once

#include "containers/vector.h"
#include "rhi/command_list.h"
#include "rhi/dx12/com_ref.h"
#include "rhi/dx12/d3d12_api.h"
#include "rhi/dx12/dx12_device.h"
#include "types.h"

//! The Direct3D 12 command list (Phase 13 Day 168).

namespace engine_placeholder::rhi::dx12
{

/// A [[rhi::CommandList]] recording into one `ID3D12GraphicsCommandList` with its own command allocator.
///
/// The device keeps the lists of each frame slot and hands them out again once that slot's frame has finished, so an
/// allocator is reset only after the GPU is done with it. States: buffers are tracked within the list from `COMMON`
/// (D3D12 returns every buffer there after each `ExecuteCommandLists`, and the device executes each list on its own),
/// promoted implicitly on use and transitioned explicitly from the tracked state; textures keep their state between
/// lists, so a list records the state its first barrier on each texture needs, and the device transitions the texture
/// into it before the list runs (a prologue list built at submit from the states it recorded).
///
/// - Thread-safety: not-thread-safe: one thread records a list; different lists record on different threads at once.
/// - Ownership: owned by its device; the application borrows it from `CreateCommandList` until the frame ends.
class Dx12CommandList final : public CommandList
{
public:
    /// Where a list is in its life.
    enum class Phase : u8
    {
        Initial,     ///< Handed out, not begun.
        Recording,   ///< Between `Begin` and `End`.
        Ended,       ///< Ended, not submitted.
        Submitting,  ///< Inside `Submit`.
        Submitted,   ///< Submitted (or dropped by a failed submit).
    };

    /// What a list needs of one texture when it starts, and leaves it in.
    ///
    /// - Thread-safety: thread-compatible plain data.
    struct TextureUse
    {
        /// The texture.
        TextureHandle texture;
        /// Its resource.
        ID3D12Resource* resource = nullptr;
        /// The state it must be in when the list starts.
        D3D12_RESOURCE_STATES initial = D3D12_RESOURCE_STATE_COMMON;
        /// The state the list leaves it in.
        D3D12_RESOURCE_STATES current = D3D12_RESOURCE_STATE_COMMON;
    };

    /// Wraps an allocator and a closed list.
    ///
    /// - Param device: the owner.
    /// - Param allocator: the list's own allocator.
    /// - Param list: a closed list.
    Dx12CommandList(Dx12Device* device, ComRef<ID3D12CommandAllocator> allocator,
                    ComRef<ID3D12GraphicsCommandList> list);
    ~Dx12CommandList() override = default;
    Dx12CommandList(const Dx12CommandList&) = delete;
    Dx12CommandList& operator=(const Dx12CommandList&) = delete;

    /// Makes the list ready for a new frame; its allocator's last use must have finished on the GPU.
    ///
    /// - Param frame: the frame it is handed out in.
    /// - Returns: true on success.
    /// - Fails: false if the allocator cannot be reset (the device is lost).
    bool Recycle(u64 frame);

    /// Returns the phase.
    Phase GetPhase() const { return phase_; }
    /// Sets the phase (the device, at submit).
    ///
    /// - Param phase: the new phase.
    void SetPhase(Phase phase) { phase_ = phase; }
    /// Returns the frame the list was handed out in.
    u64 Frame() const { return frame_; }
    /// Returns the owning device.
    const Dx12Device* Owner() const { return device_; }
    /// Returns whether a D3D12 call failed while recording (the list is then not submitted).
    bool Failed() const { return failed_; }
    /// Returns the D3D12 list. Ownership: borrowed.
    ID3D12GraphicsCommandList* List() const { return list_.Get(); }
    /// Returns the textures the list transitions, with the state each must start in. Ownership: borrowed until the next
    /// `Recycle`.
    const Vector<TextureUse>& TextureUses() const { return textures_; }

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

private:
    struct BufferState
    {
        ID3D12Resource* resource = nullptr;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        bool promoted = false;  // reached by implicit promotion: more read states may be added the same way
    };
    struct VertexSlot
    {
        u64 address = 0;
        u64 size = 0;
    };

    bool Recording(const char* what) const;
    void Fail(const char* what, HRESULT hr);
    BufferState* FindBuffer(ID3D12Resource* resource);
    void UseBuffer(ID3D12Resource* resource, D3D12_RESOURCE_STATES state);
    TextureUse* FindTexture(ID3D12Resource* resource);
    void CopyLevel(BufferHandle buffer, TextureHandle texture, const BufferTextureCopy& region, bool to_texture);
    void ApplyPipeline();
    bool FlushBindGroups(const char* what);
    void FlushVertexBuffers();
    void FlushIndexedState(bool indexed);

    Dx12Device* device_;
    ComRef<ID3D12CommandAllocator> allocator_;
    ComRef<ID3D12GraphicsCommandList> list_;
    Phase phase_ = Phase::Submitted;
    u64 frame_ = 0;
    bool failed_ = false;
    u32 label_depth_ = 0;

    Vector<BufferState> buffers_;
    Vector<TextureUse> textures_;

    bool in_rendering_ = false;
    u32 pass_color_count_ = 0;
    Format pass_color_formats_[kMaxColorAttachments] = {};
    Format pass_depth_format_ = Format::Undefined;
    ID3D12Resource* pass_resources_[kMaxColorAttachments + 1] = {};
    bool pass_discard_[kMaxColorAttachments + 1] = {};
    u32 pass_resource_count_ = 0;

    bool pipeline_bound_ = false;  // a pipeline was set (graphics inside the pass, compute outside)
    bool
        pipeline_applied_ = false;  // its root signature and state are set on the D3D12 list (GenerateMips unsets them)
    Dx12Device::PipelineInfo pipeline_;
    ID3D12PipelineState* applied_state_ = nullptr;
    u8 push_shadow_[kMaxPushConstantBytes] = {};
    u32 push_written_ = 0;  // bytes of push_shadow_ set since the pipeline was bound

    Dx12Device::BindGroupInfo groups_[kMaxBindGroups];
    bool group_set_[kMaxBindGroups] = {};
    u32 groups_dirty_ = 0;

    VertexSlot vertex_slots_[kMaxVertexBindings];
    u32 vertex_slot_count_ = 0;
    bool vertex_dirty_ = false;
    bool index_bound_ = false;
    IndexType index_type_ = IndexType::UInt32;
};

}  // namespace engine_placeholder::rhi::dx12
