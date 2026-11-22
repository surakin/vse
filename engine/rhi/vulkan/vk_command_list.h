#pragma once

#include "rhi/command_list.h"
#include "rhi/vulkan/vk_functions.h"

//! The Vulkan implementation of [[rhi::CommandList]].

namespace vse::rhi::vulkan
{

class VulkanDevice;

/// Records into one primary `VkCommandBuffer` taken from the creating thread's command pool.
///
/// Copies, barriers, rendering passes, graphics pipelines and draws are recorded today; the commands that need bind
/// groups, push constants or compute pipelines log an error and record nothing until those exist.
///
/// - Thread-safety: thread-compatible; record from one thread, different lists on different threads at once.
/// - Ownership: owned by the [[rhi::vulkan::VulkanDevice]] that made it; use it only within the frame it was created
/// in.
class VulkanCommandList final : public CommandList
{
public:
    /// Where a list is in its life.
    enum class Phase : u8
    {
        Initial,     ///< Created or recycled; `Begin` has not been called.
        Recording,   ///< Between `Begin` and `End`.
        Ended,       ///< `End` was called; ready to submit.
        Submitting,  ///< Being checked by `Submit` (detects a list passed twice).
        Submitted,   ///< Handed to the GPU.
    };

    /// Creates a list that records into `buffer`.
    ///
    /// - Param device: the owning device; must outlive the list.
    /// - Param buffer: a primary command buffer from a pool owned by the device.
    VulkanCommandList(VulkanDevice* device, VkCommandBuffer buffer);

    /// Does nothing; the command buffer belongs to its pool.
    ~VulkanCommandList() override = default;

    /// Makes the list ready for a new recording, tagged with the frame it belongs to; its pool must have been reset.
    /// - Param frame: the frame number current when the list is handed out.
    void Recycle(u64 frame);

    /// Returns whether a Vulkan call on this list failed, so the list must not be submitted.
    bool Failed() const { return failed_; }
    /// Returns the list's phase.
    Phase GetPhase() const { return phase_; }
    /// Sets the list's phase; used by `Submit`.
    /// - Param phase: the new phase.
    void SetPhase(Phase phase) { phase_ = phase; }
    /// Returns the frame the list was handed out in.
    u64 Frame() const { return frame_; }
    /// Returns the device that made the list.
    const VulkanDevice* Owner() const { return device_; }
    /// Returns the underlying command buffer. Ownership: borrowed.
    VkCommandBuffer Buffer() const { return buffer_; }

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
    // Aborts unless recording; returns false if `Begin` failed, in which case the command is skipped.
    bool Recording(const char* what) const;
    void Unsupported(const char* what) const;
    void CopyLevel(BufferHandle buffer, TextureHandle texture, const BufferTextureCopy& region, bool to_texture);

    VulkanDevice* device_;
    VkCommandBuffer buffer_;
    Phase phase_ = Phase::Initial;
    u64 frame_ = 0;
    u32 label_depth_ = 0;
    bool failed_ = false;
    bool in_rendering_ = false;
    // State of the pass and the bound pipeline, for the checks the draw commands make.
    u32 pass_color_count_ = 0;
    Format pass_color_formats_[kMaxColorAttachments] = {};
    Format pass_depth_format_ = Format::Undefined;
    bool graphics_bound_ = false;
    bool compute_bound_ = false;
    bool index_bound_ = false;
    VkPipelineLayout pipeline_layout_ = nullptr;
    u32 push_bytes_ = 0;
    ShaderStage push_stages_ = ShaderStage::None;
    u32 pipeline_layout_count_ = 0;
    u64 pipeline_layout_ids_[kMaxBindGroups] = {};
    // Bind groups set on the list; they are bound with the pipeline's layout at the next draw, because Vulkan needs the
    // layout of the pipeline that uses them.
    VkDescriptorSet group_sets_[kMaxBindGroups] = {};
    u64 group_layout_ids_[kMaxBindGroups] = {};
    u32 groups_dirty_ = 0;
    void FlushBindGroups(const char* what);
};

}  // namespace vse::rhi::vulkan
