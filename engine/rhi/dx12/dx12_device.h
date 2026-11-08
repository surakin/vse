#pragma once

#include <atomic>
#include <mutex>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "rhi/device.h"
#include "rhi/dx12/com_ref.h"
#include "rhi/dx12/d3d12_api.h"
#include "rhi/dx12/dx12_adapters.h"
#include "rhi/dx12/dx12_descriptor_heap.h"
#include "rhi/handle_pool.h"
#include "rhi/rhi.h"
#include "rhi/staging_ring.h"

//! The Direct3D 12 implementation of [[rhi::Device]] (Phase 13, Days 164-172).

namespace engine_placeholder::rhi::dx12 {

class Dx12Device;

/// Owning pointer to a Direct3D 12 device; destroying it waits for the GPU first (see [[rhi::DeviceDeleter]]).
using Dx12DevicePtr = UniquePtr<Dx12Device, DeviceDeleter>;

/// The Direct3D 12 device: one adapter, one `ID3D12Device` at feature level 12_0, and one direct queue for all work.
///
/// Created by [[rhi::CreateDevice]] with `Backend::Dx12` (or [[rhi::dx12::Dx12Device::Create]]). With validation on, the debug
/// layer's messages go to [[rhi::LogRhi]] through `ID3D12InfoQueue1`'s callback and are counted ([[rhi::dx12::GetDebugMessageCounts]]).
/// Every wait on the GPU is bounded: it waits in slices and checks for a removed device between them.
///
/// Thread-safety: thread-safe, as [[rhi::Device]] is: resource creation and destruction from any thread; `Submit`, `BeginFrame`,
///   `EndFrame` and `WaitIdle` belong to one submission thread.
/// Ownership: the caller owns the device through a [[rhi::dx12::Dx12DevicePtr]]; it owns the factory, the adapter and the queue.
class Dx12Device final : public Device {
public:
    /// Constructs an uninitialized device; use [[rhi::dx12::Dx12Device::Create]].
    Dx12Device() = default;

    /// Creates a device.
    ///
    /// Param info: what to create; the backend field is ignored.
    /// Returns: the device, or an empty pointer.
    /// Fails: as for [[rhi::CreateDevice]]; also when validation is `On` and the debug layer (the Graphics Tools feature) is missing.
    /// Ownership: the caller owns the result.
    static Dx12DevicePtr Create(const DeviceCreateInfo& info);

    /// Lists the adapters, as [[rhi::EnumerateAdapters]] does for this backend.
    ///
    /// Param info: the window (presentability) is read from it.
    /// Param out: receives the list.
    /// Returns: false if no DXGI factory could be created.
    static bool EnumerateAdapters(const DeviceCreateInfo& info, AdapterList* out);

    /// How many frames of fences the device remembers; frames further behind than this are waited for.
    static constexpr u64 kTrackedFrames = 64;
    /// How long one slice of a GPU wait lasts before the device-removed check, in milliseconds.
    static constexpr u32 kWaitSliceMs = 100;
    /// How many frame slots the per-frame resources rotate through (the shader-visible view ring, transient bind groups); a slot is
    /// reused only after the frame that last used it has finished.
    static constexpr u32 kFrameSlots = 3;
    /// Descriptors in one frame slot of the shader-visible view ring.
    static constexpr u32 kViewsPerFrame = 131072;

    /// Waits for the GPU, releases everything, reports live objects (with validation) and destroys the device.
    ~Dx12Device() override;

    const char* BackendName() const override { return "dx12"; }
    const AdapterInfo& Adapter() const override { return adapter_; }
    bool ListAdapters(AdapterList* out) const override {
        *out = adapters_;
        return true;
    }
    u32 Epoch() const override { return epoch_; }
    const DeviceCaps& Caps() const override { return caps_; }
    ResourceCounts Resources() const override;
    bool InFrame() const override { return frame_open_.load(std::memory_order_acquire); }
    void InjectDeviceLoss() override { InjectLoss("request", 0); }
    bool DeviceLost() const override { return device_lost_.load(std::memory_order_relaxed); }
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
    Swapchain* CreateSwapchain(const SwapchainDesc& desc) override;
    void DestroySwapchain(Swapchain* swapchain) override;
    bool FormatSupport(Format format, TextureUsage usage) const override;
    bool CanGenerateMips(Format format) const override;
    UploadTicket UploadTexture(TextureHandle dst, u32 mip, u32 layer, const void* data, u32 row_pitch) override;
    bool ReadbackTexture(TextureHandle src, u32 mip, u32 layer, void* dst, u64 dst_size, ResourceState state) override;
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
    bool RecreateSurface(const os::Window* window) override;

    /// Returns the D3D12 device. Ownership: borrowed; valid for the life of this object.
    ID3D12Device* D3D() const { return device_.Get(); }
    /// Returns the direct queue. Ownership: borrowed.
    ID3D12CommandQueue* Queue() const { return queue_.Get(); }
    /// Returns the DXGI factory the device was created from. Ownership: borrowed.
    IDXGIFactory6* Factory() const { return factory_.Get(); }
    /// Returns whether the debug layer is on for this device.
    bool ValidationEnabled() const { return validation_; }
    /// Returns whether GPU-based validation is on (`DeviceCreateInfo::sync_validation` with validation).
    bool GpuValidationEnabled() const { return gpu_validation_; }
    /// Returns how many frames the CPU may be ahead of the GPU (1 to 3).
    u32 FramesInFlight() const { return frames_in_flight_; }
    /// Returns the highest frame number whose GPU work has finished.
    u64 CompletedFrame() const;

    /// Notes the result of a D3D12 or DXGI call: a device-loss code sets the sticky lost state (reading
    /// `GetDeviceRemovedReason` for the log), any other failure is logged with `what`.
    ///
    /// Param result: the code.
    /// Param what: the call, for the log.
    /// Returns: true if `result` succeeded.
    bool Check(HRESULT result, const char* what);

    /// Waits until the queue's fence reaches `value`, in slices of [[rhi::dx12::Dx12Device::kWaitSliceMs]] with a device-removed check
    /// between them, for at most `timeout_ns` (`~0` for no limit).
    ///
    /// Param value: a fence value; 0 is reached at once.
    /// Param timeout_ns: the longest wait.
    /// Returns: true if the value was reached.
    /// Fails: false on timeout or when the device is removed (the lost state is set).
    bool WaitForValue(u64 value, u64 timeout_ns);

    /// Signals the queue's fence with the next value (after the work already on the queue) and returns it.
    ///
    /// Returns: the value, or 0 if the signal failed (the device is lost).
    /// Thread-safety: not-thread-safe: call with the submission lock held (Submit and the internal upload path do).
    u64 SignalLocked();

    /// What command recording needs to know about a live buffer.
    ///
    /// Thread-safety: thread-compatible plain data (a snapshot).
    /// Ownership: `resource` is borrowed; it stays valid until the buffer is destroyed and its frame has completed.
    struct BufferInfo {
        /// The resource.
        ID3D12Resource* resource = nullptr;
        /// Its GPU virtual address.
        u64 gpu_address = 0;
        /// The creation descriptor (without the debug name).
        BufferDesc desc;
    };
    /// Looks up a live buffer.
    ///
    /// Param buffer: the handle.
    /// Param out: receives the snapshot.
    /// Returns: true if the handle is live.
    /// Fails: false for an invalid or stale handle.
    /// Thread-safety: thread-safe.
    bool LookupBuffer(BufferHandle buffer, BufferInfo* out) const;

    /// What command recording needs to know about a live texture.
    ///
    /// Thread-safety: thread-compatible plain data (a snapshot).
    /// Ownership: the resource and descriptors are borrowed; valid until the texture is destroyed and its frame has completed.
    struct TextureInfo {
        /// The resource.
        ID3D12Resource* resource = nullptr;
        /// The creation descriptor (without the debug name).
        TextureDesc desc;
        /// The format the resource was created with (typeless for a sampled depth texture).
        DXGI_FORMAT resource_format = DXGI_FORMAT_UNKNOWN;
        /// The render-target view of mip 0, layer 0; `ptr` 0 without `ColorAttachment` usage.
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        /// The depth-stencil view of mip 0, layer 0; `ptr` 0 without `DepthAttachment` usage.
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        /// The shader-resource view of every mip and layer; `ptr` 0 without `Sampled` usage.
        D3D12_CPU_DESCRIPTOR_HANDLE srv{};
        /// The unordered-access view of mip 0; `ptr` 0 without `Storage` usage.
        D3D12_CPU_DESCRIPTOR_HANDLE uav{};
    };
    /// Looks up a live texture.
    ///
    /// Param texture: the handle.
    /// Param out: receives the snapshot.
    /// Returns: true if the handle is live.
    /// Fails: false for an invalid or stale handle.
    /// Thread-safety: thread-safe.
    bool LookupTexture(TextureHandle texture, TextureInfo* out) const;

    /// Returns the D3D12 state a texture is in after everything submitted so far (D3D12 has no "undefined" before-state, so the
    /// device keeps this: uploads, readbacks and, from Day 168, each submitted command list update it).
    ///
    /// Param texture: a live texture.
    /// Returns: the state; `COMMON` for an unknown handle.
    /// Fails: `COMMON`, as described.
    /// Thread-safety: thread-safe.
    D3D12_RESOURCE_STATES TextureState(TextureHandle texture) const;

    /// Records the state a texture is in after work just submitted.
    ///
    /// Param texture: a live texture; an unknown handle is ignored.
    /// Param state: the state.
    /// Thread-safety: thread-safe.
    void SetTextureState(TextureHandle texture, D3D12_RESOURCE_STATES state);

    /// What command recording needs to know about a live bind group.
    ///
    /// Thread-safety: thread-compatible plain data (a snapshot).
    /// Ownership: the descriptors are borrowed; valid until the group is destroyed (persistent) or its frame has finished (transient).
    struct BindGroupInfo {
        /// The group's CBV/SRV/UAV descriptors, contiguous in a CPU heap; copied into the frame's ring at `SetBindGroup`.
        D3D12_CPU_DESCRIPTOR_HANDLE views{};
        /// How many there are (0: the group has no views).
        u32 view_count = 0;
        /// The group's sampler table in the shader-visible sampler heap; `ptr` 0 without samplers.
        D3D12_GPU_DESCRIPTOR_HANDLE samplers{};
        /// How many samplers.
        u32 sampler_count = 0;
        /// Identity of the group's layout; equal ids mean equal layouts.
        u64 layout_id = 0;
        /// True for a transient group.
        bool transient = false;
        /// For a transient group, the frame it was created in.
        u64 frame = 0;
        /// False if a resource the group was built from has since been destroyed.
        bool resources_live = true;
    };
    /// Looks up a bind group and checks that the resources it refers to are still alive.
    ///
    /// Param group: the handle.
    /// Param out: receives the snapshot.
    /// Returns: true if the handle is live.
    /// Fails: false for an invalid or stale handle.
    /// Thread-safety: thread-safe.
    bool LookupBindGroup(BindGroupHandle group, BindGroupInfo* out) const;

    /// A root signature and where each bind group and the push constants sit in it.
    ///
    /// Thread-safety: thread-compatible plain data.
    /// Ownership: `signature` is borrowed; the device keeps every root signature it made until it is destroyed.
    struct RootLayout {
        /// The root signature.
        ID3D12RootSignature* signature = nullptr;
        /// Per bind group slot: the root parameter of its CBV/SRV/UAV table, or -1 when it has no views.
        i32 view_param[kMaxBindGroups] = {-1, -1, -1, -1};
        /// Per bind group slot: the root parameter of its sampler table, or -1 when it has no samplers.
        i32 sampler_param[kMaxBindGroups] = {-1, -1, -1, -1};
        /// The root parameter of the push constants, or -1.
        i32 push_param = -1;
        /// The push constants' size in 32-bit values.
        u32 push_values = 0;
        /// The layouts' identities, in slot order.
        u64 layout_ids[kMaxBindGroups] = {};
        /// How many layouts.
        u32 layout_count = 0;
    };
    /// Finds or makes the root signature of a pipeline layout (the design spec's section 5): one CBV/SRV/UAV table and one sampler
    /// table per bind group, registers `b`/`t`/`u`/`s` numbered as the bindings in `space<slot>`, and the push constants as root
    /// constants at `b0, space15`. Serialized as root signature 1.1 and cached by the layouts' identities and the push range.
    ///
    /// Param layouts: the bind group layouts, in slot order; each live.
    /// Param count: how many, at most [[rhi::kMaxBindGroups]].
    /// Param push_bytes: the push-constant range, at most [[rhi::kMaxPushConstantBytes]].
    /// Param push_stages: the stages that see it.
    /// Param compute: true for a compute pipeline (every parameter visible to all stages; no input layout).
    /// Param out: receives the layout.
    /// Returns: true on success.
    /// Fails: false, logging the reason, for a stale layout handle or a root signature D3D12 rejects.
    /// Thread-safety: thread-safe.
    bool RootSignatureFor(const BindGroupLayoutHandle* layouts, u32 count, u32 push_bytes, ShaderStage push_stages, bool compute, RootLayout* out);

    /// Returns the shader-visible view ring. Ownership: borrowed.
    GpuDescriptorRing& ViewRing() { return view_ring_; }
    /// Returns the shader-visible sampler heap. Ownership: borrowed.
    ID3D12DescriptorHeap* SamplerHeap() const { return sampler_tables_heap_.Heap(); }
    /// Returns how many sampler descriptors of the shader-visible heap are free, for tests.
    u32 FreeSamplerDescriptors() const;
    /// Returns how many root signatures the device holds, for tests of the cache.
    usize RootSignatureCount() const;

    /// Objects found alive by destroyed devices.
    ///
    /// Thread-safety: thread-compatible plain data (a snapshot).
    struct LeakCounts {
        /// Application objects (buffers, textures, ...) left when the device was destroyed.
        u64 user_objects = 0;
        /// D3D12 objects the debug layer's live-object report named besides the device: a leak in the backend.
        u64 live_d3d12_objects = 0;
    };
    /// Returns the process-wide totals since the last reset.
    /// Thread-safety: thread-safe.
    static LeakCounts GetLeakCounts();
    /// Sets the totals to zero. Thread-safe.
    static void ResetLeakCounts();

private:
    bool Initialize(const DeviceCreateInfo& info);
    void FillCaps();
    bool InjectLoss(const char* where, u32 number);
    void NoteRemoved(const char* where);
    void UpdateCompletedFramesLocked();
    void CollectRetiredLocked(bool all);
    void ReportLiveObjects();
    // Logs that `what` arrives on a later day of Phase 13 (Days 166-169 replace these).
    static void NotYet(const char* what, int day);

    // ---- resources (dx12_device_resources.cpp) ----
    struct BufferRes {
        ComRef<ID3D12Resource> resource;
        BufferDesc desc;              // without the debug name
        u8* mapped = nullptr;         // persistent mapping of an upload or readback heap buffer
        bool user_mapped = false;     // between MapBuffer and UnmapBuffer
        u64 bytes = 0;                // the allocation's size
    };
    struct TextureRes {
        ComRef<ID3D12Resource> resource;
        TextureDesc desc;             // without the debug name
        DXGI_FORMAT resource_format = DXGI_FORMAT_UNKNOWN;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON; // actual, after everything submitted
        ResourceState upload_state = ResourceState::Undefined;     // as UploadTexture leaves it (the RHI's rule)
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        D3D12_CPU_DESCRIPTOR_HANDLE srv{};
        D3D12_CPU_DESCRIPTOR_HANDLE uav{};
        bool external = false;        // a swapchain image (Day 169): the swapchain owns the resource
        u64 bytes = 0;
    };
    struct InternalList {
        ComRef<ID3D12CommandAllocator> allocator;
        ComRef<ID3D12GraphicsCommandList> list;
        u64 fence = 0;                // the value that frees it; ~0 while recording
    };
    bool InitializeResources();
    void ShutdownResources();
    void FillFormatSupport();
    bool CreateCommitted(D3D12_HEAP_TYPE heap, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state, const char* name,
                         ComRef<ID3D12Resource>* out, u64* bytes);
    void ReleaseResourceBytes(u64 bytes);
    void ReleaseTextureViews(TextureRes& t);
    // Internal submissions (uploads, readbacks) on the queue, under submit_mutex_.
    ID3D12GraphicsCommandList* BeginInternalLocked(usize* slot);
    u64 SubmitInternalLocked(usize slot);
    bool StageLocked(u64 size, u64 alignment, u64* offset);
    void SetDebugName(ID3D12Object* object, const char* name);

    // ---- samplers, bind group layouts, bind groups, root signatures (dx12_device_bindings.cpp) ----
    struct SamplerRes {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        SamplerDesc desc; // normalized: anisotropy clamped, name dropped
        u32 refs = 0;
    };
    struct LayoutBinding {
        BindingDesc desc;
        u32 table_offset = 0; // in the view table, or in the sampler table for a sampler
    };
    struct BindGroupLayoutRes {
        LayoutBinding bindings[kMaxBindingsPerLayout];
        u32 binding_count = 0;
        u32 view_count = 0;
        u32 sampler_count = 0;
        u64 id = 0;
    };
    struct GroupRef {
        BindingType type = BindingType::UniformBuffer;
        BufferHandle buffer;
        TextureHandle texture;
        SamplerHandle sampler;
    };
    struct BindGroupRes {
        D3D12_CPU_DESCRIPTOR_HANDLE views{};
        u32 view_count = 0;
        u32 sampler_table = ~0u; // index into sampler_tables_, ~0 without samplers
        u64 layout_id = 0;
        bool transient = false;
        u64 frame = 0;
        Vector<GroupRef> refs;
    };
    // A table of samplers in the shader-visible heap, shared by every group with the same samplers in the same order.
    struct SamplerTable {
        u32 offset = 0;
        u32 count = 0;
        Vector<u64> keys; // the samplers' handles (index and generation): a destroyed sampler's descriptor may be reused, its handle not
        u32 refs = 0;     // 0: the entry is free
    };
    struct LayoutIdentity {
        BindingDesc bindings[kMaxBindingsPerLayout];
        u32 count = 0;
        u64 id = 0;
    };
    struct RootSignatureEntry {
        ComRef<ID3D12RootSignature> signature;
        RootLayout layout;
        u32 push_bytes = 0;
        ShaderStage push_stages = ShaderStage::None;
        bool compute = false;
    };
    bool InitializeBindings();
    void ShutdownBindings();
    void ReleaseBindGroupLocked(BindGroupRes& group);
    void RetireFrameGroupsLocked(u32 slot);
    u32 AcquireSamplerTableLocked(const u64* keys, const usize* sources, u32 count);
    void ReleaseSamplerTableLocked(u32 index);

    HandlePool<SamplerRes, SamplerTag> samplers_;
    HandlePool<BindGroupLayoutRes, BindGroupLayoutTag> bind_group_layouts_;
    HandlePool<BindGroupRes, BindGroupTag> bind_groups_;
    CpuDescriptorHeap sampler_cpu_heap_;
    CpuDescriptorBlocks group_views_;           // each bind group's views, contiguous
    GpuDescriptorRing view_ring_;               // shader-visible, per frame slot
    GpuDescriptorTables sampler_tables_heap_;   // shader-visible samplers
    Vector<SamplerTable> sampler_tables_;
    Vector<LayoutIdentity> layout_identities_;  // never shrinks: an id is never reused
    u64 next_layout_id_ = 1;
    Vector<RootSignatureEntry> root_signatures_; // under resource_mutex_
    Vector<BindGroupHandle> frame_groups_[kFrameSlots];

    HandlePool<BufferRes, BufferTag> buffers_;
    HandlePool<TextureRes, TextureTag> textures_;
    CpuDescriptorHeap view_heap_;   // CBV/SRV/UAV
    CpuDescriptorHeap rtv_heap_;
    CpuDescriptorHeap dsv_heap_;
    ComRef<ID3D12Resource> staging_;
    u64 staging_bytes_ = 0;
    u8* staging_mapped_ = nullptr;
    UniquePtr<RingAllocator> staging_ring_;
    Vector<InternalList> internal_lists_;   // under submit_mutex_
    std::atomic<u64> device_memory_bytes_{0};
    std::atomic<u32> raw_resources_{0};     // ID3D12Resource objects alive, user and internal
    u32 format_support_[static_cast<usize>(Format::Count)] = {}; // D3D12_FORMAT_SUPPORT1 of the typed (or view) format
    bool format_support_ready_ = false;

    ComRef<IDXGIFactory6> factory_;
    ComRef<IDXGIAdapter1> dxgi_adapter_;
    ComRef<ID3D12Device> device_;
    ComRef<ID3D12CommandQueue> queue_;
    ComRef<ID3D12Fence> fence_;
    ComRef<ID3D12InfoQueue1> info_queue_;
    DWORD info_callback_cookie_ = 0;
    bool validation_ = false;
    bool gpu_validation_ = false;

    AdapterInfo adapter_;
    AdapterList adapters_;
    DeviceCaps caps_;
    u32 epoch_ = 0;
    u32 frames_in_flight_ = 2;
    const os::Window* window_ = nullptr;

    // Fault injection (rhi::FaultInjection), copied at creation.
    u32 lose_device_at_frame_ = 0;
    u32 lose_device_at_submit_ = 0;
    u32 lose_device_at_present_ = 0;
    std::atomic<u32>* strikes_left_ = nullptr;
    std::atomic<u32> submit_count_{0};

    std::atomic<bool> device_lost_{false};
    std::atomic<bool> frame_open_{false};

    // The submission lock orders ExecuteCommandLists and fence signals; the resource lock guards the frame counters and pools.
    std::mutex submit_mutex_;
    mutable std::mutex resource_mutex_;
    u64 last_signaled_ = 0; // under submit_mutex_
    std::atomic<u64> last_signaled_published_{0};
    u64 frame_number_ = 0;
    u64 ended_frames_ = 0;
    u64 completed_frames_ = 0;
    u64 frame_fences_[kTrackedFrames] = {};
};

} // namespace engine_placeholder::rhi::dx12
