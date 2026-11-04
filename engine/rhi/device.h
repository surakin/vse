#pragma once

#include "rhi/caps.h"
#include "rhi/command_list.h"
#include "rhi/desc.h"
#include "rhi/handle.h"
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
/// Ownership: the caller owns the device and releases it with [[rhi::DestroyDevice]], after `WaitIdle`
///   and after dropping every command list.
class Device {
public:
    virtual ~Device() = default;

    /// Returns the stable lowercase backend id, for logs and tests.
    ///
    /// Returns: `"null"` or `"vulkan"`.
    /// Ownership: the string is static and is never freed by the caller.
    virtual const char* BackendName() const = 0;

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
    /// Destroys a bind group.
    ///
    /// Param group: a live group handle.
    /// Aborts: `group` is invalid or stale.
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

    /// Returns whether the device was lost; once true it stays true.
    ///
    /// Returns: true after a device-lost error.
    virtual bool DeviceLost() const = 0;
};

/// Destroys a device created by a backend's factory function, after waiting for it to go idle.
///
/// Param device: the device, or null for no effect.
/// Ownership: takes ownership of `device`; the pointer, its command lists and every handle are invalid afterwards.
void DestroyDevice(Device* device);

} // namespace engine_placeholder::rhi
