#pragma once

#include <atomic>
#include <mutex>

#include "rhi/device.h"
#include "rhi/handle_pool.h"
#include "rhi/rhi.h"
#include "rhi/spirv.h"
#include "jobs/topology.h"
#include "rhi/vulkan/vk_command_list.h"
#include "rhi/vulkan/vk_device_select.h"
#include "rhi/vulkan/vk_functions.h"
#include "rhi/vulkan/vk_instance.h"
#include "rhi/vulkan/vk_memory.h"
#include "rhi/vulkan/vk_memory_backend.h"
#include "rhi/vulkan/vk_ring.h"
#include "os/window.h"

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
    Swapchain* CreateSwapchain(const SwapchainDesc& desc) override;
    void DestroySwapchain(Swapchain* swapchain) override;
    bool FormatSupport(Format format, TextureUsage usage) const override;
    bool CanGenerateMips(Format format) const override;
    UploadTicket UploadTexture(TextureHandle dst, u32 mip, u32 layer, const void* data, u32 row_pitch) override;
    bool ReadbackTexture(TextureHandle src, u32 mip, u32 layer, void* dst, u64 dst_size, ResourceState state) override;
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
        /// Live textures owned by the user.
        usize textures = 0;
        /// Live samplers (each distinct sampler counted once).
        usize samplers = 0;
        /// Textures and samplers destroyed but waiting for the GPU to finish with them.
        usize pending_image_destruction = 0;
        /// Raw `VkImage` objects alive.
        usize raw_images = 0;
        /// Live shader modules owned by the user.
        usize shaders = 0;
    };

    /// Returns the current object counts.
    ObjectCounts Counts() const;

    /// Returns the highest frame number whose GPU work has finished.
    u64 CompletedFrame() const;

    /// Returns how many frames the CPU may be ahead of the GPU (1 to 3).
    u32 FramesInFlight() const { return frames_in_flight_; }

    /// Objects that were still alive, or were never released, when devices were destroyed.
    ///
    /// Thread-safety: thread-compatible plain data (a snapshot).
    struct LeakCounts {
        /// Buffers, textures, samplers and swapchains the application did not destroy before the device (they are freed,
        /// with a warning).
        u64 user_objects = 0;
        /// Vulkan objects the backend itself failed to release; any is a bug in the backend.
        u64 internal_objects = 0;
    };

    /// Returns the process-wide totals of objects found leaked by destroyed devices since the last reset.
    /// Thread-safety: thread-safe.
    static LeakCounts GetLeakCounts();

    /// Sets both leak totals to zero. Thread-safe.
    static void ResetLeakCounts();

    /// Looks up a live buffer for command recording.
    ///
    /// Param buffer: the handle.
    /// Param vk_buffer: receives the `VkBuffer`; must not be null.
    /// Param desc: receives the creation descriptor; must not be null.
    /// Returns: true if the handle is live.
    /// Fails: returns false for an invalid or stale handle.
    bool LookupBuffer(BufferHandle buffer, VkBuffer* vk_buffer, BufferDesc* desc) const;

    /// What command recording needs to know about a texture.
    ///
    /// Thread-safety: thread-compatible plain data (a snapshot).
    struct TextureInfo {
        /// The image.
        VkImage image = nullptr;
        /// The view over all mips and layers, for sampling.
        VkImageView view = nullptr;
        /// The view of mip 0, layer 0, for rendering; null unless the texture has an attachment usage.
        VkImageView attachment_view = nullptr;
        /// The creation descriptor (without the debug name).
        TextureDesc desc;
        /// All aspects of the format.
        VkImageAspectFlags aspects = 0;
    };

    /// Looks up a live texture for command recording.
    ///
    /// Param texture: the handle.
    /// Param info: receives the snapshot; must not be null.
    /// Returns: true if the handle is live.
    /// Fails: returns false for an invalid or stale handle.
    bool LookupTexture(TextureHandle texture, TextureInfo* info) const;

    /// A snapshot of a live shader module, for pipeline creation.
    ///
    /// Thread-safety: thread-compatible plain data (a snapshot).
    /// Ownership: `module` and `entry` are borrowed from the device and stay valid until the shader is destroyed and
    ///   its frame has completed.
    struct ShaderInfo {
        /// The `VkShaderModule`.
        VkShaderModule module = nullptr;
        /// The stage the module implements.
        ShaderStage stage = ShaderStage::None;
        /// The entry point name, nul-terminated.
        const char* entry = nullptr;
    };

    /// Looks up a shader by handle.
    ///
    /// Param shader: the handle.
    /// Param info: receives the snapshot when the handle is live.
    /// Returns: true if the handle is live.
    /// Fails: returns false for an invalid or stale handle.
    /// Thread-safety: thread-safe.
    bool LookupShader(ShaderHandle shader, ShaderInfo* info) const;

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
    struct TextureRes {
        VkImage image = nullptr;
        VkImageView view = nullptr;
        VkImageView attachment_view = nullptr;
        MemoryAllocation memory;
        bool external = false; // a swapchain image: the swapchain owns the VkImage
        TextureDesc desc;
        ResourceState state = ResourceState::Undefined; // as left by UploadTexture; barriers in lists are the caller's
    };
    struct SamplerRes {
        VkSampler sampler = nullptr;
        SamplerDesc desc; // normalized: anisotropy clamped, name dropped
        u32 refs = 0;
    };
    struct ShaderRes {
        VkShaderModule module = nullptr;
        ShaderStage stage = ShaderStage::None;
        char entry[kMaxShaderEntryNameLength + 1] = {};
    };
    struct CommandBufferSlot {
        VkCommandBuffer buffer;
        u64 fence; // the timeline value that frees it; 0 when free
    };

    void InitializeFormats();
    void DestroyTextureRes(TextureRes* t);
    // Submits `fill` after staging chunks; shared by the texture upload and readback paths.
    bool StageChunkLocked(u64 alloc_bytes, u64 align, u64* ring_offset);
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
    u64 SubmitInternalLocked(Fill&& fill) {
        return SubmitRawLocked([](void* context, VkCommandBuffer cb) { (*static_cast<Fill*>(context))(cb); }, &fill, nullptr, 0, nullptr, 0);
    }
    // `waits` and `extra_signals` are binary semaphores for presentation; the timeline is always signalled too.
    u64 SubmitRawLocked(void (*fill)(void*, VkCommandBuffer), void* context, const VkSemaphoreSubmitInfo* waits, u32 wait_count,
                        const VkSemaphoreSubmitInfo* extra_signals, u32 extra_signal_count);
    bool CreateSurface(const os::NativeWindowInfo& native);
    friend class VulkanSwapchain;
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

    const os::Window* window_ = nullptr;
    VkSurfaceKHR surface_ = nullptr;
    class VulkanSwapchain* swapchain_ = nullptr;
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
    HandlePool<TextureRes, TextureTag> textures_;
    HandlePool<SamplerRes, SamplerTag> samplers_;
    HandlePool<ShaderRes, ShaderTag> shaders_;
    VkFormatFeatureFlags format_features_[static_cast<usize>(Format::Count)] = {};
    std::atomic<usize> raw_image_count_{0};
    u32 frames_in_flight_ = 2;
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
