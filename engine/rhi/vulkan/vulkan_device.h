#pragma once

#include "sync/atomic.h"
#include "sync/mutex.h"

#include "containers/vector.h"
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
#include "rhi/vulkan/vk_pipeline_policy.h"
#include "rhi/staging_ring.h"
#include "platform/window.h"

//! The Vulkan implementation of [[rhi::Device]].

namespace engine_placeholder::rhi::vulkan
{

class VulkanDevice;

/// Owning pointer to a Vulkan device; destroying it waits for the GPU first (see [[rhi::DeviceDeleter]]).
using VulkanDevicePtr = UniquePtr<VulkanDevice, DeviceDeleter>;

/// The Vulkan 1.3 device: one physical GPU, one logical device and one graphics+compute queue.
///
/// Created by [[rhi::CreateDevice]] (or [[rhi::vulkan::VulkanDevice::Create]]). All work, including uploads, runs
/// on the single graphics queue; a dedicated transfer family is recorded in the queue plan but unused in Phase 4.
///
/// - Thread-safety: thread-safe for creation, destruction and mapping, as for [[rhi::Device]]; `Submit`, `BeginFrame`,
///   `EndFrame` and `WaitIdle` belong to one submission thread.
/// - Ownership: the caller owns the device through a [[rhi::vulkan::VulkanDevicePtr]]; it owns the Vulkan instance.
class VulkanDevice final : public Device
{
public:
    /// Constructs an uninitialized device; use [[rhi::vulkan::VulkanDevice::Create]] or [[rhi::CreateDevice]].
    VulkanDevice() = default;

    /// Creates a Vulkan device.
    ///
    /// - Param info: what to create; the backend field is ignored.
    /// - Returns: the device, or an empty pointer.
    /// - Fails: as for [[rhi::CreateDevice]].
    /// - Ownership: the caller owns the result through the returned pointer.
    static VulkanDevicePtr Create(const DeviceCreateInfo& info);

    /// Lists the physical devices, as [[rhi::EnumerateAdapters]] does for this backend.
    ///
    /// - Param info: validation settings and the window to test presentability against.
    /// - Param out: receives the list.
    /// - Returns: false if the loader or the instance could not be created.
    static bool EnumerateAdapters(const DeviceCreateInfo& info, AdapterList* out);

    /// How many frames of command pools exist; a pool is reset only after the frame that used it has finished.
    static constexpr usize kFramePools = 3;
    /// How many Vulkan descriptor types the descriptor pools size (uniform buffer, storage buffer, sampled image,
    /// sampler, storage image); the RHI's read-only storage buffer counts as a storage buffer.
    static constexpr u32 kDescriptorKinds = 5;
    /// How many threads can hold a command-pool slot at once; a thread releases its slot when it exits.
    static constexpr usize kThreadSlots = jobs::kMaxParticipants + 64;

    /// Waits for the GPU to go idle, then destroys every resource, the device and the instance.
    ~VulkanDevice() override;

    const char* BackendName() const override;
    const AdapterInfo& Adapter() const override { return adapter_; }
    bool ListAdapters(AdapterList* out) const override
    {
        *out = adapters_;
        return true;
    }
    u32 Epoch() const override { return epoch_; }
    ResourceCounts Resources() const override;
    void InjectDeviceLoss() override { InjectLoss("request", 0); }
    bool RecreateSurface(const os::Window* window) override;
    bool InFrame() const override { return frame_open_.Load(MemoryOrder::Acquire); }
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
    void FlushMappedBuffer(BufferHandle buffer, u64 offset, u64 size) override;
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
    /// - Ownership: the string lives as long as the device.
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
    /// - Ownership: borrowed; valid for the life of the device.
    MemoryAllocator& Memory() { return *memory_; }

    /// Returns how many times `vkAllocateMemory` has succeeded, for tests of the suballocator.
    u64 MemoryObjectsAllocated() const { return memory_backend_->AllocateCalls(); }

    /// Reads the driver's budget and current usage of a memory heap, if `VK_EXT_memory_budget` is available.
    ///
    /// - Param heap: the heap index.
    /// - Param budget: receives the bytes this process may use; must not be null.
    /// - Param usage: receives the bytes this process currently uses; must not be null.
    /// - Returns: true if the extension is enabled and `heap` exists.
    /// - Fails: returns false if `VK_EXT_memory_budget` is not enabled or `heap` is out of range.
    bool HeapBudget(u32 heap, u64* budget, u64* usage) const;

    /// Counts of the device's live Vulkan objects, for leak checks in tests.
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    struct ObjectCounts
    {
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
        /// Live bind group layouts owned by the user.
        usize bind_group_layouts = 0;
        /// Live pipelines owned by the user.
        usize pipelines = 0;
        /// Distinct `VkDescriptorSetLayout` objects alive; equal layouts share one.
        usize descriptor_set_layouts = 0;
        /// Distinct `VkPipelineLayout` objects alive; equal layouts share one.
        usize pipeline_layouts = 0;
        /// Live bind groups, persistent and transient.
        usize bind_groups = 0;
        /// Descriptor pools alive (persistent and per-frame).
        usize descriptor_pools = 0;
    };

    /// Returns the current object counts.
    ObjectCounts Counts() const;

    /// Returns the highest frame number whose GPU work has finished.
    u64 CompletedFrame() const;

    /// Returns how many frames the CPU may be ahead of the GPU (1 to 3).
    u32 FramesInFlight() const { return frames_in_flight_; }

    /// Objects that were still alive, or were never released, when devices were destroyed.
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    struct LeakCounts
    {
        /// Buffers, textures, samplers and swapchains the application did not destroy before the device (they are
        /// freed, with a warning).
        u64 user_objects = 0;
        /// Vulkan objects the backend itself failed to release; any is a bug in the backend.
        u64 internal_objects = 0;
    };

    /// Returns the process-wide totals of objects found leaked by destroyed devices since the last reset.
    /// - Thread-safety: thread-safe.
    static LeakCounts GetLeakCounts();

    /// Sets both leak totals to zero. Thread-safe.
    static void ResetLeakCounts();

    /// Looks up a live buffer for command recording.
    ///
    /// - Param buffer: the handle.
    /// - Param vk_buffer: receives the `VkBuffer`; must not be null.
    /// - Param desc: receives the creation descriptor; must not be null.
    /// - Returns: true if the handle is live.
    /// - Fails: returns false for an invalid or stale handle.
    bool LookupBuffer(BufferHandle buffer, VkBuffer* vk_buffer, BufferDesc* desc) const;

    /// What command recording needs to know about a texture.
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    struct TextureInfo
    {
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
    /// - Param texture: the handle.
    /// - Param info: receives the snapshot; must not be null.
    /// - Returns: true if the handle is live.
    /// - Fails: returns false for an invalid or stale handle.
    bool LookupTexture(TextureHandle texture, TextureInfo* info) const;

    /// A snapshot of a live shader module, for pipeline creation.
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    /// - Ownership: `module` and `entry` are borrowed from the device and stay valid until the shader is destroyed and
    ///   its frame has completed.
    struct ShaderInfo
    {
        /// The `VkShaderModule`.
        VkShaderModule module = nullptr;
        /// The stage the module implements.
        ShaderStage stage = ShaderStage::None;
        /// The entry point name, nul-terminated.
        const char* entry = nullptr;
    };

    /// Looks up a sampler by handle.
    ///
    /// - Param sampler: the handle.
    /// - Param vk_sampler: receives the `VkSampler` when the handle is live.
    /// - Returns: true if the handle is live.
    /// - Fails: returns false for an invalid or stale handle.
    /// - Thread-safety: thread-safe.
    bool LookupSampler(SamplerHandle sampler, VkSampler* vk_sampler) const;

    /// Looks up a shader by handle.
    ///
    /// - Param shader: the handle.
    /// - Param info: receives the snapshot when the handle is live.
    /// - Returns: true if the handle is live.
    /// - Fails: returns false for an invalid or stale handle.
    /// - Thread-safety: thread-safe.
    bool LookupShader(ShaderHandle shader, ShaderInfo* info) const;

    /// A snapshot of a live pipeline, for the command list.
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    /// - Ownership: the Vulkan handles are borrowed; the pipeline layout lives as long as the device.
    struct PipelineInfo
    {
        /// The pipeline.
        VkPipeline pipeline = nullptr;
        /// Its layout.
        VkPipelineLayout layout = nullptr;
        /// True for a compute pipeline.
        bool compute = false;
        /// Number of bind group layouts the pipeline declares.
        u32 layout_count = 0;
        /// Identity of each declared bind group layout; equal ids mean equal layouts.
        u64 layout_ids[kMaxBindGroups] = {};
        /// Size of the push-constant range in bytes.
        u32 push_bytes = 0;
        /// Stages that see the push constants.
        ShaderStage push_stages = ShaderStage::None;
        /// Number of color attachments the pipeline writes.
        u32 color_count = 0;
        /// Their formats.
        Format color_formats[kMaxColorAttachments] = {};
        /// The depth attachment format, or `Undefined`.
        Format depth_format = Format::Undefined;
    };

    /// A snapshot of a live bind group, for the command list.
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    /// - Ownership: `set` is borrowed from the device; it stays valid until the group is destroyed (persistent) or its
    /// frame
    ///   has finished (transient).
    struct BindGroupInfo
    {
        /// The descriptor set.
        VkDescriptorSet set = nullptr;
        /// Identity of the group's bind group layout; equal ids mean equal layouts.
        u64 layout_id = 0;
        /// True for a transient group.
        bool transient = false;
        /// For a transient group, the frame it was created in.
        u64 frame = 0;
        /// False if a resource the group was built from has since been destroyed.
        bool resources_live = true;
    };

    /// Looks up a bind group by handle and checks that the resources it refers to are still alive.
    ///
    /// - Param group: the handle.
    /// - Param info: receives the snapshot when the handle is live.
    /// - Returns: true if the handle is live.
    /// - Fails: returns false for an invalid or stale handle.
    /// - Thread-safety: thread-safe.
    bool LookupBindGroup(BindGroupHandle group, BindGroupInfo* info) const;

    /// Looks up a pipeline by handle.
    ///
    /// - Param pipeline: the handle.
    /// - Param info: receives the snapshot when the handle is live.
    /// - Returns: true if the handle is live.
    /// - Fails: returns false for an invalid or stale handle.
    /// - Thread-safety: thread-safe.
    bool LookupPipeline(PipelineHandle pipeline, PipelineInfo* info) const;

    /// What the on-disk pipeline cache did for this device.
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    struct PipelineCacheStats
    {
        /// Whether a cache is in use (it is not when disabled or when no directory could be chosen).
        bool enabled = false;
        /// Bytes of an existing file given to the driver at start; 0 if there was none or it was ignored.
        u64 loaded_bytes = 0;
        /// True if a file existed but was ignored (damaged, or for another device or driver).
        bool rejected = false;
        /// Bytes written by the last save; 0 until the device is destroyed.
        u64 saved_bytes = 0;
    };

    /// Returns what the pipeline cache did so far.
    PipelineCacheStats PipelineCache() const;

    /// Writes the pipeline cache to disk now, as the destructor does, so a long session or one that may crash does not
    /// lose what it compiled.
    ///
    /// - Returns: true if a file was written.
    /// - Fails: returns false if the cache is disabled, empty, or the file cannot be written (logged).
    /// - Thread-safety: thread-safe; concurrent calls in one process write one after the other.
    bool SavePipelineCache();

    /// Returns the pipeline cache file path, or an empty string if no cache is in use.
    const char* PipelineCachePath() const { return cache_path_; }

    /// Notes the result of a Vulkan call made while recording: a lost device sets the sticky lost state, and any
    /// other failure is logged.
    ///
    /// - Param result: the `VkResult` to examine.
    void ReportResult(VkResult result);

    /// Makes the next `Submit` fail with `result` instead of reaching the driver, once; for testing the
    /// device-lost and error paths. Pass `VK_SUCCESS` to clear a pending fault.
    ///
    /// - Param result: the result the next submission reports, such as `VK_ERROR_DEVICE_LOST`.
    void InjectSubmitFailure(VkResult result)
    {
        inject_submit_result_.Store(static_cast<i32>(result), MemoryOrder::Relaxed);
    }

    /// Returns how many command pools exist, for tests of the per-thread pool policy.
    usize CommandPoolCount() const;

private:
    bool Initialize(const DeviceCreateInfo& info);
    // Loads the loader, creates the instance and the surface, and describes every physical device.
    bool Discover(const DeviceCreateInfo& info, VkPhysicalDevice* physicals, PhysicalDeviceInfo* infos, u32* count);
    void FillCaps();
    // Logs that `what` is not implemented yet in this build of the backend (resource kinds arrive on later days).
    static void NotImplemented(const char* what);

    // A VkBuffer with its memory; used for user buffers and for internal staging.
    struct RawBuffer
    {
        VkBuffer buffer = nullptr;
        MemoryAllocation memory;
        u64 size = 0;
    };
    struct BufferRes
    {
        RawBuffer raw;
        BufferDesc desc;
        bool mapped = false;
    };
    struct TextureRes
    {
        VkImage image = nullptr;
        VkImageView view = nullptr;
        VkImageView attachment_view = nullptr;
        MemoryAllocation memory;
        bool external = false;  // a swapchain image: the swapchain owns the VkImage
        TextureDesc desc;
        ResourceState state = ResourceState::Undefined;  // as left by UploadTexture; barriers in lists are the caller's
    };
    struct SamplerRes
    {
        VkSampler sampler = nullptr;
        SamplerDesc desc;  // normalized: anisotropy clamped, name dropped
        u32 refs = 0;
    };
    struct ShaderRes
    {
        VkShaderModule module = nullptr;
        ShaderStage stage = ShaderStage::None;
        char entry[kMaxShaderEntryNameLength + 1] = {};
    };
    // One cached VkDescriptorSetLayout, shared by every bind group layout with the same bindings.
    struct SetLayoutEntry
    {
        VkDescriptorSetLayout layout = nullptr;
        u64 id = 0;  // never reused, so a pipeline layout key can name it safely
        BindingDesc bindings[kMaxBindingsPerLayout];
        u32 binding_count = 0;
        u32 refs = 0;
    };
    struct BindGroupLayoutRes
    {
        u32 set_slot = 0;
        u64 id = 0;
        BindingDesc bindings[kMaxBindingsPerLayout];
        u32 binding_count = 0;
    };
    struct PipelineLayoutEntry
    {
        VkPipelineLayout layout = nullptr;
        u64 set_ids[kMaxBindGroups] = {};
        u32 set_count = 0;
        u32 push_bytes = 0;
        ShaderStage push_stages = ShaderStage::None;
        u32 refs = 0;  // pipelines using it; it is destroyed when the last one has been collected
    };
    struct PipelineRes
    {
        VkPipeline pipeline = nullptr;
        VkPipelineLayout layout = nullptr;
        u32 layout_slot = 0;
        bool compute = false;
        u32 layout_count = 0;
        u64 layout_ids[kMaxBindGroups] = {};
        u32 push_bytes = 0;
        ShaderStage push_stages = ShaderStage::None;
        u32 color_count = 0;
        Format color_formats[kMaxColorAttachments] = {};
        Format depth_format = Format::Undefined;
    };
    struct GroupRef
    {
        BindingType type = BindingType::UniformBuffer;
        BufferHandle buffer;
        TextureHandle texture;
        SamplerHandle sampler;
    };
    struct BindGroupRes
    {
        VkDescriptorSet set = nullptr;
        VkDescriptorPool pool = nullptr;  // the pool a persistent set is freed to
        u64 layout_id = 0;
        bool transient = false;
        u64 frame = 0;
        Vector<GroupRef> refs;  // owned; released when the group is collected
        u32 ref_count = 0;
    };
    // A growing list of descriptor pools; the per-frame ones are filled in order and reset together.
    // A pool made for one set of a layout too large for the default sizing; `counts` is what it can hold per type.
    struct ExactPool
    {
        VkDescriptorPool pool;
        u32 counts[kDescriptorKinds];
    };
    struct DescriptorPools
    {
        Vector<VkDescriptorPool> pools;
        Vector<ExactPool> exact;  // reused when they fit, so large layouts do not make a pool per group
        usize current = 0;
    };
    static constexpr usize kMaxSetLayouts = 512;
    static constexpr usize kMaxPipelineLayouts = 512;
    struct CommandBufferSlot
    {
        VkCommandBuffer buffer;
        u64 fence;  // the timeline value that frees it; 0 when free
    };

    void InitializeFormats();
    // Pipeline cache and layout caches (vulkan_device_pipelines.cpp).
    void InitializePipelineCache(const DeviceCreateInfo& info);
    void ShutdownPipelines();
    void ReleaseSetLayoutLocked(u32 slot);
    // Finds or makes the pipeline layout and takes a reference on it; null if it cannot be made.
    VkPipelineLayout PipelineLayoutLocked(const u32* set_slots, const u64* set_ids, u32 count, u32 push_bytes,
                                          ShaderStage push_stages, u32* slot);
    void ReleasePipelineLayoutLocked(u32 slot);
    // Descriptor pools and bind groups (vulkan_device_bindings.cpp).
    VkDescriptorPool MakeDescriptorPoolLocked(u32 max_sets, u32 per_type, bool free_individual,
                                              const BindingDesc* exact, u32 exact_count);
    bool AllocateSetLocked(DescriptorPools& pools, bool persistent, VkDescriptorSetLayout layout,
                           const BindingDesc* bindings, u32 binding_count, VkDescriptorSet* set,
                           VkDescriptorPool* pool);
    void ResetFrameDescriptorsLocked(usize frame_slot);
    void ReleaseBindGroupLocked(BindGroupRes* group);
    void ShutdownDescriptors();
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
    // Records `fill` into a one-time command buffer and submits it, signalling a new timeline value. Holds
    // submit_mutex_.
    template <typename Fill> u64 SubmitInternalLocked(Fill&& fill)
    {
        return SubmitRawLocked([](void* context, VkCommandBuffer cb) { (*static_cast<Fill*>(context))(cb); }, &fill,
                               nullptr, 0, nullptr, 0);
    }
    // `waits` and `extra_signals` are binary semaphores for presentation; the timeline is always signalled too.
    u64 SubmitRawLocked(void (*fill)(void*, VkCommandBuffer), void* context, const VkSemaphoreSubmitInfo* waits,
                        u32 wait_count, const VkSemaphoreSubmitInfo* extra_signals, u32 extra_signal_count);
    bool CreateSurface(const os::NativeWindowInfo& native);
    friend class VulkanSwapchain;
    VkCommandBuffer AcquireUploadCommandBufferLocked();
    void NoteDeviceLost(VkResult result);
    // Fault injection: loses the device now if an injected loss may still strike. Returns whether the device is lost.
    bool InjectLoss(const char* where, u32 number);
    // Counts a present for fault injection (the swapchain, a friend, calls it). Returns whether the device is lost now.
    bool CountPresent();
    void SetDebugName(u64 handle, VkObjectType type, const char* name);
    // Releases buffers whose frames have completed (or all of them when `all`).
    void CollectRetired(bool all);
    void UpdateCompletedFrames();
    // Command pools: one per (frame slot, thread slot), made by the thread that owns the slot.
    struct ThreadPool
    {
        VkCommandPool pool = nullptr;
        Vector<UniquePtr<VulkanCommandList>> lists;
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
    UniquePtr<class VulkanSwapchain> swapchain_;
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
    AdapterInfo adapter_;
    AdapterList
        adapters_;   // every adapter Discover found, kept so a live device never needs a second instance to list them
    u32 epoch_ = 0;  // assigned once the device is known to be usable, so scratch devices do not use up serials
    Atomic<u32> swapchain_live_{0};
    bool anisotropy_enabled_ = false;
    bool memory_budget_enabled_ = false;
    UniquePtr<VulkanMemoryBackend> memory_backend_;
    UniquePtr<MemoryAllocator> memory_;
    Atomic<bool> device_lost_{false};
    u32 lose_device_at_submit_ = 0;        // fault injection: the Nth submit that submits something loses the device
    u32 lose_device_at_present_ = 0;       // fault injection: the Nth present loses the device
    Atomic<u32>* strikes_left_ = nullptr;  // fault injection: limit on injected losses across devices, null = none
    Atomic<u32> submit_count_{0};
    Atomic<u32> present_count_{0};
    u32 lose_device_at_frame_ = 0;  // fault injection: the frame at which to report the device lost, 0 = never
    Atomic<u64> frame_number_{0};

    // Resources and deferred destruction (guarded by resource_mutex_).
    mutable Mutex resource_mutex_;
    HandlePool<BufferRes, BufferTag> buffers_;
    HandlePool<TextureRes, TextureTag> textures_;
    HandlePool<SamplerRes, SamplerTag> samplers_;
    HandlePool<ShaderRes, ShaderTag> shaders_;
    HandlePool<BindGroupLayoutRes, BindGroupLayoutTag> bind_group_layouts_;
    HandlePool<PipelineRes, PipelineTag> pipelines_;
    HandlePool<BindGroupRes, BindGroupTag> bind_groups_;
    DescriptorPools persistent_pools_;
    DescriptorPools frame_pools_[kFramePools];
    Vector<BindGroupHandle> frame_groups_[kFramePools];
    u32 descriptor_sets_per_pool_ = 1024;
    SetLayoutEntry set_layouts_[kMaxSetLayouts];
    u64 next_set_layout_id_ = 1;
    PipelineLayoutEntry pipeline_layouts_[kMaxPipelineLayouts];
    VkPipelineCache pipeline_cache_ = nullptr;
    char cache_path_[512] = {};
    PipelineCacheStats cache_stats_;
    Mutex cache_save_mutex_;
    bool fill_mode_non_solid_ = false;
    VkFormatFeatureFlags format_features_[static_cast<usize>(Format::Count)] = {};
    Atomic<usize> raw_image_count_{0};
    u32 frames_in_flight_ = 2;
    Atomic<bool> frame_open_{false};
    u64 ended_frames_ = 0;
    u64 completed_frames_ = 0;
    static constexpr u64 kTrackedFrames = 64;
    u64 frame_fences_[kTrackedFrames] = {};
    Atomic<usize> raw_buffer_count_{0};

    // Timeline and submission (guarded by submit_mutex_).
    Mutex submit_mutex_;
    VkSemaphore timeline_ = nullptr;
    u64 last_submit_value_ = 0;
    VkCommandPool upload_pool_ = nullptr;
    Vector<CommandBufferSlot> upload_buffers_;
    UniquePtr<RingAllocator> staging_ring_;
    RawBuffer staging_;
    Vector<VkCommandBufferSubmitInfo> submit_scratch_;
    Atomic<i32> inject_submit_result_{0};

    // Per-thread command pools, a ring of kFramePools frames deep, indexed by the thread's slot.
    // The pools are owned here; the atomic slots publish them to other threads without a lock (a null slot is
    // "not made yet"). Only the thread holding a slot makes or fills its pool.
    UniquePtr<ThreadPool> owned_pools_[kFramePools][kThreadSlots];
    Atomic<ThreadPool*> thread_pools_[kFramePools][kThreadSlots] = {};
};

}  // namespace engine_placeholder::rhi::vulkan
