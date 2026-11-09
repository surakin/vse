#pragma once

#include "sync/atomic.h"

#include "rhi/device.h"

//! A per-frame bump allocator for small, frequently changing uniform data.

namespace engine_placeholder::rhi {

/// One allocation from a [[rhi::UniformRing]].
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `data` points into the ring's mapped buffer and is valid for the frame the allocation was made in.
struct UniformAllocation {
    /// The ring's buffer; invalid if the allocation failed. Use it as the `buffer` of a bind group entry.
    BufferHandle buffer;
    /// Byte offset of the allocation in `buffer`; a multiple of the device's uniform offset alignment.
    u64 offset = 0;
    /// Byte size of the allocation as requested (the ring reserves it rounded up to the alignment).
    u64 size = 0;
    /// Where to write the data (CPU pointer to `size` bytes); null if the allocation failed.
    void* data = nullptr;

    /// Returns whether the allocation succeeded.
    /// Fails: never; a failed allocation is the one for which this returns false.
    bool Valid() const { return data != nullptr; }
};

/// Hands out aligned slices of a persistently mapped `CpuToGpu` buffer, a fresh region each frame.
///
/// The ring owns one buffer of `kSlots` equal regions. [[rhi::UniformRing::BeginFrame]] picks the region for the
/// current frame (`FrameNumber() % kSlots`) and empties it; `Allocate` then bumps a pointer through it. Because a region is
/// reused only every `kSlots` frames and a device runs at most three frames in flight, the GPU is done with a region
/// before the CPU writes it again. Put an allocation in a bind group entry with `offset` and `size`, or bind the whole
/// buffer once and use the offsets in your own shader indexing.
///
/// Thread-safety: thread-compatible. `Allocate` is thread-safe and lock-free; `BeginFrame` and the destructor must not run
///   concurrently with anything else.
/// Ownership: the ring owns its buffer and destroys it in the destructor; the device must outlive the ring.
class UniformRing {
public:
    /// The number of per-frame regions; at least the largest frames-in-flight the device allows.
    static constexpr u32 kSlots = 3;

    /// Creates the buffer.
    ///
    /// Param device: the device; must outlive the ring.
    /// Param bytes_per_frame: capacity of one frame's region in bytes; rounded up to the offset alignment.
    /// Ownership: the ring creates and owns its buffer; check [[rhi::UniformRing::Valid]].
    UniformRing(Device* device, u64 bytes_per_frame);
    /// Unmaps and destroys the buffer.
    ~UniformRing();
    UniformRing(const UniformRing&) = delete;
    UniformRing& operator=(const UniformRing&) = delete;

    /// Returns whether the buffer was created and mapped.
    /// Fails: never; false means creation failed (logged) and every allocation will fail.
    bool Valid() const { return mapped_ != nullptr; }

    /// Starts the current frame's region: call it once per frame, after `Device::BeginFrame` and before any `Allocate`.
    /// Aborts: no frame is open.
    void BeginFrame();

    /// Reserves `size` bytes, aligned to the device's uniform buffer offset alignment.
    ///
    /// Param size: bytes wanted, greater than zero.
    /// Returns: the allocation, or an invalid one if the region is full (logged once per frame) or the ring is not valid.
    /// Fails: returns an invalid allocation; nothing is reserved.
    /// Thread-safety: thread-safe.
    UniformAllocation Allocate(u64 size);

    /// Makes this frame's writes visible to the GPU: call it once per frame, after the last `Allocate` was written and
    /// before submitting the work that reads them.
    ///
    /// Does nothing on devices with coherent host memory. Not thread-safe with `Allocate`.
    void Flush();

    /// Returns the capacity of one frame's region in bytes.
    u64 BytesPerFrame() const { return region_bytes_; }
    /// Returns the bytes reserved so far in the current frame's region, including alignment padding.
    u64 BytesUsed() const { return used_.Load(MemoryOrder::Relaxed); }
    /// Returns the buffer all allocations come from.
    BufferHandle Buffer() const { return buffer_; }

private:
    Device* device_;
    BufferHandle buffer_;
    byte* mapped_ = nullptr;
    u64 region_bytes_ = 0;
    u64 alignment_ = 256;
    u64 region_base_ = 0;
    Atomic<u64> used_{0};
    Atomic<bool> reported_full_{false};
};

} // namespace engine_placeholder::rhi
