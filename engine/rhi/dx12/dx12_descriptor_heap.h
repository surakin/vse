#pragma once

#include "sync/atomic.h"

#include "containers/vector.h"
#include "rhi/dx12/com_ref.h"
#include "rhi/dx12/d3d12_api.h"
#include "types.h"

//! CPU (non-shader-visible) descriptor heaps for the views a resource gets when it is created (Phase 13 Day 165).

namespace engine_placeholder::rhi::dx12 {

/// Hands out single descriptors of one heap type from a growing list of non-shader-visible heaps, with a free list.
///
/// A resource's views are made when the resource is created and freed with the resource's deferred destruction, so a freed
/// descriptor is no longer referenced by any command list in flight. Bind groups copy from these into the shader-visible ring
/// (Day 166).
///
/// - Thread-safety: not-thread-safe; the device calls it under its resource lock.
/// - Ownership: owns the heaps.
class CpuDescriptorHeap {
public:
    /// How many descriptors one heap holds; the allocator adds heaps of this size as it runs out.
    static constexpr u32 kDescriptorsPerHeap = 1024;

    /// Sets the device and the heap type; no heap is created until the first allocation.
    ///
    /// - Param device: the device; borrowed for the allocator's life.
    /// - Param type: the heap type (CBV/SRV/UAV, sampler, RTV or DSV).
    void Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type);

    /// Returns a free descriptor.
    ///
    /// - Param out: receives the CPU handle.
    /// - Returns: true on success.
    /// - Fails: returns false if a new heap cannot be created (out of memory).
    bool Allocate(D3D12_CPU_DESCRIPTOR_HANDLE* out);

    /// Gives a descriptor back; a null handle (`ptr` 0) is ignored.
    ///
    /// - Param handle: a handle from `Allocate`.
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE handle);

    /// Returns how many descriptors are handed out.
    u32 Used() const { return used_; }
    /// Returns how many heaps exist.
    usize HeapCount() const { return heaps_.Size(); }

    /// Releases every heap.
    void Shutdown();

private:
    ID3D12Device* device_ = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE type_ = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    u32 increment_ = 0;
    Vector<ComRef<ID3D12DescriptorHeap>> heaps_;
    Vector<usize> free_; // descriptor addresses (D3D12_CPU_DESCRIPTOR_HANDLE::ptr) ready for reuse
    u32 next_in_last_ = kDescriptorsPerHeap; // descriptors of the newest heap handed out so far
    u32 used_ = 0;
};

/// Hands out contiguous ranges of one non-shader-visible heap type: a bind group's views sit next to each other, so `SetBindGroup`
/// copies them into the frame's ring with one `CopyDescriptorsSimple` (Phase 13 Day 166). Ranges come in power-of-two sizes, each with
/// its own free list.
///
/// - Thread-safety: not-thread-safe; the device calls it under its resource lock.
/// - Ownership: owns the heaps.
class CpuDescriptorBlocks {
public:
    /// How many descriptors one heap holds; the largest range is this size.
    static constexpr u32 kDescriptorsPerHeap = 16384;

    /// Sets the device and the heap type.
    ///
    /// - Param device: borrowed for the allocator's life.
    /// - Param type: the heap type.
    void Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type);
    /// Returns a range of at least `count` contiguous descriptors.
    ///
    /// - Param count: 1 to `kDescriptorsPerHeap`.
    /// - Param out: receives the handle of the first.
    /// - Returns: true on success.
    /// - Fails: false when `count` is out of range or a heap cannot be created.
    bool Allocate(u32 count, D3D12_CPU_DESCRIPTOR_HANDLE* out);
    /// Gives a range back.
    ///
    /// - Param first: the handle `Allocate` returned; `ptr` 0 is ignored.
    /// - Param count: the count it was asked for.
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE first, u32 count);
    /// Returns the distance between two descriptors of this type.
    u32 Increment() const { return increment_; }
    /// Releases every heap.
    void Shutdown();

private:
    static constexpr u32 kClasses = 15; // 1, 2, 4, ... 16384
    static u32 ClassOf(u32 count);
    ID3D12Device* device_ = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE type_ = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    u32 increment_ = 0;
    Vector<ComRef<ID3D12DescriptorHeap>> heaps_;
    Vector<usize> free_[kClasses];
    u32 next_in_last_ = kDescriptorsPerHeap;
};

/// The shader-visible CBV/SRV/UAV heap, split into one region per frame slot: `SetBindGroup` takes a range from the region of its
/// frame, and `BeginFrame` resets a region once the frame that last used it has finished (Phase 13 Day 166).
///
/// - Thread-safety: thread-safe for `Allocate` (command lists record on several threads); `Reset` belongs to the submission thread.
/// - Ownership: owns the heap.
class GpuDescriptorRing {
public:
    /// Creates the heap.
    ///
    /// - Param device: the device.
    /// - Param per_slot: descriptors per frame slot.
    /// - Param slots: the number of slots (the device's frame pools).
    /// - Returns: true on success.
    /// - Fails: false if the heap cannot be created.
    bool Initialize(ID3D12Device* device, u32 per_slot, u32 slots);
    /// Takes `count` descriptors from a slot's region.
    ///
    /// - Param slot: the frame slot.
    /// - Param count: descriptors wanted.
    /// - Param cpu: receives the CPU handle of the first.
    /// - Param gpu: receives its GPU handle.
    /// - Returns: true on success.
    /// - Fails: false when the region is full (the caller logs the sizes).
    bool Allocate(u32 slot, u32 count, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu);
    /// Empties a slot's region.
    ///
    /// - Param slot: the frame slot.
    void Reset(u32 slot);
    /// Returns how many descriptors of a slot are used.
    u32 Used(u32 slot) const { return slot < kMaxSlots ? used_[slot].Load(MemoryOrder::Relaxed) : 0; }
    /// Returns the descriptors per slot.
    u32 PerSlot() const { return per_slot_; }
    /// Returns the heap. Ownership: borrowed.
    ID3D12DescriptorHeap* Heap() const { return heap_.Get(); }
    /// Releases the heap.
    void Shutdown() { heap_.Reset(); }

private:
    static constexpr u32 kMaxSlots = 4;
    ComRef<ID3D12DescriptorHeap> heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_{};
    u32 increment_ = 0;
    u32 per_slot_ = 0;
    Atomic<u32> used_[kMaxSlots] = {};
};

/// Persistent ranges of the shader-visible sampler heap (2048 descriptors at most, D3D12's limit), first fit with coalescing on free:
/// a bind group's samplers are copied once at creation, and groups with the same samplers share a table (Phase 13 Day 166).
///
/// - Thread-safety: not-thread-safe; the device calls it under its resource lock.
/// - Ownership: owns the heap.
class GpuDescriptorTables {
public:
    /// The size of the shader-visible sampler heap.
    static constexpr u32 kCapacity = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
    /// Creates the heap.
    ///
    /// - Param device: the device.
    /// - Returns: true on success.
    /// - Fails: false if the heap cannot be created.
    bool Initialize(ID3D12Device* device);
    /// Takes a range of `count` descriptors.
    ///
    /// - Param count: descriptors wanted.
    /// - Param offset: receives the first descriptor's index.
    /// - Returns: true on success.
    /// - Fails: false when no free range is large enough.
    bool Allocate(u32 count, u32* offset);
    /// Gives a range back.
    ///
    /// - Param offset: from `Allocate`.
    /// - Param count: the count it was asked for.
    void Free(u32 offset, u32 count);
    /// Returns the CPU handle of a descriptor.
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(u32 offset) const { return D3D12_CPU_DESCRIPTOR_HANDLE{cpu_start_.ptr + static_cast<usize>(offset) * increment_}; }
    /// Returns the GPU handle of a descriptor.
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(u32 offset) const { return D3D12_GPU_DESCRIPTOR_HANDLE{gpu_start_.ptr + static_cast<u64>(offset) * increment_}; }
    /// Returns how many descriptors are free.
    u32 FreeCount() const;
    /// Returns the heap. Ownership: borrowed.
    ID3D12DescriptorHeap* Heap() const { return heap_.Get(); }
    /// Releases the heap.
    void Shutdown() {
        heap_.Reset();
        ranges_.Clear();
    }

private:
    struct Range {
        u32 offset;
        u32 count;
    };
    ComRef<ID3D12DescriptorHeap> heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_{};
    u32 increment_ = 0;
    Vector<Range> ranges_; // free ranges, sorted by offset, never adjacent
};

} // namespace engine_placeholder::rhi::dx12
