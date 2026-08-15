#pragma once

#include "types.h"

// Fixed-size free-list pool allocator backed by a single mmap'd block
// (PLAN.md assumption #2 — hand-rolled rather than malloc-backed). Every
// slot is the same size; free slots are threaded into an intrusive
// singly-linked list (each free slot's first bytes hold the pointer to
// the next free slot), so Alloc()/Free() are both O(1) with no extra
// bookkeeping storage.
namespace engine_placeholder {

class PoolAllocator {
public:
    PoolAllocator(usize slot_size, usize slot_count);
    ~PoolAllocator();

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    // Returns nullptr if every slot is currently allocated.
    void* Alloc();

    // `ptr` must be a pointer previously returned by Alloc() on this
    // pool and not already freed.
    void Free(void* ptr);

private:
    u8* base_ = nullptr;
    usize slot_size_ = 0;
    usize slot_count_ = 0;
    void* free_list_ = nullptr;
};

} // namespace engine_placeholder
