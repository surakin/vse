#pragma once

#include "rhi/device.h"
#include "types.h"

//! A backend that validates every call against the RHI contract and draws nothing.
//!
//! It lets the layers above the RHI, and the contract itself, be tested with no GPU. Buffer contents are
//! kept in host memory so uploads, copies and mapping round-trip; textures keep only their state.

namespace engine_placeholder::rhi {

namespace detail {
struct NullDeviceState;
} // namespace detail

/// Options for [[rhi::CreateNullDevice]].
///
/// Thread-safety: thread-compatible plain data.
struct NullDeviceOptions {
    /// Creates options with the RHI-guaranteed minimum limits, except that the sampler anisotropy limit is 16.
    NullDeviceOptions() { caps.max_sampler_anisotropy = 16.0f; }

    /// The limits the device reports and validates against.
    DeviceCaps caps;
    /// Whether a contract violation aborts the process (as a real backend would) instead of being counted.
    bool abort_on_error = false;
    /// How many frames the simulated GPU lags behind the CPU: a resource destroyed in frame F is released when
    /// frame F + latency has ended. 0 means the GPU finishes each frame instantly.
    u32 simulated_latency_frames = 0;
    /// Largest buffer the device will allocate, in bytes; larger requests fail like an out-of-memory condition.
    u64 max_buffer_bytes = 256ull << 20;
    /// Reports the device lost at the start of this frame (1 is the first `BeginFrame`), as [[rhi::FaultInjection]] does
    /// for the Vulkan backend; 0 never does.
    u32 lose_device_at_frame = 0;
    /// Makes `RecreateSurface` fail, to exercise the fallback to a full reset.
    bool fail_surface_recreate = false;
    /// The adapter the device reports from `Device::Adapter`; the default is the first of [[rhi::DefaultNullAdapters]].
    /// [[rhi::CreateDevice]] fills it from the requested adapter.
    AdapterInfo adapter = DefaultAdapter();

    /// Returns the adapter a default-constructed option set reports.
    static AdapterInfo DefaultAdapter() {
        AdapterList list;
        DefaultNullAdapters(&list);
        return list.items[0];
    }
};

/// Counts of the null device's resources, for tests and leak checks.
///
/// Thread-safety: thread-compatible plain data.
struct NullDeviceStats {
    /// Live buffers.
    usize buffers = 0;
    /// Live textures.
    usize textures = 0;
    /// Live samplers.
    usize samplers = 0;
    /// Live shader modules.
    usize shaders = 0;
    /// Live bind group layouts.
    usize bind_group_layouts = 0;
    /// Live bind groups.
    usize bind_groups = 0;
    /// Live pipelines.
    usize pipelines = 0;
    /// Destroyed resources still waiting for their frame to complete, across all kinds.
    usize pending_destruction = 0;
    /// Command lists handed out and not yet recycled.
    usize command_lists = 0;
};

class NullDevice;

/// Owning pointer to a null device; destroying it waits for the device first (see [[rhi::DeviceDeleter]]).
using NullDevicePtr = UniquePtr<NullDevice, DeviceDeleter>;

/// The validating, GPU-less device.
///
/// Every contract violation is logged on [[rhi::LogRhi]] at error level, counted, and otherwise ignored
/// (the offending call does nothing, or returns the invalid handle), unless
/// [[rhi::NullDeviceOptions::abort_on_error]] is set. Resource state is validated when work is submitted,
/// because only then is the order of lists known.
///
/// Thread-safety: thread-safe for creation, destruction, mapping and inspection; `Submit`, `BeginFrame`,
///   `EndFrame` and `WaitIdle` belong to one submission thread, as for [[rhi::Device]].
/// Ownership: created by [[rhi::CreateNullDevice]], which returns a [[rhi::NullDevicePtr]].
class NullDevice final : public Device {
public:
    /// Creates the device; use [[rhi::CreateNullDevice]].
    ///
    /// Param options: limits and behavior.
    explicit NullDevice(const NullDeviceOptions& options);

    /// Releases every resource, including those still waiting for deferred destruction.
    ~NullDevice() override;

    const char* BackendName() const override;
    const AdapterInfo& Adapter() const override;
    u32 Epoch() const override;
    ResourceCounts Resources() const override;
    bool InFrame() const override;
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

    /// Returns how many contract violations have been reported since creation or the last `ResetErrors`.
    u32 ErrorCount() const;

    /// Copies the most recent error message into `out`, truncating and always terminating it.
    ///
    /// Param out: destination buffer.
    /// Param capacity: size of `out` in bytes; 0 writes nothing.
    /// Returns: the length of the message copied, 0 if there has been no error.
    /// Ownership: `out` is borrowed for the call; the device keeps no reference to it.
    usize LastError(char* out, usize capacity) const;

    /// Clears the error count and the last message.
    void ResetErrors();

    /// Returns the current resource counts.
    NullDeviceStats Statistics() const;

    /// Copies bytes out of a buffer's host storage regardless of its memory location, for test inspection.
    ///
    /// Param buffer: a live buffer.
    /// Param offset: byte offset to read from.
    /// Param out: destination for `size` bytes.
    /// Param size: bytes to read.
    /// Returns: true if the range was copied.
    /// Fails: returns false for a stale handle or a range outside the buffer.
    /// Ownership: `out` is borrowed for the call; the device keeps no reference to it.
    bool PeekBuffer(BufferHandle buffer, u64 offset, void* out, u64 size) const;

    /// Returns the tracked state of a buffer, for test inspection.
    ///
    /// Param buffer: a live buffer.
    /// Returns: its state, or `Undefined` for a stale handle.
    ResourceState StateOf(BufferHandle buffer) const;

    /// Returns the tracked state of a texture, for test inspection.
    ///
    /// Param texture: a live texture.
    /// Returns: its state, or `Undefined` for a stale handle.
    ResourceState StateOf(TextureHandle texture) const;

    /// Makes the device behave as lost: later submits fail and `DeviceLost()` reports true.
    void SimulateDeviceLoss();

    /// Makes the swapchain report `Lost` from now on, as if the window's surface had been taken away.
    ///
    /// A device without a swapchain ignores it.
    void SimulateSurfaceLoss();

    bool RecreateSurface(const os::Window* window) override;

private:
    UniquePtr<detail::NullDeviceState> state_;
};

/// Creates a null device.
///
/// Param options: limits and behavior; the defaults are the RHI-guaranteed minimums.
/// Returns: the device; never empty.
/// Aborts: the host is out of memory.
/// Ownership: the caller owns the device through the returned pointer.
NullDevicePtr CreateNullDevice(const NullDeviceOptions& options = NullDeviceOptions());

} // namespace engine_placeholder::rhi
