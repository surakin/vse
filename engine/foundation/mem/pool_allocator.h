#pragma once

#include "types.h"

//! Fixed-size free-list pool allocator backed by a single mmap'd block.
//! Alloc and Free are O(1) with no bookkeeping storage beyond the slots.
namespace vse
{

/// Allocator handing out equally sized slots from one fixed block, with O(1)
/// allocation and free.
///
/// Free slots form an intrusive singly-linked list stored in the slots'
/// own first bytes, so a slot is at least pointer-sized. Slot memory is not
/// zeroed and no constructors or destructors run.
///
/// - Thread-safety: not-thread-safe. Synchronize externally if shared.
/// - Ownership: owns the backing block, released by the destructor. A slot
///   belongs to the caller from `Alloc` until it is passed to `Free`.
class PoolAllocator
{
public:
    /// Reserves a block holding `slot_count` slots of `slot_size` bytes.
    ///
    /// - Param slot_size: bytes per slot; values below `sizeof(void*)` are
    ///   rounded up to it.
    /// - Param slot_count: number of slots.
    /// - Fails: if the OS refuses the reservation the pool has no slots and
    ///   every `Alloc` returns nullptr.
    PoolAllocator(usize slot_size, usize slot_count);
    /// Releases the backing block; outstanding slots become invalid.
    ~PoolAllocator();

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    /// Takes one slot from the free list.
    ///
    /// - Returns: the slot, or nullptr when every slot is allocated.
    /// - Fails: reports exhaustion by returning nullptr.
    void* Alloc();

    /// Returns a slot to the free list for reuse.
    ///
    /// - Param ptr: a pointer previously returned by `Alloc` on this pool and
    ///   not already freed; anything else corrupts the pool (not checked).
    void Free(void* ptr);

private:
    u8* base_ = nullptr;
    usize slot_size_ = 0;
    usize slot_count_ = 0;
    void* free_list_ = nullptr;
};

}  // namespace vse
