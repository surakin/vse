#pragma once

#include <atomic>
#include <mutex>

#include "rhi/device.h"
#include "rhi/handle_pool.h"
#include "rhi/rhi.h"
#include "jobs/topology.h"
#include "rhi/vulkan/vk_command_list.h"
#include "rhi/vulkan/vk_device_select.h"
#include "rhi/vulkan/vk_functions.h"
#include "rhi/vulkan/vk_instance.h"
#include "rhi/vulkan/vk_memory.h"
#include "rhi/vulkan/vk_memory_backend.h"
#include "rhi/vulkan/vk_ring.h"

//! The Vulkan implementation of [[rhi::Device]].

namespace engine_placeholder::rhi::vulkan {

/// The Vulkan 1.3 device: one physical GPU, one logical device and one graphics+compute queue.
///
/// Created by [[rhi::CreateDevice]] (or [[rhi::vulkan::VulkanDevice::Create]]). All work, including uploads, runs
/// on the single graphics queue; a dedicated transfer family is recorded in the queue plan but unused in Phase 3.
///
/// Thread-safety: thread-safe for creation, destruction and mapping, as for [[rhi::Device]]; `Submit`, `BeginFrame`,
///   `EndFrame` and `WaitIdle` belong to one submission thread.
/// Ownership: the caller owns the device and releases it with [[rhi::DestroyDevice]]; it owns the Vulkan instance.
class VulkanDevice final : public Device {
public:
    /// Creates a Vulkan device.
    ///
    /// Param info: what to create; the backend field is ignored.
    /// Returns: the device, or null.
    /// Fails: as for [[rhi::CreateDevice]].
    /// Ownership: the caller owns the result.
    static VulkanDevice* Create(const DeviceCreateInfo& info);

    /// How many frames of command pools exist; a pool is reset only after the frame that used it has finished.
    static constexpr usize kFramePools = 3;
    /// How many threads can hold a command-pool slot at once; a thread releases its slot when it exits.
    static constexpr usize kThreadSlots = jobs::kMaxParticipants + 64;

    /// Waits for the GPU to go idle, then destroys every resource, the device and the instance.
    ~VulkanDevice() override;

    const char* BackendName() const override;
    const DeviceCaps& Caps() const override;
    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void DestroyBuffer(BufferHandle buffer) override;
    TextureHandle CreateTexture(const TextureDesc& desc) override;
    void DestroyTexture(TextureHandle texture) override;
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void DestroySampler(SamplerHandle sampler) override;
    ShaderHandle CreateShader(const ShaderDesc& desc) override;
    void DestroyShader(ShaderHandle shader) override;
    BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    void DestroyBindGroupLayout(BindGroupLayoutHandle layout) override;
    BindGroupHandle CreateBindGroup(const BindGroupDesc& desc) override;
    void DestroyBindGroup(BindGroupHandle group) override;
    PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override;
    void DestroyPipeline(PipelineHandle pipeline) override;
    void* MapBuffer(BufferHandle buffer) override;
    void UnmapBuffer(BufferHandle buffer) override;
    UploadTicket UploadBuffer(BufferHandle dst, u64 offset, const void* data, u64 size) override;
    bool ReadbackBuffer(BufferHandle src, u64 offset, void* dst, u64 size) override;
    CommandList* CreateCommandList() override;
    FenceValue Submit(CommandList* const* lists, usize count) override;
    bool WaitFence(FenceValue value, u64 timeout_ns) override;
    FenceValue CompletedFence() const override;
    void BeginFrame() override;
    void EndFrame() override;
    u64 FrameNumber() const override;
    void WaitIdle() override;
    bool DeviceLost() const override;

    /// Returns the name of the GPU in use, as reported by the driver.
    /// Ownership: the string lives as long as the device.
    const char* GpuName() const { return gpu_info_.name; }

    /// Returns the description of the GPU in use.
    const PhysicalDeviceInfo& GpuInfo() const { return gpu_info_; }

    /// Returns the queue families in use.
    const QueuePlan& Queues() const { return queues_; }

    /// Returns the instance this device was created from.
    const VulkanInstance& Instance() const { return instance_; }

    /// Returns the raw `VkDevice`. Ownership: borrowed; valid until the device is destroyed.
    VkDevice Handle() const { return device_; }

    /// Returns the device-level function table.
    const VkDeviceFns& Fns() const { return fns_; }

    /// Returns the physical device's memory properties.
    const VkPhysicalDeviceMemoryProperties& MemoryProperties() const { return memory_properties_; }

    /// Returns the physical device's properties and limits.
    const VkPhysicalDeviceProperties& Properties() const { return properties_; }

    /// Returns the GPU memory suballocator.
    /// Ownership: borrowed; valid for the life of the device.
    MemoryAllocator& Memory() { return *memory_; }

    /// Returns how many times `vkAllocateMemory` has succeeded, for tests of the suballocator.
    u64 MemoryObjectsAllocated() const { return memory_backend_->AllocateCalls(); }

    /// Reads the driver's budget and current usage of a memory heap, if `VK_EXT_memory_budget` is available.
    ///
    /// Param heap: the heap index.
    /// Param budget: receives the bytes this process may use; must not be null.
    /// Param usage: receives the bytes this process currently uses; must not be null.
    /// Returns: true if the extension is enabled and `heap` exists.
    /// Fails: returns false if `VK_EXT_memory_budget` is not enabled or `heap` is out of range.
    bool HeapBudget(u32 heap, u64* budget, u64* usage) const;

    /// Counts of the device's live Vulkan objects, for leak checks in tests.
    ///
    /// Thread-safety: thread-compatible plain data (a snapshot).
    struct ObjectCounts {
        /// Live buffers owned by the user (not internal staging).
        usize buffers = 0;
        /// Buffers destroyed but waiting for the GPU to finish with them.
        usize pending_destruction = 0;
        /// Raw `VkBuffer` objects alive, including internal staging.
        usize raw_buffers = 0;
    };

    /// Returns the current object counts.
    ObjectCounts Counts() const;

    /// Returns the highest frame number whose GPU work has finished.
    u64 CompletedFrame() const;

    /// Looks up a live buffer for command recording.
    ///
    /// Param buffer: the handle.
    /// Param vk_buffer: receives the `VkBuffer`; must not be null.
    /// Param desc: receives the creation descriptor; must not be null.
    /// Returns: true if the handle is live.
    /// Fails: returns false for an invalid or stale handle.
    bool LookupBuffer(BufferHandle buffer, VkBuffer* vk_buffer, BufferDesc* desc) const;

    /// Notes the result of a Vulkan call made while recording: a lost device sets the sticky lost state, and any
    /// other failure is logged.
    ///
    /// Param result: the `VkResult` to examine.
    void ReportResult(VkResult result);

    /// Makes the next `Submit` fail with `result` instead of reaching the driver, once; for testing the
    /// device-lost and error paths. Pass `VK_SUCCESS` to clear a pending fault.
    ///
    /// Param result: the result the next submission reports, such as `VK_ERROR_DEVICE_LOST`.
    void InjectSubmitFailure(VkResult result) { inject_submit_result_.store(static_cast<i32>(result), std::memory_order_relaxed); }

    /// Returns how many command pools exist, for tests of the per-thread pool policy.
    usize CommandPoolCount() const;

private:
    VulkanDevice() = default;
    bool Initialize(const DeviceCreateInfo& info);
    void FillCaps();
    // Logs that `what` is not implemented yet in this build of the backend (resource kinds arrive on later days).
    static void NotImplemented(const char* what);

    // A VkBuffer with its memory; used for user buffers and for internal staging.
    struct RawBuffer {
        VkBuffer buffer = nullptr;
        MemoryAllocation memory;
        u64 size = 0;
    };
    struct BufferRes {
        RawBuffer raw;
        BufferDesc desc;
        bool mapped = false;
    };
    struct CommandBufferSlot {
        VkCommandBuffer buffer;
        u64 fence; // the timeline value that frees it; 0 when free
    };

    bool CreateRawBuffer(u64 size, VkBufferUsageFlags usage, MemoryLocation location, const char* what, RawBuffer* out);
    void DestroyRawBuffer(RawBuffer* raw);
    void InitializeTimeline();
    bool InitializeUpload();
    void ShutdownUpload();
    // Waits for `value` on the timeline, marking the device lost on VK_ERROR_DEVICE_LOST.
    bool WaitTimeline(u64 value, u64 timeout_ns);
    u64 CompletedTimeline() const;
    // Records `fill` into a one-time command buffer and submits it, signalling a new timeline value. Holds submit_mutex_.
    template <typename Fill>
    u64 SubmitInternalLocked(Fill&& fill);
    VkCommandBuffer AcquireUploadCommandBufferLocked();
    void NoteDeviceLost(VkResult result);
    void SetDebugName(u64 handle, VkObjectType type, const char* name);
    // Releases buffers whose frames have completed (or all of them when `all`).
    void CollectRetired(bool all);
    void UpdateCompletedFrames();
    // Command pools: one per (frame slot, thread slot), made by the thread that owns the slot.
    struct ThreadPool {
        VkCommandPool pool = nullptr;
        detail::GrowArray<VulkanCommandList*> lists;
        usize used = 0;
    };
    ThreadPool* PoolForCurrentThread(usize frame_slot);
    void ResetFramePools(usize frame_slot);
    void ShutdownCommands();
    void SyncRange(const MemoryAllocation& allocation, u64 rel_offset, u64 length, bool flush);
    void FlushRange(const MemoryAllocation& allocation, u64 rel_offset, u64 length);
    void InvalidateRange(const MemoryAllocation& allocation, u64 rel_offset, u64 length);

    VulkanInstance instance_;
    VkPhysicalDevice physical_ = nullptr;
    VkDevice device_ = nullptr;
    VkDeviceFns fns_;
    VkQueue queue_ = nullptr;
    PhysicalDeviceInfo gpu_info_;
    QueuePlan queues_;
    VkPhysicalDeviceProperties properties_{};
    VkPhysicalDeviceMemoryProperties memory_properties_{};
    DeviceCaps caps_;
    bool anisotropy_enabled_ = false;
    bool memory_budget_enabled_ = false;
    VulkanMemoryBackend* memory_backend_ = nullptr;
    MemoryAllocator* memory_ = nullptr;
    std::atomic<bool> device_lost_{false};
    std::atomic<u64> frame_number_{0};

    // Resources and deferred destruction (guarded by resource_mutex_).
    mutable std::mutex resource_mutex_;
    HandlePool<BufferRes, BufferTag> buffers_;
    std::atomic<bool> frame_open_{false};
    u64 ended_frames_ = 0;
    u64 completed_frames_ = 0;
    static constexpr u64 kTrackedFrames = 64;
    u64 frame_fences_[kTrackedFrames] = {};
    std::atomic<usize> raw_buffer_count_{0};

    // Timeline and submission (guarded by submit_mutex_).
    std::mutex submit_mutex_;
    VkSemaphore timeline_ = nullptr;
    u64 last_submit_value_ = 0;
    VkCommandPool upload_pool_ = nullptr;
    detail::GrowArray<CommandBufferSlot> upload_buffers_;
    RingAllocator* staging_ring_ = nullptr;
    RawBuffer staging_;
    detail::GrowArray<VkCommandBufferSubmitInfo> submit_scratch_;
    std::atomic<i32> inject_submit_result_{0};

    // Per-thread command pools, a ring of kFramePools frames deep, indexed by the thread's slot.
    std::atomic<ThreadPool*> thread_pools_[kFramePools][kThreadSlots] = {};
};

} // namespace engine_placeholder::rhi::vulkan
