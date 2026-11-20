#pragma once

#include "sync/atomic.h"
#include "sync/mutex.h"

#include "containers/hash_map.h"
#include "containers/shared_ptr.h"
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
#include "mem/heap_allocator.h"

//! The Direct3D 12 implementation of [[rhi::Device]] (Phase 13, Days 164-172).

namespace vse::rhi::dx12
{

class Dx12Device;
class Dx12CommandList;
class Dx12Swapchain;

/// Owning pointer to a Direct3D 12 device; destroying it waits for the GPU first (see [[rhi::DeviceDeleter]]).
using Dx12DevicePtr = UniquePtr<Dx12Device, DeviceDeleter>;

/// The Direct3D 12 device: one adapter, one `ID3D12Device` at feature level 12_0, and one direct queue for all work.
///
/// Created by [[rhi::CreateDevice]] with `Backend::Dx12` (or [[rhi::dx12::Dx12Device::Create]]). With validation on,
/// the debug layer's messages go to [[rhi::LogRhi]] through `ID3D12InfoQueue1`'s callback and are counted
/// ([[rhi::dx12::GetDebugMessageCounts]]). Every wait on the GPU is bounded: it waits in slices and checks for a
/// removed device between them.
///
/// - Thread-safety: thread-safe, as [[rhi::Device]] is: resource creation and destruction from any thread; `Submit`,
/// `BeginFrame`,
///   `EndFrame` and `WaitIdle` belong to one submission thread.
/// - Ownership: the caller owns the device through a [[rhi::dx12::Dx12DevicePtr]]; it owns the factory, the adapter and
/// the queue.
class Dx12Device final : public Device
{
public:
    /// Constructs an uninitialized device; use [[rhi::dx12::Dx12Device::Create]].
    Dx12Device() = default;

    /// Creates a device.
    ///
    /// - Param info: what to create; the backend field is ignored.
    /// - Returns: the device, or an empty pointer.
    /// - Fails: as for [[rhi::CreateDevice]]; also when validation is `On` and the debug layer (the Graphics Tools
    /// feature) is missing.
    /// - Ownership: the caller owns the result.
    static Dx12DevicePtr Create(const DeviceCreateInfo& info);

    /// Lists the adapters, as [[rhi::EnumerateAdapters]] does for this backend.
    ///
    /// - Param info: the window (presentability) is read from it.
    /// - Param out: receives the list.
    /// - Returns: false if no DXGI factory could be created.
    static bool EnumerateAdapters(const DeviceCreateInfo& info, AdapterList* out);

    /// How many frames of fences the device remembers; frames further behind than this are waited for.
    static constexpr u64 kTrackedFrames = 64;
    /// How long one slice of a GPU wait lasts before the device-removed check, in milliseconds.
    static constexpr u32 kWaitSliceMs = 100;
    /// How many frame slots the per-frame resources rotate through (the shader-visible view ring, transient bind
    /// groups); a slot is reused only after the frame that last used it has finished.
    static constexpr u32 kFrameSlots = 3;
    /// Descriptors in one frame slot of the shader-visible view ring.
    static constexpr u32 kViewsPerFrame = 131072;

    /// Waits for the GPU, releases everything, reports live objects (with validation) and destroys the device.
    ~Dx12Device() override;

    const char* BackendName() const override { return "dx12"; }
    const AdapterInfo& Adapter() const override { return adapter_; }
    bool ListAdapters(AdapterList* out) const override
    {
        *out = adapters_;
        return true;
    }
    u32 Epoch() const override { return epoch_; }
    const DeviceCaps& Caps() const override { return caps_; }
    ResourceCounts Resources() const override;
    bool InFrame() const override { return frame_open_.Load(MemoryOrder::Acquire); }
    void InjectDeviceLoss() override;
    bool DeviceLost() const override { return device_lost_.Load(MemoryOrder::Relaxed); }
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
    /// - Param result: the code.
    /// - Param what: the call, for the log.
    /// - Returns: true if `result` succeeded.
    bool Check(HRESULT result, const char* what);

    /// Waits until the queue's fence reaches `value`, in slices of [[rhi::dx12::Dx12Device::kWaitSliceMs]] with a
    /// device-removed check between them, for at most `timeout_ns` (`~0` for no limit).
    ///
    /// - Param value: a fence value; 0 is reached at once.
    /// - Param timeout_ns: the longest wait.
    /// - Returns: true if the value was reached.
    /// - Fails: false on timeout or when the device is removed (the lost state is set).
    bool WaitForValue(u64 value, u64 timeout_ns);

    /// Signals the queue's fence with the next value (after the work already on the queue) and returns it.
    ///
    /// - Returns: the value, or 0 if the signal failed (the device is lost).
    /// - Thread-safety: not-thread-safe: call with the submission lock held (Submit and the internal upload path do).
    u64 SignalLocked();

    /// What command recording needs to know about a live buffer.
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    /// - Ownership: `resource` is borrowed; it stays valid until the buffer is destroyed and its frame has completed.
    struct BufferInfo
    {
        /// The resource.
        ID3D12Resource* resource = nullptr;
        /// Its GPU virtual address.
        u64 gpu_address = 0;
        /// The creation descriptor (without the debug name).
        BufferDesc desc;
    };
    /// Looks up a live buffer.
    ///
    /// - Param buffer: the handle.
    /// - Param out: receives the snapshot.
    /// - Returns: true if the handle is live.
    /// - Fails: false for an invalid or stale handle.
    /// - Thread-safety: thread-safe.
    bool LookupBuffer(BufferHandle buffer, BufferInfo* out) const;

    /// What command recording needs to know about a live texture.
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    /// - Ownership: the resource and descriptors are borrowed; valid until the texture is destroyed and its frame has
    /// completed.
    struct TextureInfo
    {
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
    /// - Param texture: the handle.
    /// - Param out: receives the snapshot.
    /// - Returns: true if the handle is live.
    /// - Fails: false for an invalid or stale handle.
    /// - Thread-safety: thread-safe.
    bool LookupTexture(TextureHandle texture, TextureInfo* out) const;

    /// Returns the D3D12 state a texture is in after everything submitted so far (D3D12 has no "undefined"
    /// before-state, so the device keeps this: uploads, readbacks and, from Day 168, each submitted command list update
    /// it).
    ///
    /// - Param texture: a live texture.
    /// - Returns: the state; `COMMON` for an unknown handle.
    /// - Fails: `COMMON`, as described.
    /// - Thread-safety: thread-safe.
    D3D12_RESOURCE_STATES TextureState(TextureHandle texture) const;

    /// Records the state a texture is in after work just submitted.
    ///
    /// - Param texture: a live texture; an unknown handle is ignored.
    /// - Param state: the state.
    /// - Thread-safety: thread-safe.
    void SetTextureState(TextureHandle texture, D3D12_RESOURCE_STATES state);

    /// A GPU-only buffer a bind group reads or writes, and the state that use needs; the command list tracks buffer
    /// states with it.
    ///
    /// - Thread-safety: thread-compatible plain data.
    /// - Ownership: `resource` is borrowed, valid while the group's buffer lives.
    struct GroupBufferUse
    {
        /// The buffer's resource.
        ID3D12Resource* resource = nullptr;
        /// `VERTEX_AND_CONSTANT_BUFFER`, a shader-resource state or `UNORDERED_ACCESS`.
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    };

    /// What command recording needs to know about a live bind group.
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    /// - Ownership: the descriptors are borrowed; valid until the group is destroyed (persistent) or its frame has
    /// finished (transient).
    struct BindGroupInfo
    {
        /// The group's CBV/SRV/UAV descriptors, contiguous in a CPU heap; copied into the frame's ring at
        /// `SetBindGroup`.
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
        /// The GPU-only buffers the group uses (borrowed from the group; valid until it is collected).
        const GroupBufferUse* buffer_uses = nullptr;
        /// How many.
        u32 buffer_use_count = 0;
    };
    /// Looks up a bind group and checks that the resources it refers to are still alive.
    ///
    /// - Param group: the handle.
    /// - Param out: receives the snapshot.
    /// - Returns: true if the handle is live.
    /// - Fails: false for an invalid or stale handle.
    /// - Thread-safety: thread-safe.
    bool LookupBindGroup(BindGroupHandle group, BindGroupInfo* out) const;

    /// A root signature and where each bind group and the push constants sit in it.
    ///
    /// - Thread-safety: thread-compatible plain data.
    /// - Ownership: `signature` is borrowed; the device keeps every root signature it made until it is destroyed.
    struct RootLayout
    {
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
        /// FNV-1a hash of the serialized root signature: stable across runs, unlike the layout identities, so the
        /// pipeline cache keys on it.
        u64 content_hash = 0;
    };
    /// Finds or makes the root signature of a pipeline layout (the design spec's section 5): one CBV/SRV/UAV table and
    /// one sampler table per bind group, registers `b`/`t`/`u`/`s` numbered as the bindings in `space<slot>`, and the
    /// push constants as root constants at `b0, space15`. Serialized as root signature 1.1 and cached by the layouts'
    /// identities and the push range.
    ///
    /// - Param layouts: the bind group layouts, in slot order; each live.
    /// - Param count: how many, at most [[rhi::kMaxBindGroups]].
    /// - Param push_bytes: the push-constant range, at most [[rhi::kMaxPushConstantBytes]].
    /// - Param push_stages: the stages that see it.
    /// - Param compute: true for a compute pipeline (every parameter visible to all stages; no input layout).
    /// - Param out: receives the layout.
    /// - Returns: true on success.
    /// - Fails: false, logging the reason, for a stale layout handle or a root signature D3D12 rejects.
    /// - Thread-safety: thread-safe.
    bool RootSignatureFor(const BindGroupLayoutHandle* layouts, u32 count, u32 push_bytes, ShaderStage push_stages,
                          bool compute, RootLayout* out);

    /// What command recording needs to know about a live pipeline (Day 168 records with it).
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    /// - Ownership: the D3D12 objects are borrowed; valid until the pipeline is destroyed and its frame has finished.
    struct PipelineInfo
    {
        /// The pipeline state.
        ID3D12PipelineState* state = nullptr;
        /// For a strip with primitive restart: the state that cuts at 0xFFFF, for 16-bit indices (`state` cuts at
        /// 0xFFFFFFFF); else null.
        ID3D12PipelineState* state_cut16 = nullptr;
        /// The root signature and where each group and the push constants sit in it.
        RootLayout root;
        /// True for a compute pipeline.
        bool compute = false;
        /// The primitive topology for `IASetPrimitiveTopology`.
        D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        /// Each vertex buffer slot's stride, for `IASetVertexBuffers`.
        u32 vertex_strides[kMaxVertexBindings] = {};
        /// How many vertex buffer slots.
        u32 vertex_binding_count = 0;
        /// The render target formats, in order.
        Format color_formats[kMaxColorAttachments] = {};
        /// How many render targets.
        u32 color_count = 0;
        /// The depth format, or `Undefined`.
        Format depth_format = Format::Undefined;
        /// The push constant range in bytes.
        u32 push_bytes = 0;
    };
    /// Looks up a pipeline.
    ///
    /// - Param pipeline: the handle.
    /// - Param out: receives the snapshot.
    /// - Returns: true if the handle is live.
    /// - Fails: false for an invalid or stale handle.
    /// - Thread-safety: thread-safe.
    bool LookupPipeline(PipelineHandle pipeline, PipelineInfo* out) const;

    /// What the on-disk pipeline cache (an `ID3D12PipelineLibrary`) did for this device.
    ///
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    struct PipelineCacheStats
    {
        /// Whether a cache is in use (not when disabled, without a directory, or when D3D12 cannot make a library).
        bool enabled = false;
        /// Bytes of the file the library was loaded from; 0 if there was none or it was ignored.
        u64 loaded_bytes = 0;
        /// True if a file existed but was ignored (damaged, or for another adapter or driver).
        bool rejected = false;
        /// Pipeline states found in the library (a strip with primitive restart has two states).
        u32 hits = 0;
        /// Pipeline states compiled because the library did not have them.
        u32 misses = 0;
        /// Bytes written by the last save; 0 until a save.
        u64 saved_bytes = 0;
    };
    /// Returns what the pipeline cache did so far.
    ///
    /// - Thread-safety: thread-safe.
    PipelineCacheStats PipelineCache() const;
    /// Writes the pipeline cache to disk now, as the destructor does, if it holds pipelines the file does not.
    ///
    /// - Returns: true if a file was written.
    /// - Fails: false when the cache is disabled, unchanged since it was loaded or saved, or cannot be written
    /// (logged).
    /// - Thread-safety: thread-safe.
    bool SavePipelineCache();
    /// Returns the pipeline cache file's path, or an empty string when no cache is in use.
    const char* PipelineCachePath() const { return cache_path_; }

    /// The compute pipeline `GenerateMips` runs (shaders/mipgen.comp), with its own root signature: a table of the
    /// source level's view and the written level's storage view, a static linear clamp sampler, and four root
    /// constants.
    ///
    /// - Thread-safety: thread-compatible plain data.
    /// - Ownership: borrowed; the device keeps both for its life.
    struct MipPipeline
    {
        /// The root signature.
        ID3D12RootSignature* root = nullptr;
        /// The pipeline state.
        ID3D12PipelineState* state = nullptr;
    };
    /// Returns the mip pipeline, making it on first use.
    ///
    /// - Param out: receives it.
    /// - Returns: true on success.
    /// - Fails: false (logged) if D3D12 refuses the root signature or the pipeline.
    /// - Thread-safety: thread-safe.
    bool MipPipelineFor(MipPipeline* out);
    /// Returns whether a texture of this description is created so `GenerateMips` can write its levels: `Sampled`,
    /// `CopySrc` and `CopyDst` usage, more than one level, 2D, one sample, and a format
    /// [[rhi::Device::CanGenerateMips]] accepts. Such a texture gets unordered access, through a typeless resource for
    /// an sRGB format.
    ///
    /// - Param desc: the texture's description.
    /// - Returns: true if it qualifies.
    /// - Thread-safety: thread-safe.
    bool MipGenCapable(const TextureDesc& desc) const;
    /// Returns the format of the storage view `GenerateMips` writes a level through: the format itself, or its linear
    /// twin for sRGB.
    ///
    /// - Param format: the texture's format.
    /// - Returns: the DXGI format.
    /// - Thread-safety: thread-safe. A pure function.
    static DXGI_FORMAT MipStorageFormat(Format format);

    /// Waits until the GPU has finished everything submitted so far, without freeing what was destroyed (unlike
    /// `WaitIdle`): what the open frame destroyed may still be used by its lists that are not submitted yet. For the
    /// swapchain, which must not hold a buffer the GPU still reads.
    ///
    /// - Thread-safety: thread-safe.
    void WaitForGpu();
    /// Returns the window the device presents to, or null. Ownership: borrowed.
    const os::Window* Window() const { return window_; }
    /// Makes a swap chain buffer a device texture: a render-target view in `format` (the sRGB view of a linear buffer),
    /// external (the swap chain owns it), in `PRESENT`.
    ///
    /// - Param buffer: the buffer; the texture holds a reference until released.
    /// - Param format: the RHI format of the view.
    /// - Param width: its width.
    /// - Param height: its height.
    /// - Returns: the handle, or invalid when out of descriptors or handles.
    /// - Fails: an invalid handle (logged by the pools).
    /// - Thread-safety: thread-safe.
    TextureHandle RegisterSwapchainImage(ComRef<ID3D12Resource> buffer, Format format, u32 width, u32 height);
    /// Frees a swap chain image's texture at once, with its reference to the buffer; the GPU must be done with it.
    ///
    /// - Param texture: from `RegisterSwapchainImage`.
    /// - Thread-safety: thread-safe.
    void ReleaseSwapchainImage(TextureHandle texture);
    /// Counts a present for the fault injection (`FaultInjection::lose_device_at_present`).
    ///
    /// - Returns: true if the device is lost (now or before).
    /// - Thread-safety: thread-safe.
    bool CountPresent();
    /// Presents a swap chain under the submission lock, so it is ordered with the queue's work; a device-loss result
    /// marks the device lost.
    ///
    /// - Param swapchain: the swap chain.
    /// - Param interval: the sync interval.
    /// - Param flags: the present flags.
    /// - Returns: the result of `IDXGISwapChain::Present`.
    /// - Thread-safety: thread-safe.
    HRESULT PresentSwapchain(IDXGISwapChain3* swapchain, UINT interval, UINT flags);

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
    /// - Thread-safety: thread-compatible plain data (a snapshot).
    struct LeakCounts
    {
        /// Application objects (buffers, textures, ...) left when the device was destroyed.
        u64 user_objects = 0;
        /// D3D12 objects the debug layer's live-object report named besides the device: a leak in the backend.
        u64 live_d3d12_objects = 0;
    };
    /// Returns the process-wide totals since the last reset.
    /// - Thread-safety: thread-safe.
    static LeakCounts GetLeakCounts();
    /// Sets the totals to zero. Thread-safe.
    static void ResetLeakCounts();

private:
    bool Initialize(const DeviceCreateInfo& info);
    void FillCaps();
    bool InjectLoss(const char* where, u32 number);
    // Takes one strike of the shared limit (FaultInjection::strikes_left); false when it is spent. No limit always
    // strikes.
    bool TakeStrike();
    void NoteRemoved(const char* where);
    void UpdateCompletedFramesLocked();
    void CollectRetiredLocked(bool all);
    void ReportLiveObjects();

    // ---- resources (dx12_device_resources.cpp) ----
    struct BufferRes
    {
        ComRef<ID3D12Resource> resource;
        BufferDesc desc;           // without the debug name
        u8* mapped = nullptr;      // persistent mapping of an upload or readback heap buffer
        bool user_mapped = false;  // between MapBuffer and UnmapBuffer
        u64 bytes = 0;             // the allocation's size
    };
    struct TextureRes
    {
        ComRef<ID3D12Resource> resource;
        TextureDesc desc;  // without the debug name
        DXGI_FORMAT resource_format = DXGI_FORMAT_UNKNOWN;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;  // actual, after everything submitted
        ResourceState upload_state = ResourceState::Undefined;      // as UploadTexture leaves it (the RHI's rule)
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        D3D12_CPU_DESCRIPTOR_HANDLE srv{};
        D3D12_CPU_DESCRIPTOR_HANDLE uav{};
        bool external = false;  // a swapchain image (Day 169): the swapchain owns the resource
        u64 bytes = 0;
    };
    struct InternalList
    {
        ComRef<ID3D12CommandAllocator> allocator;
        ComRef<ID3D12GraphicsCommandList> list;
        u64 fence = 0;  // the value that frees it; ~0 while recording
    };
    bool InitializeResources();
    void ShutdownResources();
    void FillFormatSupport();
    bool CreateCommitted(D3D12_HEAP_TYPE heap, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state,
                         const char* name, ComRef<ID3D12Resource>* out, u64* bytes);
    void ReleaseResourceBytes(u64 bytes);
    void ReleaseTextureViews(TextureRes& t);
    // Internal submissions (uploads, readbacks) on the queue, under submit_mutex_.
    ID3D12GraphicsCommandList* BeginInternalLocked(usize* slot);
    u64 SubmitInternalLocked(usize slot);
    bool StageLocked(u64 size, u64 alignment, u64* offset);
    void SetDebugName(ID3D12Object* object, const char* name);

    // ---- samplers, bind group layouts, bind groups, root signatures (dx12_device_bindings.cpp) ----
    struct SamplerRes
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        SamplerDesc desc;  // normalized: anisotropy clamped, name dropped
        u32 refs = 0;
    };
    struct LayoutBinding
    {
        BindingDesc desc;
        u32 table_offset = 0;  // in the view table, or in the sampler table for a sampler
    };
    struct BindGroupLayoutRes
    {
        LayoutBinding bindings[kMaxBindingsPerLayout];
        u32 binding_count = 0;
        u32 view_count = 0;
        u32 sampler_count = 0;
        u64 id = 0;
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
        D3D12_CPU_DESCRIPTOR_HANDLE views{};
        u32 view_count = 0;
        u32 sampler_table = ~0u;  // index into sampler_tables_, ~0 without samplers
        u64 layout_id = 0;
        bool transient = false;
        u64 frame = 0;
        Vector<GroupRef> refs;
        Vector<GroupBufferUse> buffer_uses;
    };
    // A table of samplers in the shader-visible heap, shared by every group with the same samplers in the same order.
    struct SamplerTable
    {
        u32 offset = 0;
        u32 count = 0;
        Vector<u64> keys;  // the samplers' handles (index and generation): a destroyed sampler's descriptor may be
                           // reused, its handle not
        u32 refs = 0;      // 0: the entry is free
        u64 hash = 0;      // SamplerTableHash of `keys`, its key in sampler_table_index_
    };
    struct LayoutIdentity
    {
        BindingDesc bindings[kMaxBindingsPerLayout];
        u32 count = 0;
        u64 id = 0;
    };
    struct RootSignatureEntry
    {
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

    // ---- shaders, pipelines and the pipeline cache (dx12_device_pipelines.cpp) ----
    struct ShaderCode
    {
        Vector<byte> bytes;  // a copy of the DXIL container
    };
    struct ShaderRes
    {
        SharedPtr<ShaderCode> code;  // shared with pipeline creation in progress, so it compiles outside the lock
        ShaderStage stage = ShaderStage::None;
        u64 hash = 0;  // FNV-1a of the bytes, part of a pipeline's cache key
    };
    struct PipelineRes
    {
        ComRef<ID3D12PipelineState> state;
        ComRef<ID3D12PipelineState> state_cut16;
        PipelineInfo info;
    };
    void InitializePipelineCache(const DeviceCreateInfo& info);
    void ShutdownPipelines();
    // Load the pipeline stored under `key` from the library, else create it and store it there.
    ComRef<ID3D12PipelineState> CachedGraphicsState(u64 key, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
                                                    const char* name);
    ComRef<ID3D12PipelineState> CachedComputeState(u64 key, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc,
                                                   const char* name);
    void StoreInLibrary(u64 key, ID3D12PipelineState* state);
    PipelineHandle AddPipeline(PipelineRes&& res, const char* what);

    // ---- command lists and submission (dx12_device_commands.cpp) ----
    struct ListPool
    {
        Vector<UniquePtr<Dx12CommandList>> lists;
        usize used = 0;
        u64 frame = 0;  // the frame the pool's lists were last handed out in
    };
    UniquePtr<Dx12Swapchain> swapchain_;  // owner thread (dx12_device_swapchain.cpp)
    Atomic<u32> swapchain_live_{0};
    Atomic<u32> present_count_{0};
    ListPool list_pools_[kFrameSlots];  // under list_mutex_
    Mutex list_mutex_;
    void ShutdownCommands();
    ComRef<ID3D12RootSignature> mip_root_;  // under mip_mutex_
    ComRef<ID3D12PipelineState> mip_state_;
    Mutex mip_mutex_;
    bool mip_support_[static_cast<usize>(Format::Count)] = {};

    HandlePool<ShaderRes, ShaderTag> shaders_;
    HandlePool<PipelineRes, PipelineTag> pipelines_;
    ComRef<ID3D12PipelineLibrary> library_;  // under library_mutex_
    Vector<byte> library_file_;   // the file the library was made from: D3D12 reads it for the library's life
    bool library_dirty_ = false;  // pipelines stored since the last load or save
    Vector<u64>
        library_keys_;  // sorted: the pipelines the library holds (asking for another one makes the debug layer warn)
    char cache_path_[512] = {};
    PipelineCacheStats cache_stats_;  // under library_mutex_
    mutable Mutex library_mutex_;

    HandlePool<SamplerRes, SamplerTag> samplers_;
    HandlePool<BindGroupLayoutRes, BindGroupLayoutTag> bind_group_layouts_;
    HandlePool<BindGroupRes, BindGroupTag> bind_groups_;
    CpuDescriptorHeap sampler_cpu_heap_;
    CpuDescriptorBlocks group_views_;          // each bind group's views, contiguous
    GpuDescriptorRing view_ring_;              // shader-visible, per frame slot
    GpuDescriptorTables sampler_tables_heap_;  // shader-visible samplers
    Vector<SamplerTable> sampler_tables_;
    // Container audit Day 12: the live tables by the hash of their keys (a hit is compared, a colliding set gets a
    // table of its own, unindexed) and the free entries, so acquiring a table no longer scans every one.
    HashMap<u64, u32, HeapAllocator> sampler_table_index_{HeapAllocator::Default()};
    Vector<u32> free_sampler_tables_;
    Vector<LayoutIdentity> layout_identities_;  // never shrinks: an id is never reused
    u64 next_layout_id_ = 1;
    Vector<RootSignatureEntry> root_signatures_;  // under resource_mutex_
    Vector<BindGroupHandle> frame_groups_[kFrameSlots];

    HandlePool<BufferRes, BufferTag> buffers_;
    HandlePool<TextureRes, TextureTag> textures_;
    CpuDescriptorHeap view_heap_;  // CBV/SRV/UAV
    CpuDescriptorHeap rtv_heap_;
    CpuDescriptorHeap dsv_heap_;
    ComRef<ID3D12Resource> staging_;
    u64 staging_bytes_ = 0;
    u8* staging_mapped_ = nullptr;
    UniquePtr<RingAllocator> staging_ring_;
    Vector<InternalList> internal_lists_;  // under submit_mutex_
    Atomic<u64> device_memory_bytes_{0};
    Atomic<u32> raw_resources_{0};                                // ID3D12Resource objects alive, user and internal
    u32 format_support_[static_cast<usize>(Format::Count)] = {};  // D3D12_FORMAT_SUPPORT1 of the typed (or view) format
    bool format_support_ready_ = false;

    ComRef<IDXGIFactory6> factory_;
    ComRef<IDXGIAdapter1> dxgi_adapter_;
    ComRef<ID3D12Device> device_;
    ComRef<ID3D12CommandQueue> queue_;
    ComRef<ID3D12Fence> fence_;
    ComRef<ID3D12InfoQueue1> info_queue_;
    DWORD info_callback_cookie_ = 0;
    bool device_user_counted_ = false;  // counted among the users of the shared ID3D12Device (dx12_device.cpp)
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
    Atomic<u32>* strikes_left_ = nullptr;
    u32 fail_allocations_from_ = 0;
    Atomic<u32> allocation_count_{0};  // committed resources asked for, for fail_allocations_from
    bool removal_injected_ = false;    // InjectDeviceLoss removed this device with the debug layer on (owner thread)
    Atomic<u32> submit_count_{0};

    Atomic<bool> device_lost_{false};
    Atomic<bool> frame_open_{false};

    // The submission lock orders ExecuteCommandLists and fence signals; the resource lock guards the frame counters and
    // pools.
    Mutex submit_mutex_;
    mutable Mutex resource_mutex_;
    u64 last_signaled_ = 0;  // under submit_mutex_
    Atomic<u64> last_signaled_published_{0};
    u64 frame_number_ = 0;
    u64 ended_frames_ = 0;
    u64 completed_frames_ = 0;
    u64 frame_fences_[kTrackedFrames] = {};
};

}  // namespace vse::rhi::dx12
