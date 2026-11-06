#pragma once

#include "containers/unique_ptr.h"
#include "mem/heap_allocator.h"
#include "os/window.h"
#include "rhi/adapter.h"
#include "rhi/caps.h"
#include "rhi/command_list.h"
#include "rhi/desc.h"
#include "rhi/handle.h"
#include "rhi/swapchain.h"
#include "types.h"

//! The device: creates and destroys GPU resources and submits recorded work.

namespace engine_placeholder::rhi {

/// A point on the device's timeline; later submissions have larger values. 0 means none.
using FenceValue = u64;

/// Identifies an upload so the caller can wait for it to finish on the GPU.
///
/// Thread-safety: thread-compatible plain data.
struct UploadTicket {
    /// The device fence value reached when the upload has finished; 0 means it had already finished when the call returned.
    /// If `ok` is false, it is the fence of the last part that was submitted before the failure (or 0).
    FenceValue fence = 0;
    /// False if the upload failed (lost device, staging memory that never freed up); the destination then holds only
    /// the parts that were submitted, and the failure was logged.
    bool ok = true;
};

/// How many objects of each kind a device holds, for leak checks.
///
/// Counts live objects only; resources already destroyed but waiting for the GPU (deferred destruction) are not counted,
/// so call [[rhi::Device::Quiesce]] first for an exact answer.
///
/// Thread-safety: thread-compatible plain data (a snapshot).
struct ResourceCounts {
    /// Live buffers.
    usize buffers = 0;
    /// Live textures, including swapchain images.
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
    /// Live swapchains (0 or 1).
    usize swapchains = 0;
    /// Command lists handed out and not yet recycled (null backend; the Vulkan backend recycles its lists per frame and reports 0).
    usize command_lists = 0;
    /// Backend-internal objects the application cannot see (staging buffers, rings); 0 on the null backend.
    usize internal_objects = 0;
    /// Bytes of device memory currently allocated by the device (0 on the null backend).
    u64 device_memory_bytes = 0;

    /// Returns the sum of the counts of objects an application creates (buffers to swapchains and command lists).
    /// Fails: never.
    usize UserObjects() const {
        return buffers + textures + samplers + shaders + bind_group_layouts + bind_groups + pipelines + swapchains + command_lists;
    }
};

/// Hands out a new process-wide device serial, starting at 1.
///
/// Thread-safety: thread-safe.
u32 AllocateDeviceSerial();

/// Returns the first handle generation for pools of the device with this serial: `(serial & 0xFFF) << 20 | 1`.
///
/// Handles of two devices then differ in their top 12 bits, so a handle kept across a device re-creation is detected as
/// stale instead of aliasing a new resource. The limits: a slot reused over 2^20 times could reach another device's
/// range, and the serial wraps after 4096 devices in one process.
///
/// Param serial: from [[rhi::AllocateDeviceSerial]].
/// Returns: the seed for `HandlePool::SeedGeneration`.
constexpr u32 GenerationSeed(u32 serial) { return ((serial & 0xFFFu) << 20) | 1u; }

/// The GPU device: owner of every resource, factory of command lists, and the submission point.
///
/// Creation functions never abort on an environment problem: they log the reason on [[rhi::LogRhi]] and
/// return the invalid handle. Passing a stale or foreign handle, or violating a documented precondition, is
/// a programming error (`Aborts:`); the null backend counts such errors instead of aborting.
///
/// `Destroy*` makes the handle invalid at once but defers the actual release until the GPU has finished
/// the frames that could use the resource.
///
/// Thread-safety: thread-safe for resource creation, destruction, mapping and `CreateCommandList`
///   (serialized internally). `Submit`, `BeginFrame`, `EndFrame` and `WaitIdle` must be called from one
///   thread at a time (the submission thread).
/// Ownership: the caller owns the device through a [[rhi::DevicePtr]], which waits for the GPU to go
///   idle and destroys it; drop every command list first.
class Device {
public:
    virtual ~Device() = default;

    /// Returns the stable lowercase backend id, for logs and tests.
    ///
    /// Returns: `"null"` or `"vulkan"`.
    /// Ownership: the string is static and is never freed by the caller.
    virtual const char* BackendName() const = 0;

    /// Returns the adapter (GPU) the device was created on.
    ///
    /// Ownership: the reference is valid for the life of the device.
    virtual const AdapterInfo& Adapter() const = 0;

    /// Returns the device's serial: unique in the process, the same for the life of the device. Handle generations are
    /// seeded from it (see [[rhi::GenerationSeed]]).
    virtual u32 Epoch() const = 0;

    /// Returns the device limits.
    /// Ownership: the reference is valid for the life of the device.
    virtual const DeviceCaps& Caps() const = 0;

    /// Creates a buffer.
    ///
    /// Param desc: the description; must pass `Validate()` and fit the device limits.
    /// Returns: the buffer handle, or the invalid handle on failure.
    /// Fails: an invalid description or an out-of-memory condition is logged and returns the invalid handle.
    virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
    /// Destroys a buffer; the handle is invalid immediately and the memory is released after the GPU is done.
    ///
    /// Param buffer: a live buffer handle.
    /// Aborts: `buffer` is invalid, stale, or still mapped.
    virtual void DestroyBuffer(BufferHandle buffer) = 0;

    /// Creates a texture in state `Undefined`.
    ///
    /// Param desc: the description; must pass `Validate()` and fit the device limits.
    /// Returns: the texture handle, or the invalid handle on failure.
    /// Fails: an invalid description, an unsupported format or an out-of-memory condition is logged and returns the invalid handle.
    virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
    /// Destroys a texture; the handle is invalid immediately and the memory is released after the GPU is done.
    ///
    /// Param texture: a live texture handle.
    /// Aborts: `texture` is invalid or stale.
    virtual void DestroyTexture(TextureHandle texture) = 0;

    /// Creates a sampler.
    ///
    /// Equal descriptors may share one handle: a backend can return the handle it made for an earlier equal request
    /// and count the references, so destroy a sampler once for every time you created it. The anisotropy is clamped
    /// to the device limit.
    ///
    /// Param desc: the description; must pass `Validate()`.
    /// Returns: the sampler handle, or the invalid handle on failure.
    /// Fails: an invalid description is logged and returns the invalid handle.
    virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
    /// Destroys a sampler.
    ///
    /// Param sampler: a live sampler handle.
    /// Aborts: `sampler` is invalid or stale.
    virtual void DestroySampler(SamplerHandle sampler) = 0;

    /// Creates a shader module from SPIR-V.
    ///
    /// Param desc: the description; the words are copied before the call returns.
    /// Returns: the shader handle, or the invalid handle on failure.
    /// Fails: an invalid description or a rejected module is logged and returns the invalid handle.
    virtual ShaderHandle CreateShader(const ShaderDesc& desc) = 0;
    /// Destroys a shader module; pipelines created from it stay valid.
    ///
    /// Param shader: a live shader handle.
    /// Aborts: `shader` is invalid or stale.
    virtual void DestroyShader(ShaderHandle shader) = 0;

    /// Creates a bind group layout.
    ///
    /// Param desc: the description; entries are copied before the call returns.
    /// Returns: the layout handle, or the invalid handle on failure.
    /// Fails: an invalid description is logged and returns the invalid handle.
    virtual BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc& desc) = 0;
    /// Destroys a bind group layout; pipelines and groups created from it stay valid.
    ///
    /// Param layout: a live layout handle.
    /// Aborts: `layout` is invalid or stale.
    virtual void DestroyBindGroupLayout(BindGroupLayoutHandle layout) = 0;

    /// Creates a bind group.
    ///
    /// Param desc: the layout and one entry per binding element; entries are copied before the call returns.
    /// Returns: the group handle, or the invalid handle on failure.
    /// Fails: an invalid description, a stale resource, or entries that do not match the layout are logged and
    ///   return the invalid handle.
    virtual BindGroupHandle CreateBindGroup(const BindGroupDesc& desc) = 0;
    /// Destroys a persistent bind group; the descriptors are recycled once the GPU has finished the frames that could
    /// have used it. A transient group is never destroyed by the application: the device releases it.
    ///
    /// Param group: a live group handle made without `BindGroupDesc::transient`.
    /// Aborts: `group` is invalid or stale, or is transient.
    virtual void DestroyBindGroup(BindGroupHandle group) = 0;

    /// Creates a graphics pipeline.
    ///
    /// Param desc: the description; arrays are copied before the call returns.
    /// Returns: the pipeline handle, or the invalid handle on failure.
    /// Fails: an invalid description, a stale shader or layout, or a driver rejection is logged and returns the invalid handle.
    virtual PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    /// Creates a compute pipeline.
    ///
    /// Param desc: the description.
    /// Returns: the pipeline handle, or the invalid handle on failure.
    /// Fails: an invalid description, a stale shader or layout, or a driver rejection is logged and returns the invalid handle.
    virtual PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
    /// Destroys a pipeline of either kind.
    ///
    /// Param pipeline: a live pipeline handle.
    /// Aborts: `pipeline` is invalid or stale.
    virtual void DestroyPipeline(PipelineHandle pipeline) = 0;

    /// Creates the swapchain for the window the device was created with.
    ///
    /// Param desc: the presentation mode and initial size.
    /// Returns: the swapchain, or null.
    /// Fails: returns null, logging the reason, if the device was created without a window, already has a swapchain,
    ///   or the surface cannot be presented to.
    /// Ownership: the device owns the swapchain; give it back with `DestroySwapchain`.
    virtual Swapchain* CreateSwapchain(const SwapchainDesc& desc) = 0;

    /// Destroys a swapchain after the GPU is done with its images.
    ///
    /// Param swapchain: a swapchain from `CreateSwapchain`, or null for no effect.
    /// Ownership: takes back the swapchain; it and its textures are invalid afterwards.
    virtual void DestroySwapchain(Swapchain* swapchain) = 0;

    /// Tells whether textures of a format can be created with the given usages on this device.
    ///
    /// Param format: the texel format.
    /// Param usage: the usage flags wanted together; `None` asks whether the format exists at all.
    /// Returns: true if `CreateTexture` accepts the combination.
    /// Fails: returns false for `Format::Undefined` or an unsupported combination.
    virtual bool FormatSupport(Format format, TextureUsage usage) const = 0;

    /// Tells whether [[rhi::CommandList::GenerateMips]] works for a format: the device must blit it with linear filtering.
    ///
    /// Param format: the texel format.
    /// Returns: true if mips can be generated; otherwise upload precomputed mips with `UploadTexture`.
    virtual bool CanGenerateMips(Format format) const = 0;

    /// Copies one mip level of one layer from the CPU into a texture, outside the frame's command lists.
    ///
    /// The data is staged and copied on the GPU; it has been read from `data` when the call returns, and the copy is
    /// ordered after earlier submitted work. The whole texture moves to `ShaderRead`: it must be in state `Undefined`
    /// (the first upload) or `ShaderRead` (uploading another level or layer), and the other levels keep their contents.
    /// The device remembers only what its own upload and readback calls did: if you moved the texture elsewhere with
    /// `Barrier` in a command list, move it back to `ShaderRead` before uploading again, because the device cannot see
    /// the list's barriers. Large levels are split to fit the staging ring. Uploads to one texture must not overlap in
    /// time (use one thread per texture). Depth-stencil formats with a stencil aspect cannot be uploaded.
    ///
    /// Param dst: a texture created with `Sampled` and `CopyDst` usage.
    /// Param mip: the mip level.
    /// Param layer: the array layer.
    /// Param data: the texel rows of the level; must not be null.
    /// Param row_pitch: bytes between rows (block rows for compressed formats); 0 means tightly packed. Otherwise at
    ///   least the row size and a multiple of the texel (block) size.
    /// Returns: a ticket; see [[rhi::Device::UploadBuffer]].
    /// Fails: a lost device or staging memory running out logs and returns a ticket with `ok` false; the texture is
    ///   then left in `ShaderRead` with the level only partly written.
    /// Aborts: `dst` is invalid or stale, lacks usage, `mip` or `layer` is out of range, `data` is null, `row_pitch` is
    ///   invalid, or the texture is in another state.
    /// Ownership: `data` is borrowed for the call only; it must hold the level's rows at `row_pitch`.
    virtual UploadTicket UploadTexture(TextureHandle dst, u32 mip, u32 layer, const void* data, u32 row_pitch) = 0;

    /// Copies one mip level of one layer of a texture back to the CPU and waits (a synchronous helper for tests and tools).
    ///
    /// You say which state the texture is in (the device cannot see barriers recorded in command lists); it is moved to
    /// `CopySrc` for the copy and back, so it is left in the same state. Rows are tightly packed in `dst`.
    ///
    /// Param src: a texture created with `CopySrc` usage.
    /// Param mip: the mip level.
    /// Param layer: the array layer.
    /// Param dst: where to write the level; must not be null.
    /// Param dst_size: bytes available in `dst`; at least the level's size.
    /// Param state: the state the texture is in now; not `Undefined`.
    /// Returns: true on success.
    /// Fails: returns false, logging the reason, if the device is lost or memory is exhausted.
    /// Aborts: `src` is invalid or stale, lacks `CopySrc`, `mip` or `layer` is out of range, `dst` is null or too small,
    ///   `state` is `Undefined` or not a texture state, or (on the null backend) the texture is not in `state`.
    /// Ownership: `dst` is borrowed for the call only.
    virtual bool ReadbackTexture(TextureHandle src, u32 mip, u32 layer, void* dst, u64 dst_size, ResourceState state) = 0;

    /// Maps a CPU-visible buffer and returns a pointer to its first byte.
    ///
    /// The mapping stays valid until `UnmapBuffer`. CPU writes to a `CpuToGpu` buffer are visible to work
    /// submitted afterwards.
    ///
    /// Param buffer: a buffer in `CpuToGpu` or `GpuToCpu` memory that is not already mapped.
    /// Returns: the pointer.
    /// Aborts: `buffer` is invalid or stale, not CPU-visible, or already mapped.
    /// Ownership: the memory belongs to the buffer; the pointer is valid until `UnmapBuffer` or `DestroyBuffer`.
    virtual void* MapBuffer(BufferHandle buffer) = 0;
    /// Ends a mapping.
    ///
    /// Param buffer: a buffer returned mapped by `MapBuffer`.
    /// Aborts: `buffer` is invalid, stale or not mapped.
    virtual void UnmapBuffer(BufferHandle buffer) = 0;
    /// Makes CPU writes to a mapped `CpuToGpu` buffer visible to the GPU without unmapping it.
    ///
    /// Needed only on devices whose host-visible memory is not coherent; elsewhere it does nothing. Call it before
    /// submitting work that reads the range.
    ///
    /// Param buffer: a `CpuToGpu` buffer.
    /// Param offset: first byte of the written range.
    /// Param size: bytes written; the range must lie inside the buffer.
    /// Aborts: `buffer` is invalid or stale, or the range lies outside the buffer.
    virtual void FlushMappedBuffer(BufferHandle buffer, u64 offset, u64 size) {
        (void)buffer;
        (void)offset;
        (void)size;
    }

    /// Copies bytes from the CPU into a buffer, outside the frame's command lists.
    ///
    /// The data is staged and copied on the GPU; it has been read from `data` when the call returns. The
    /// copy is ordered after all work submitted earlier and before all work submitted later, so a later `Submit` can
    /// use the new contents after a barrier from `CopyDst`. The buffer is in state `CopyDst` afterwards. Large uploads
    /// are split to fit the staging ring. Uploads from several threads are serialized.
    ///
    /// Param dst: a buffer created with `CopyDst` usage.
    /// Param offset: byte offset in `dst`.
    /// Param data: the bytes to copy; must not be null.
    /// Param size: number of bytes; the range must lie inside `dst`.
    /// A buffer in `CpuToGpu` memory is the exception: it is written at once through its mapping, with no GPU copy and
    /// no ordering against work already submitted, and it keeps its fixed state. Do not overwrite data the GPU may
    /// still be reading (use a buffer per frame in flight).
    ///
    /// Returns: a ticket; pass its `fence` to `WaitFence` to know when the GPU has finished, and check its `ok`. A failed
    ///   upload logs the reason and returns `ok` false.
    /// Fails: a lost device, or staging memory running out, logs and copies nothing.
    /// Aborts: `dst` is invalid or stale, lacks `CopyDst` usage, `data` is null, or the range is outside the buffer.
    /// Ownership: `data` is borrowed for the call only.
    virtual UploadTicket UploadBuffer(BufferHandle dst, u64 offset, const void* data, u64 size) = 0;

    /// Copies bytes from a buffer back to the CPU and waits for them (a synchronous helper for tests and tools).
    ///
    /// It waits for the GPU, so it stalls the frame; use it only for tests, tools and one-off captures. Work submitted
    /// earlier is complete and visible when it reads.
    ///
    /// Param src: a buffer created with `CopySrc` usage (or any `GpuToCpu` buffer).
    /// Param offset: byte offset in `src`.
    /// Param dst: where to write `size` bytes; must not be null.
    /// Param size: number of bytes; the range must lie inside `src`.
    /// Returns: true on success.
    /// Fails: returns false, logging the reason, if the device is lost or staging memory is exhausted.
    /// Aborts: `src` is invalid or stale, lacks `CopySrc` usage, `dst` is null, or the range is outside the buffer.
    /// Ownership: `dst` is borrowed for the call only.
    virtual bool ReadbackBuffer(BufferHandle src, u64 offset, void* dst, u64 size) = 0;

    /// Returns a command list for the calling thread to record, valid until the next `BeginFrame`.
    ///
    /// Call it between `BeginFrame` and `EndFrame`. Keep the pointer only for that frame: lists are recycled later.
    ///
    /// Returns: a list ready for `Begin`.
    /// Fails: returns null when the device is lost or out of memory.
    /// Aborts: no frame is open.
    /// Ownership: the device owns the list; do not delete it.
    virtual CommandList* CreateCommandList() = 0;

    /// Submits recorded lists for execution in the given order.
    ///
    /// Submissions are serialized and run on one queue in call order, so a later `Submit` needs no explicit wait on an
    /// earlier one. Call it from one thread at a time (it takes a mutex, but the order of calls is the order of execution).
    ///
    /// Param lists: lists obtained this frame and ended; none may repeat.
    /// Param count: number of lists; 0 submits nothing and returns the latest fence value.
    /// Ownership: the array is borrowed for the call; the device keeps ownership of the lists.
    /// Returns: the fence value that is reached when all the work has finished, or 0 on failure.
    /// Fails: returns 0 when the device is lost.
    /// Aborts: no frame is open, `lists` is null with a non-zero count, or a list was not ended, belongs to another
    ///   device, is from an earlier frame, or was already submitted.
    virtual FenceValue Submit(CommandList* const* lists, usize count) = 0;

    /// Waits until the device timeline has reached `value`.
    ///
    /// Param value: a value returned by `Submit`.
    /// Param timeout_ns: how long to wait; 0 polls.
    /// Returns: true if the value was reached.
    /// Fails: returns false on timeout or device loss.
    virtual bool WaitFence(FenceValue value, u64 timeout_ns) = 0;

    /// Returns the largest fence value the GPU has reached.
    virtual FenceValue CompletedFence() const = 0;

    /// Starts a frame: advances the frame number, recycles command lists and releases resources whose frames completed.
    ///
    /// Invalidates every command list from earlier frames.
    virtual void BeginFrame() = 0;
    /// Ends the current frame, marking it as submitted for deferred-destruction purposes.
    virtual void EndFrame() = 0;
    /// Returns the current frame number; the first frame is 1, and 0 means no frame has begun.
    virtual u64 FrameNumber() const = 0;

    /// Blocks until all submitted work has finished, then releases every deferred destruction.
    virtual void WaitIdle() = 0;

    /// Replaces the presentation surface after the window's native surface changed or was lost, keeping the device.
    ///
    /// Only possible when the adapter can present to the new surface; otherwise the caller does a full reset
    /// ([[rhi::DeviceHost::RecreateSurface]] does this).
    ///
    /// Param window: the window to present to; usually the one the device was created with.
    /// Returns: true if there is a new surface. The swapchain must be created again.
    /// Fails: returns false, logging the reason, if the surface cannot be created or the adapter cannot present to it.
    /// Aborts: the swapchain still exists, or a frame is open.
    virtual bool RecreateSurface(const os::Window* window) = 0;

    /// Waits for the GPU to go idle and drains the deferred-destruction queue; the device stays usable.
    ///
    /// Call before [[rhi::Device::Resources]] to get exact counts, and before destroying anything a reset replaces.
    ///
    /// Thread-safety: owner-thread-only, like `WaitIdle`: the submission thread.
    virtual void Quiesce() { WaitIdle(); }

    /// Returns the live object counts; see [[rhi::ResourceCounts]].
    ///
    /// Thread-safety: thread-safe.
    virtual ResourceCounts Resources() const = 0;

    /// Returns whether a frame is open (between `BeginFrame` and `EndFrame`).
    ///
    /// Fails: never.
    /// Thread-safety: thread-safe.
    virtual bool InFrame() const = 0;

    /// Makes the device behave as lost, as if the driver had reported it: later submissions are ignored and `DeviceLost` is true.
    ///
    /// A development and test aid (the demos use it to exercise recovery). The default does nothing.
    virtual void InjectDeviceLoss() {}

    /// Returns whether the device was lost; once true it stays true.
    ///
    /// Returns: true after a device-lost error.
    virtual bool DeviceLost() const = 0;
};

/// Deleter of [[rhi::DevicePtr]]: waits for the device to go idle, then destroys it.
///
/// Used for devices made by a backend's factory function (heap storage, the `Device` base first).
///
/// Thread-safety: not-thread-safe; the thread that submits to the device must call it.
/// Ownership: takes the device; it, its command lists and every handle are invalid afterwards.
struct DeviceDeleter {
    /// Waits for `device` to go idle and destroys it.
    ///
    /// Param device: the device, or null for no effect.
    /// Ownership: takes the device; it is destroyed and its storage freed.
    void operator()(Device* device) const {
        if (device != nullptr) {
            device->WaitIdle();
            device->~Device();
            HeapAllocator::Default().Free(static_cast<void*>(device), sizeof(Device), alignof(Device));
        }
    }
};

/// Owning pointer to a device; destroying or resetting it waits for the GPU and releases the device.
using DevicePtr = UniquePtr<Device, DeviceDeleter>;

} // namespace engine_placeholder::rhi
