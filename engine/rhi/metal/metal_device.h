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
#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "rhi/device.h"
#include "rhi/rhi.h"

#import <Metal/Metal.h>

namespace engine_placeholder::rhi::metal
{

class MetalDevice;

// What the command buffers' completion handlers touch: shared with each handler, so a handler that runs after the
// device is gone (Metal calls them on its own threads, after the timeline value the device waited for) still has it.
//
// Thread-safety: thread-safe (atomics only). Ownership: shared by the device and every pending handler.
struct MetalDeviceShared
{
    Atomic<bool> lost{false};
};

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

    // Days 317-321 (until then: a failure with a log line).
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
    UploadTicket UploadTexture(TextureHandle dst, u32 mip, u32 layer, const void* data, u32 row_pitch) override;
    bool ReadbackTexture(TextureHandle src, u32 mip, u32 layer, void* dst, u64 dst_size, ResourceState state) override;
    void* MapBuffer(BufferHandle buffer) override;
    void UnmapBuffer(BufferHandle buffer) override;
    UploadTicket UploadBuffer(BufferHandle dst, u64 offset, const void* data, u64 size) override;
    bool ReadbackBuffer(BufferHandle src, u64 offset, void* dst, u64 size) override;
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

private:
    bool Initialize(const DeviceCreateInfo& info);
    void FillCaps();
    bool TakeStrike();
    bool InjectLoss(const char* where, u32 number);
    void UpdateCompletedFramesLocked();

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

    mutable Mutex frame_mutex_;
    u64 frame_number_ = 0;      // under frame_mutex_
    u64 ended_frames_ = 0;      // the last frame EndFrame closed
    u64 completed_frames_ = 0;  // the last frame whose fence the GPU reached
    Array<u64, kTrackedFrames> frame_fences_ = {};
};

}  // namespace engine_placeholder::rhi::metal
