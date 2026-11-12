#pragma once

// The Metal implementation of rhi::Device (Phase 26, Days 316-325). Objective-C++: included only by the backend's .mm
// files and its tests (rhi_boundary_check keeps Metal names in engine/rhi/metal/); the rest of the engine reaches it
// through the backend registry (rhi_backends) and the neutral rhi::Device.
//
// Day 316: the device, its queue, the timeline (an MTLSharedEvent: the device's fence), frames in flight, the
// capabilities, the adapters, validation (advisory: Metal's is an environment setting of the process) and
// command-buffer errors. The resource, binding, pipeline, command-list and swapchain operations come on Days 317-321
// and until then fail with a log line saying so.

#include "sync/atomic.h"
#include "sync/mutex.h"

#include "containers/array.h"
#include "containers/vector.h"
#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "rhi/device.h"
#include "rhi/handle_pool.h"
#include "rhi/metal/metal_pipeline_cache.h"
#include "rhi/staging_ring.h"
#include "rhi/rhi.h"

#import <Metal/Metal.h>

namespace engine_placeholder::rhi::metal
{

class MetalDevice;

// Metal's argument tables on the Mac2 family: buffers (vertex buffers and push constants share it), textures, samplers.
inline constexpr u32 kMetalBufferSlots = 31;
inline constexpr u32 kMetalTextureSlots = 128;
inline constexpr u32 kMetalSamplerSlots = 16;

// The buffer slot of vertex buffer binding `binding`: the top of the buffer table, counting down (30, 29, ...), so the
// shader's own buffers, which Slang numbers from 0, never meet them (pipeline creation checks it).
constexpr u32 MetalVertexBufferSlot(u32 binding)
{
    return kMetalBufferSlots - 1 - binding;
}

// One resolved resource of a bind group: its binding, array element and type, and the Metal object (retained) with a
// buffer's offset.
struct MetalGroupResource
{
    u32 binding = 0;
    u32 element = 0;
    BindingType type = BindingType::UniformBuffer;
    id<MTLBuffer> buffer = nil;
    u64 offset = 0;
    id<MTLTexture> texture = nil;
    id<MTLSamplerState> sampler = nil;
};

// One resource a shader stage binds, from its Metal library's slot table matched against a pipeline's layouts (Day
// 318): the set and binding, the binding's type (which argument table) and array size, and the first slot.
struct MetalSlotRef
{
    u8 set = 0;
    u8 binding = 0;
    u8 slot = 0;
    BindingType type = BindingType::UniformBuffer;
    u32 count = 1;
};

// Every resource one stage of a pipeline binds, and its push-constant buffer slot (kNoShaderSlot for none).
struct MetalStageSlots
{
    Vector<MetalSlotRef> refs;
    u32 push_constant_slot = kNoShaderSlot;
};

// Matches a stage's slot table against a pipeline's bind group layouts: every resource the shader binds must be in the
// layout of its set, of the type the shader declares (the same argument table), visible to `stage`, with the same array
// size and inside Metal's tables, no two at one slot, and the push-constant slot inside the buffer table and apart. A
// module without a table is refused when a layout gives the stage any binding. False with the reason in `error`.
bool MapStageSlots(MetalDevice& device, ShaderStage stage, const ShaderSlotTable* table,
                   const BindGroupLayoutHandle* layouts, u32 layout_count, MetalStageSlots* out, char* error,
                   usize error_size);

// The resources bound on one encoder, per stage, so a bind group that sets a slot to what it already holds issues no
// call. A new encoder starts empty: call Reset when one begins (Day 318; the command lists use it from Day 320).
class MetalBinder
{
public:
    // Forgets what is bound (a new encoder).
    void Reset();
    // Binds a group's resources at set `set` for the vertex and fragment stages of a render encoder (either may be
    // null). False when the group is invalid or lacks a resource a stage binds (logged).
    bool BindRender(MetalDevice& device, id<MTLRenderCommandEncoder> encoder, const MetalStageSlots* vertex,
                    const MetalStageSlots* fragment, u32 set, BindGroupHandle group);
    // The same for a compute encoder.
    bool BindCompute(MetalDevice& device, id<MTLComputeCommandEncoder> encoder, const MetalStageSlots& compute, u32 set,
                     BindGroupHandle group);
    // Writes push constants at the stages' push-constant slots (setVertexBytes:, setFragmentBytes:); `size` at most
    // kMaxPushConstantBytes. A stage without push constants is skipped.
    void PushRender(id<MTLRenderCommandEncoder> encoder, const MetalStageSlots* vertex, const MetalStageSlots* fragment,
                    const void* data, u32 size);
    void PushCompute(id<MTLComputeCommandEncoder> encoder, const MetalStageSlots& compute, const void* data, u32 size);
    // How many set calls the binder issued since the last Reset (tests: unchanged slots are skipped).
    u32 IssuedCalls() const { return issued_; }

    // The slots of one stage on the current encoder (pointers are not retained: the device keeps the objects alive
    // while a frame that used them runs). A push writes a buffer slot too, so it forgets that slot.
    struct Bound
    {
        Array<const void*, kMetalBufferSlots> buffers = {};
        Array<u64, kMetalBufferSlots> offsets = {};
        Array<const void*, kMetalTextureSlots> textures = {};
        Array<const void*, kMetalSamplerSlots> samplers = {};
    };

private:
    Bound vertex_;
    Bound fragment_;
    Bound compute_;
    u32 issued_ = 0;
    Vector<MetalGroupResource> scratch_;  // reused: no allocation per bind once warm
};

// What a command list needs of a pipeline (Day 320), resolved by handle when it is bound: the Metal states, the
// fixed-function state Metal sets on the encoder, each stage's slots and a compute pipeline's threadgroup size. The
// objects are retained.
struct MetalPipelineInfo
{
    bool compute = false;
    id<MTLRenderPipelineState> render = nil;
    id<MTLComputePipelineState> compute_state = nil;
    id<MTLDepthStencilState> depth = nil;
    MTLPrimitiveType primitive = MTLPrimitiveTypeTriangle;
    MTLCullMode cull = MTLCullModeBack;
    MTLWinding winding = MTLWindingCounterClockwise;
    MTLTriangleFillMode fill = MTLTriangleFillModeFill;
    u32 vertex_binding_count = 0;
    MetalStageSlots vertex;
    MetalStageSlots fragment;
    MetalStageSlots kernel;
    MTLSize threadgroup = MTLSizeMake(0, 0, 0);
};

// The vertex descriptor of a pipeline description: each attribute at its location, read from its binding's buffer at
// MetalVertexBufferSlot, and each binding's stride and step rate (Day 319). Exposed for tests.
MTLVertexDescriptor* MakeMetalVertexDescriptor(const GraphicsPipelineDesc& desc);

// What the command buffers' completion handlers touch: shared with each handler, so a handler that runs after the
// device is gone (Metal calls them on its own threads, after the timeline value the device waited for) still has it.
//
// Thread-safety: thread-safe (atomics only). Ownership: shared by the device and every pending handler.
struct MetalDeviceShared
{
    Atomic<bool> lost{false};
};

// Makes every Metal device created afterwards in this process use the Managed storage mode for CPU-visible buffers, as
// on a GPU without unified memory, so a unified Mac covers that path (a test hook: Phase 26 Day 317). Thread-safety:
// thread-safe; devices read it when they are created.
void ForceManagedStorageForTest(bool on);

/// Owning pointer to a Metal device; destroying it waits for the GPU first (see rhi::DeviceDeleter).
using MetalDevicePtr = UniquePtr<MetalDevice, DeviceDeleter>;

// The Metal device: one adapter (an MTLDevice), one MTLCommandQueue for all work, and an MTLSharedEvent as the timeline
// the fence values count on. Every wait on the GPU is bounded: it waits in slices and checks for a lost device between
// them.
//
// Thread-safety: thread-safe, as rhi::Device is: resource creation and destruction from any thread; Submit, BeginFrame,
// EndFrame and WaitIdle belong to one submission thread. Completion handlers run on Metal's threads and touch only
// atomics. Ownership: the caller owns the device through a MetalDevicePtr; it owns the MTLDevice reference, the queue
// and the event.
class MetalDevice final : public Device
{
public:
    MetalDevice() = default;
    ~MetalDevice() override;

    // Creates a device as rhi::CreateDevice does for this backend; null with the reason logged.
    static MetalDevicePtr Create(const DeviceCreateInfo& info);
    // Application objects (buffers, textures, ...) devices still had when they were destroyed, since the last reset;
    // each is also a warning in the log. For tests. Thread-safety: thread-safe.
    static u64 LeakedUserObjects();
    static void ResetLeakedUserObjects();
    // Lists the adapters (every MTLDevice of the system), as rhi::EnumerateAdapters does for this backend.
    static bool EnumerateAdapters(const DeviceCreateInfo& info, AdapterList* out);

    // How many frames of fences the device remembers; frames further behind than this are waited for.
    static constexpr u64 kTrackedFrames = 64;
    // How long one slice of a GPU wait lasts before the lost-device check, in milliseconds.
    static constexpr u64 kWaitSliceMs = 100;

    const char* BackendName() const override { return "metal"; }
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
    bool DeviceLost() const override { return shared_->lost.Load(MemoryOrder::Acquire); }

    // Day 318 (metal_device_bindings.mm).
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void DestroySampler(SamplerHandle sampler) override;
    BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    void DestroyBindGroupLayout(BindGroupLayoutHandle layout) override;
    BindGroupHandle CreateBindGroup(const BindGroupDesc& desc) override;
    void DestroyBindGroup(BindGroupHandle group) override;

    // Day 317 (metal_device_resources.mm).
    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void DestroyBuffer(BufferHandle buffer) override;
    TextureHandle CreateTexture(const TextureDesc& desc) override;
    void DestroyTexture(TextureHandle texture) override;
    UploadTicket UploadTexture(TextureHandle dst, u32 mip, u32 layer, const void* data, u32 row_pitch) override;
    bool ReadbackTexture(TextureHandle src, u32 mip, u32 layer, void* dst, u64 dst_size, ResourceState state) override;
    void* MapBuffer(BufferHandle buffer) override;
    void UnmapBuffer(BufferHandle buffer) override;
    void FlushMappedBuffer(BufferHandle buffer, u64 offset, u64 size) override;
    UploadTicket UploadBuffer(BufferHandle dst, u64 offset, const void* data, u64 size) override;
    bool ReadbackBuffer(BufferHandle src, u64 offset, void* dst, u64 size) override;

    // Day 319 (metal_device_pipelines.mm).
    ShaderHandle CreateShader(const ShaderDesc& desc) override;
    void DestroyShader(ShaderHandle shader) override;
    PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override;
    void DestroyPipeline(PipelineHandle pipeline) override;
    // Waits for the GPU and writes the pipeline cache.
    void Quiesce() override;

    // Days 320-321 (until then: a failure with a log line).
    Swapchain* CreateSwapchain(const SwapchainDesc& desc) override;
    void DestroySwapchain(Swapchain* swapchain) override;
    CommandList* CreateCommandList() override;
    bool RecreateSurface(const os::Window* window) override;

    // Day 316.
    bool FormatSupport(Format format, TextureUsage usage) const override;
    bool CanGenerateMips(Format format) const override;
    FenceValue Submit(CommandList* const* lists, usize count) override;
    bool WaitFence(FenceValue value, u64 timeout_ns) override;
    FenceValue CompletedFence() const override;
    void BeginFrame() override;
    void EndFrame() override;
    u64 FrameNumber() const override;
    void WaitIdle() override;

    // The Metal device. Ownership: borrowed; valid for the life of this object.
    id<MTLDevice> Mtl() const { return device_; }
    // The queue every command buffer comes from. Ownership: borrowed.
    id<MTLCommandQueue> Queue() const { return queue_; }
    // Whether Metal's API validation is on for this process (MTL_DEBUG_LAYER was set when Metal started).
    bool ValidationEnabled() const { return validation_; }
    // How many frames the CPU may be ahead of the GPU (1 to 3).
    u32 FramesInFlight() const { return frames_in_flight_; }
    // The highest frame number whose GPU work has finished.
    u64 CompletedFrame() const;

    // Signals the timeline with the next value after the work already committed to the queue and returns it; 0 when the
    // device is lost. Not thread-safe: call with the submission lock held (Submit and the internal paths do).
    u64 SignalLocked();
    // Encodes the next timeline value at the end of `buffer`, which the caller then commits; returns the value. Not
    // thread-safe: under the submission lock.
    u64 SignalAfterLocked(id<MTLCommandBuffer> buffer);
    // Waits until the timeline reaches `value`, in slices of kWaitSliceMs with a lost-device check between them, for at
    // most `timeout_ns` (~0 for no limit). True if reached; false on timeout or when the device is lost.
    bool WaitForValue(u64 value, u64 timeout_ns);
    // Adds the completion handler every command buffer of the device gets: a failed buffer is logged with its label and
    // a lost-device error marks the device lost (the handler runs on a Metal thread).
    void WatchCommandBuffer(id<MTLCommandBuffer> buffer);
    // Records a lost device (once): logged, an ensure, and further submissions are ignored.
    void NoteLost(const char* where, const char* reason) { NoteLost(*shared_, where, reason); }
    // The same for the state a completion handler holds.
    static void NoteLost(MetalDeviceShared& shared, const char* where, const char* reason);

    // The submission lock (the queue's order and the timeline's values).
    Mutex& SubmitMutex() { return submit_mutex_; }

    // Whether CPU-visible buffers use the Managed storage mode (a GPU without unified memory, or
    // ForceManagedStorageForTest).
    bool ManagedStorage() const { return managed_; }
    // The Metal buffer of a live handle, nil for an invalid or stale one. Ownership: borrowed while the handle lives.
    id<MTLBuffer> BufferObject(BufferHandle buffer) const;
    // The Metal texture of a live handle, nil for an invalid or stale one. Ownership: borrowed while the handle lives.
    id<MTLTexture> TextureObject(TextureHandle texture) const;
    // The Metal sampler of a live handle, nil for an invalid or stale one. Ownership: borrowed while the handle lives.
    id<MTLSamplerState> SamplerObject(SamplerHandle sampler) const;
    // The bindings of a live layout (sorted by binding), copied into `out`; false for an invalid or stale handle.
    bool LayoutBindings(BindGroupLayoutHandle layout, Vector<BindingDesc>* out) const;
    // Resolves a group's resources by handle (the reset rule: groups hold handles, read at record time) into `out`,
    // which keeps the Metal objects alive; false for an invalid group or a resource destroyed since. The lock is held
    // only while resolving: the caller makes its encoder calls afterwards.
    bool ResolveGroup(BindGroupHandle group, Vector<MetalGroupResource>* out) const;
    // A pipeline's Metal state (Day 319), copied into `out` (its slot lists too); false for an invalid or stale handle.
    bool LookupPipeline(PipelineHandle pipeline, MetalPipelineInfo* out) const;
    // The pipeline cache: its archive and its file.
    MetalPipelineCache& PipelineCache() { return pipeline_cache_; }
    // Pipelines found in the cache's archive and pipelines compiled, since the device was created.
    u32 PipelineCacheHits() const { return cache_hits_.Load(MemoryOrder::Relaxed); }
    u32 PipelineCacheMisses() const { return cache_misses_.Load(MemoryOrder::Relaxed); }
    // The depth-stencil state for a description, shared by equal ones (Day 319). Ownership: the device keeps it.
    id<MTLDepthStencilState> DepthStencilState(bool test, bool write, CompareOp compare);
    // The storage mode a buffer of `location` gets on this device (Private, Shared or Managed).
    MTLStorageMode StorageModeFor(MemoryLocation location) const;

private:
    bool Initialize(const DeviceCreateInfo& info);
    void FillCaps();
    bool TakeStrike();
    bool InjectLoss(const char* where, u32 number);
    void UpdateCompletedFramesLocked();

    // Day 318 (metal_device_bindings.mm).
    static constexpr u32 kFrameSlots = 4;  // transient groups end when their frame's slot comes round again
    struct SamplerRes
    {
        id<MTLSamplerState> state = nil;
        SamplerDesc desc;  // normalized, without the debug name
        u32 refs = 1;      // equal descriptions share one sampler
    };
    struct BindGroupLayoutRes
    {
        Vector<BindingDesc> bindings;  // sorted by binding
    };
    struct GroupEntry
    {
        u32 binding = 0;
        u32 element = 0;
        BindingType type = BindingType::UniformBuffer;
        BufferHandle buffer;
        u64 offset = 0;
        TextureHandle texture;
        SamplerHandle sampler;
    };
    struct BindGroupRes
    {
        Vector<GroupEntry> entries;
        bool transient = false;
    };
    void RetireFrameGroupsLocked(u32 slot);

    // Day 319 (metal_device_pipelines.mm).
    struct ShaderRes
    {
        id<MTLLibrary> library = nil;
        id<MTLFunction> function = nil;
        ShaderStage stage = ShaderStage::None;
        bool has_slots = false;
        Vector<ShaderSlot> slots;  // copied from the descriptor's table (borrowed only for the call)
        u32 push_constant_slot = kNoShaderSlot;
        u32 threadgroup[3] = {0, 0, 0};
    };
    struct PipelineRes
    {
        MetalPipelineInfo info;
    };
    struct DepthStateEntry
    {
        u32 key = 0;
        id<MTLDepthStencilState> state = nil;
    };
    // A shader's function and slot table, copied out (the table's `slots` point into `*slots`); false for an invalid
    // handle or one of another stage.
    bool ShaderParts(ShaderHandle shader, ShaderStage stage, id<MTLFunction>* function, Vector<ShaderSlot>* slots,
                     ShaderSlotTable* table, bool* has_table) const;
    void ReleaseSamplerLocked(SamplerHandle sampler);

    struct BufferRes
    {
        id<MTLBuffer> buffer = nil;
        BufferDesc desc;           // without the debug name
        u8* mapped = nullptr;      // the contents of a Shared or Managed buffer
        bool user_mapped = false;  // between MapBuffer and UnmapBuffer
        bool managed = false;      // Managed: CPU writes need didModifyRange, GPU writes a synchronizeResource
        u64 bytes = 0;             // the allocation's size, as counted
    };
    struct TextureRes
    {
        id<MTLTexture> texture = nil;
        TextureDesc desc;                                       // without the debug name
        ResourceState upload_state = ResourceState::Undefined;  // as UploadTexture leaves it (the RHI's rule)
        bool external = false;                                  // a swapchain image (Day 321): the swapchain owns it
        u64 bytes = 0;
    };
    // metal_device_resources.mm.
    bool InitializeResources();
    void ShutdownResources();
    void CollectRetiredLocked(bool all);
    // Makes a buffer, counting it (and failing as an injected allocation failure asks); nil with the reason logged.
    id<MTLBuffer> NewBuffer(u64 size, MTLResourceOptions options, const char* name, u64* bytes);
    bool CountAllocation(const char* name);
    void ReleaseBytes(u64 bytes);
    // A command buffer for the device's own copies, nil when the device is lost; committed by CommitInternalLocked,
    // which signals the timeline after it and returns the value (0 when lost). Under the submission lock.
    id<MTLCommandBuffer> BeginInternalLocked(NSString* label);
    u64 CommitInternalLocked(id<MTLCommandBuffer> buffer);
    // Space in the staging ring, waiting for the GPU to free some when it is full. Under the submission lock.
    bool StageLocked(u64 size, u64 alignment, u64* offset);
    // Runs `encode` on a blit encoder with a temporary Shared buffer of `bytes`, waits, and copies the buffer to `dst`.
    bool BlitToCpu(u64 bytes, NSString* label, void (^encode)(id<MTLBlitCommandEncoder>, id<MTLBuffer>), u8* dst);
    // Buffer-to-buffer copies through the staging ring or a temporary buffer, for any range (aligned to 4 bytes
    // inside).
    bool ReadGpuBuffer(id<MTLBuffer> source, u64 offset, u64 size, u8* dst);
    UploadTicket WriteGpuBuffer(id<MTLBuffer> target, u64 offset, const u8* data, u64 size);
    // didModifyRange: on a Managed buffer (macOS).
    static void NoteModified(id<MTLBuffer> buffer, u64 offset, u64 size);
    static void NoteLeakedUserObjects(u64 count);

    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> queue_ = nil;
    id<MTLSharedEvent> timeline_ = nil;
    MTLSharedEventListener* listener_ = nil;  // delivers the timeline's notifications for WaitForValue
    AdapterInfo adapter_;
    AdapterList adapters_;
    DeviceCaps caps_;
    u32 epoch_ = 0;
    u32 frames_in_flight_ = 2;
    bool validation_ = false;
    Array<bool, static_cast<usize>(Format::Count)> mip_support_ = {};

    // Faults (DeviceCreateInfo::faults).
    u32 lose_device_at_frame_ = 0;
    u32 lose_device_at_submit_ = 0;
    Atomic<u32>* strikes_left_ = nullptr;
    Atomic<u32> submit_count_{0};

    SharedPtr<MetalDeviceShared> shared_ = MakeShared<MetalDeviceShared>();  // the loss flag, shared with the handlers
    Atomic<bool> frame_open_{false};

    mutable Mutex submit_mutex_;
    u64 last_signaled_ = 0;  // under submit_mutex_
    Atomic<u64> last_signaled_published_{0};

    mutable Mutex resource_mutex_;
    u64 frame_number_ = 0;      // under resource_mutex_
    u64 ended_frames_ = 0;      // the last frame EndFrame closed
    u64 completed_frames_ = 0;  // the last frame whose fence the GPU reached
    Array<u64, kTrackedFrames> frame_fences_ = {};

    // Resources (Day 317), under resource_mutex_.
    HandlePool<BufferRes, BufferTag> buffers_;
    HandlePool<TextureRes, TextureTag> textures_;
    bool managed_ = false;
    u32 fail_allocations_from_ = 0;
    Atomic<u32> allocation_count_{0};  // device allocations asked for, for fail_allocations_from
    Atomic<u64> device_memory_bytes_{0};
    Atomic<u32> raw_resources_{0};  // Metal buffers and textures alive, the application's and the device's own

    // Bindings (Day 318), under resource_mutex_.
    HandlePool<SamplerRes, SamplerTag> samplers_;
    HandlePool<BindGroupLayoutRes, BindGroupLayoutTag> bind_group_layouts_;
    HandlePool<BindGroupRes, BindGroupTag> bind_groups_;
    Array<Vector<BindGroupHandle>, kFrameSlots> frame_groups_;

    // Shaders and pipelines (Day 319), under resource_mutex_.
    HandlePool<ShaderRes, ShaderTag> shaders_;
    HandlePool<PipelineRes, PipelineTag> pipelines_;
    Vector<DepthStateEntry> depth_states_;
    MetalPipelineCache pipeline_cache_;
    Atomic<u32> cache_hits_{0};
    Atomic<u32> cache_misses_{0};

    // The staging ring (a Shared buffer), under submit_mutex_.
    id<MTLBuffer> staging_ = nil;
    u8* staging_mapped_ = nullptr;
    u64 staging_bytes_ = 0;
    UniquePtr<RingAllocator> staging_ring_;
};

}  // namespace engine_placeholder::rhi::metal
