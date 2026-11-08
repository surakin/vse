#pragma once

#include <atomic>
#include <mutex>

#include "containers/unique_ptr.h"
#include "rhi/device.h"
#include "rhi/dx12/com_ref.h"
#include "rhi/dx12/d3d12_api.h"
#include "rhi/dx12/dx12_adapters.h"
#include "rhi/rhi.h"

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
    // Logs that `what` arrives on a later day of Phase 13 (Days 165-169 replace these).
    static void NotYet(const char* what, int day);

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
