#pragma once

#include <mutex>

#include "rhi/enums.h"
#include "rhi/vulkan/vk_array.h"
#include "types.h"

//! GPU memory suballocation: large blocks carved by a free list, so `vkAllocateMemory` is called rarely.
//!
//! There is no Vulkan type in this header's logic: the allocator talks to a [[rhi::vulkan::MemoryBackend]], which
//! the Vulkan device implements and the tests replace with a fake.

namespace engine_placeholder::rhi::vulkan {

/// The most memory types and heaps the allocator tracks (the Vulkan limits).
inline constexpr u32 kMaxMemoryTypes = 32;
/// The most memory heaps the allocator tracks (the Vulkan limit).
inline constexpr u32 kMaxMemoryHeaps = 16;

/// One memory type of the device, as plain data.
///
/// Thread-safety: thread-compatible plain data.
struct MemoryTypeInfo {
    /// Index of the heap this type allocates from.
    u32 heap_index = 0;
    /// Whether the memory is local to the GPU.
    bool device_local = false;
    /// Whether the CPU can map it.
    bool host_visible = false;
    /// Whether CPU writes and GPU writes are visible to each other without explicit flushes.
    bool host_coherent = false;
    /// Whether CPU reads are cached (fast to read back).
    bool host_cached = false;
};

/// One memory heap of the device, as plain data.
///
/// Thread-safety: thread-compatible plain data.
struct MemoryHeapInfo {
    /// Heap size in bytes.
    u64 size = 0;
    /// Whether the heap is GPU-local memory.
    bool device_local = false;
};

/// The device's memory layout, as plain data.
///
/// Thread-safety: thread-compatible plain data.
struct MemoryLayout {
    /// The memory types; only the first `type_count` are valid.
    MemoryTypeInfo types[kMaxMemoryTypes];
    /// Number of valid entries in `types`.
    u32 type_count = 0;
    /// The heaps; only the first `heap_count` are valid.
    MemoryHeapInfo heaps[kMaxMemoryHeaps];
    /// Number of valid entries in `heaps`.
    u32 heap_count = 0;
};

/// Picks the memory type for a resource.
///
/// Preferences, each falling back to the next: `GpuOnly` takes device-local memory that is not host-visible, then any
/// device-local memory, then anything (unified memory); `CpuToGpu` takes host-visible coherent memory that is not
/// device-local, then host-visible coherent device-local memory (resizable BAR), then any host-visible memory;
/// `GpuToCpu` takes host-visible cached coherent, then cached, then coherent, then any host-visible memory.
///
/// Param layout: the device's memory types.
/// Param location: where the resource should live.
/// Param type_bits: the resource's `memoryTypeBits`; bit `i` set means type `i` is allowed.
/// Returns: the type index, or -1 if no allowed type satisfies even the weakest preference.
/// Fails: returns -1 for a `CpuToGpu` or `GpuToCpu` request when no allowed type is host-visible.
int SelectMemoryType(const MemoryLayout& layout, MemoryLocation location, u32 type_bits);

/// The outcome of a memory allocation attempt.
enum class MemoryResult : u8 {
    Ok = 0,          ///< Allocated.
    OutOfDeviceMemory, ///< The device (GPU) memory is exhausted.
    OutOfHostMemory, ///< The host (CPU) memory is exhausted.
    Failed,          ///< Any other failure: no suitable memory type, the allocation-count limit, or a driver error.
};

/// What a [[rhi::vulkan::MemoryBackend]] allocates for.
///
/// Thread-safety: thread-compatible plain data.
struct BackendAllocation {
    /// Backend-specific handle of the memory object; opaque to the allocator.
    u64 handle = 0;
    /// The mapped pointer of the whole object if it was asked to be mapped, else null.
    void* mapped = nullptr;
};

/// The allocator's source of real memory objects.
///
/// Thread-safety: thread-compatible; the allocator serializes its calls, so an implementation needs no locking of its own.
class MemoryBackend {
public:
    virtual ~MemoryBackend() = default;

    /// Allocates one memory object.
    ///
    /// Param memory_type: the memory type index.
    /// Param size: bytes.
    /// Param map: whether to map the whole object persistently.
    /// Param dedicated_buffer: a buffer the object is dedicated to (as an opaque handle), or 0.
    /// Param dedicated_image: an image the object is dedicated to (as an opaque handle), or 0.
    /// Param out: receives the handle and mapping on success.
    /// Returns: the outcome; `out` is only valid for `Ok`.
    virtual MemoryResult Allocate(u32 memory_type, u64 size, bool map, u64 dedicated_buffer, u64 dedicated_image,
                                  BackendAllocation* out) = 0;

    /// Frees an object returned by `Allocate`, unmapping it if it was mapped.
    ///
    /// Param allocation: the object.
    virtual void Free(const BackendAllocation& allocation) = 0;
};

/// A range of memory handed out by the allocator.
///
/// Thread-safety: thread-compatible plain data.
struct MemoryAllocation {
    /// Backend handle of the memory object containing the range; 0 for an empty allocation.
    u64 memory = 0;
    /// Byte offset of the range inside that object.
    u64 offset = 0;
    /// Size of the range in bytes.
    u64 size = 0;
    /// Pointer to the first byte, if the memory is host-visible; null otherwise.
    void* mapped = nullptr;
    /// The memory type index.
    u32 memory_type = 0;
    /// Whether host writes are visible to the GPU without a flush (and device writes to the host without an invalidate).
    bool coherent = false;
    /// Whether this range is a whole dedicated memory object.
    bool dedicated = false;
    /// Which pool the range came from (allocator bookkeeping).
    u32 pool = 0;
    /// The block's index in its pool (allocator bookkeeping).
    u32 block = 0;

    /// Returns whether this holds memory (an allocation failure yields an empty one).
    bool Valid() const { return size != 0; }
};

/// What to allocate.
///
/// Thread-safety: thread-compatible plain data.
struct MemoryRequest {
    /// Bytes needed.
    u64 size = 0;
    /// Required alignment in bytes; a power of two.
    u64 alignment = 1;
    /// The resource's `memoryTypeBits`.
    u32 memory_type_bits = ~0u;
    /// Where the resource should live.
    MemoryLocation location = MemoryLocation::GpuOnly;
    /// True for buffers and linear-tiled images, false for optimal-tiled images. The two never share a memory block,
    /// which satisfies `bufferImageGranularity` without tracking it per neighbor.
    bool linear = true;
    /// Allocate a dedicated memory object (the driver asked for one, or the caller prefers it).
    bool dedicated = false;
    /// For a dedicated allocation, the buffer it is for (opaque handle), or 0.
    u64 dedicated_buffer = 0;
    /// For a dedicated allocation, the image it is for (opaque handle), or 0.
    u64 dedicated_image = 0;
};

/// Tunables of the allocator.
///
/// Thread-safety: thread-compatible plain data.
struct MemoryAllocatorConfig {
    /// Size of a block, in bytes.
    u64 block_size = 64ull << 20;
    /// Size of a block in a device-local heap at least `large_heap_bytes` big.
    u64 large_block_size = 256ull << 20;
    /// Heap size from which `large_block_size` is used.
    u64 large_heap_bytes = 8ull << 30;
    /// Requests larger than this fraction of a block (as a divisor: `block / dedicated_divisor`) get their own memory object.
    u32 dedicated_divisor = 2;
    /// The device's `maxMemoryAllocationCount`.
    u32 max_allocation_count = 4096;
    /// Memory objects kept in reserve below the limit for allocations made outside the allocator (swapchain images,
    /// driver-internal objects); the allocator itself, dedicated objects included, stops at `max_allocation_count - headroom`.
    u32 allocation_headroom = 64;
    /// The device's `nonCoherentAtomSize`: ranges in host-visible but non-coherent memory are aligned and sized to a
    /// multiple of it, so flushing or invalidating one never touches a neighbor.
    u64 non_coherent_atom_size = 256;
};

/// Usage numbers for one heap.
///
/// Thread-safety: thread-compatible plain data (a snapshot).
struct HeapStats {
    /// Bytes in memory objects allocated from the heap (blocks and dedicated objects).
    u64 allocated_bytes = 0;
    /// Bytes handed out to resources.
    u64 used_bytes = 0;
    /// Number of suballocation blocks.
    u32 block_count = 0;
    /// Number of dedicated memory objects.
    u32 dedicated_count = 0;
    /// Free bytes inside blocks.
    u64 free_bytes = 0;
    /// The largest single free range inside any block.
    u64 largest_free_bytes = 0;
    /// Estimate of fragmentation: 0 when the free space is one range, approaching 1 when it is in many small pieces.
    f32 fragmentation = 0.0f;
};

/// A snapshot of the allocator's state.
///
/// Thread-safety: thread-compatible plain data (a snapshot).
struct MemoryStats {
    /// Per-heap numbers; the first `heap_count` entries are valid.
    HeapStats heaps[kMaxMemoryHeaps];
    /// Number of valid entries in `heaps`.
    u32 heap_count = 0;
    /// Memory objects currently allocated from the backend.
    u32 live_memory_objects = 0;
    /// Memory objects ever allocated from the backend.
    u64 total_memory_objects_made = 0;
};

/// The free ranges of one memory block, with coalescing; the core of the allocator, exposed for testing.
///
/// Thread-safety: thread-compatible. Not synchronized.
class FreeList {
public:
    /// Creates a list with one free range covering `[0, size)`.
    explicit FreeList(u64 size);

    /// Takes `size` bytes aligned to `alignment` from the best-fitting free range (the smallest that fits).
    ///
    /// Param size: bytes to take; must be non-zero.
    /// Param alignment: a power of two.
    /// Param out_offset: receives the offset of the range.
    /// Returns: true on success.
    /// Fails: returns false if no free range is big enough.
    bool Allocate(u64 size, u64 alignment, u64* out_offset);

    /// Returns a range to the list, merging it with free neighbors.
    ///
    /// Param offset: the offset returned by `Allocate`.
    /// Param size: the size passed to `Allocate`.
    /// Returns: true on success.
    /// Fails: returns false, changing nothing, if the range overlaps free space already in the list (a double free) or
    ///   lies outside the block.
    bool Free(u64 offset, u64 size);

    /// Returns the block size.
    u64 Capacity() const { return capacity_; }
    /// Returns the total free bytes.
    u64 FreeBytes() const { return free_bytes_; }
    /// Returns the size of the largest free range, 0 if none.
    u64 LargestFree() const;
    /// Returns the number of separate free ranges.
    usize RangeCount() const { return ranges_.Size(); }
    /// Returns the offset of free range `i` (ranges are ordered by offset).
    u64 RangeOffset(usize i) const { return ranges_[i].offset; }
    /// Returns the size of free range `i`.
    u64 RangeSize(usize i) const { return ranges_[i].size; }

private:
    struct Range {
        u64 offset;
        u64 size;
    };
    detail::GrowArray<Range> ranges_;
    u64 capacity_;
    u64 free_bytes_;
};

/// Carves device memory into suballocations from large blocks.
///
/// Per memory type there are two pools (linear and optimal resources), each a list of blocks managed by a
/// [[rhi::vulkan::FreeList]]. Requests larger than a fraction of the block size, and requests marked dedicated, get
/// their own memory object. An emptied block is released unless it is the only empty block of its pool (one spare is
/// kept). Host-visible blocks are mapped once, when created.
///
/// Failure policy: allocation never aborts. It returns an empty [[rhi::vulkan::MemoryAllocation]] and reports
/// whether the device or the host ran out of memory, so the resource creation above it can fail with a log line.
/// Freeing an allocation twice, or one that is not from this allocator, is a programming error and aborts.
///
/// Thread-safety: thread-safe. Calls are serialized by an internal mutex, including the backend's.
/// Ownership: borrows the backend, which must outlive it; frees every block on destruction.
class MemoryAllocator {
public:
    /// Creates an allocator over `layout` using `backend`.
    ///
    /// Param layout: the device's memory types and heaps.
    /// Param config: tunables.
    /// Param backend: where memory objects come from; must outlive the allocator.
    MemoryAllocator(const MemoryLayout& layout, const MemoryAllocatorConfig& config, MemoryBackend* backend);
    /// Frees every block and dedicated object still held.
    ~MemoryAllocator();
    MemoryAllocator(const MemoryAllocator&) = delete;
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;

    /// Allocates memory for a resource.
    ///
    /// Param request: what is needed. In non-coherent host-visible memory the returned range is rounded up to the atom size.
    /// Param result: receives the outcome; may be null.
    /// Returns: the allocation, empty on failure.
    /// Fails: an empty allocation, with the reason logged on `LogRhi` and given in `result`: no allowed memory type, the
    ///   allocation-count limit, or the backend failing. Exhaustion logs the per-heap breakdown.
    MemoryAllocation Allocate(const MemoryRequest& request, MemoryResult* result);

    /// Returns an allocation's range.
    ///
    /// Param allocation: an allocation returned by `Allocate`; empty ones are ignored.
    /// Aborts: the range was already freed or is not from this allocator.
    void Free(const MemoryAllocation& allocation);

    /// Returns a snapshot of the allocator's usage.
    MemoryStats Stats() const;

    /// Logs the per-heap usage on `LogRhi` at `Warn` level, as done on exhaustion.
    void LogStats() const;

private:
    struct Block {
        u64 memory;
        void* mapped;
        u64 size;
        u64 used;
        FreeList* free_list;
        bool alive;
    };
    struct Pool {
        detail::GrowArray<Block*> blocks;
    };

    MemoryAllocation AllocateLocked(const MemoryRequest& request, MemoryResult* result);
    void FreeLocked(const MemoryAllocation& allocation);
    MemoryStats StatsLocked() const;
    void LogStatsLocked() const;
    u64 BlockSizeFor(u32 memory_type) const;
    static u32 PoolIndex(u32 memory_type, bool linear) { return memory_type * 2 + (linear ? 1u : 0u); }

    MemoryLayout layout_;
    MemoryAllocatorConfig config_;
    MemoryBackend* backend_;
    mutable std::mutex mutex_;
    Pool pools_[kMaxMemoryTypes * 2];
    struct Dedicated {
        u64 memory;
        void* mapped;
        u64 size;
        u32 heap;
    };
    detail::GrowArray<Dedicated> dedicated_; // every live dedicated object, so teardown frees them and a double free is caught
    u64 dedicated_bytes_[kMaxMemoryHeaps] = {};
    u32 dedicated_count_[kMaxMemoryHeaps] = {};
    u32 live_objects_ = 0;
    u64 objects_made_ = 0;
};

} // namespace engine_placeholder::rhi::vulkan
