#pragma once

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
/// Thread-safety: not-thread-safe; the device calls it under its resource lock.
/// Ownership: owns the heaps.
class CpuDescriptorHeap {
public:
    /// How many descriptors one heap holds; the allocator adds heaps of this size as it runs out.
    static constexpr u32 kDescriptorsPerHeap = 1024;

    /// Sets the device and the heap type; no heap is created until the first allocation.
    ///
    /// Param device: the device; borrowed for the allocator's life.
    /// Param type: the heap type (CBV/SRV/UAV, sampler, RTV or DSV).
    void Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type);

    /// Returns a free descriptor.
    ///
    /// Param out: receives the CPU handle.
    /// Returns: true on success.
    /// Fails: returns false if a new heap cannot be created (out of memory).
    bool Allocate(D3D12_CPU_DESCRIPTOR_HANDLE* out);

    /// Gives a descriptor back; a null handle (`ptr` 0) is ignored.
    ///
    /// Param handle: a handle from `Allocate`.
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

} // namespace engine_placeholder::rhi::dx12
