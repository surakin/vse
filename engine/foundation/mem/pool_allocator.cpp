#include "mem/pool_allocator.h"

#include "os/os.h"

namespace engine_placeholder {

namespace {

usize RoundUpSlotSize(usize slot_size) {
    // Every free slot stores a `void*` (the next-free-slot pointer) in
    // its first bytes, so slots must be at least pointer-sized.
    return slot_size < sizeof(void*) ? sizeof(void*) : slot_size;
}

} // namespace

PoolAllocator::PoolAllocator(usize slot_size, usize slot_count)
    : slot_size_(RoundUpSlotSize(slot_size)), slot_count_(slot_count) {
    usize total = slot_size_ * slot_count_;
    void* mem = os::ReserveMemory(total);
    if (mem == nullptr) {
        base_ = nullptr;
        slot_count_ = 0;
        return;
    }
    base_ = static_cast<u8*>(mem);

    // Thread every slot onto the free list, lowest address first.
    free_list_ = nullptr;
    for (usize i = slot_count_; i > 0; --i) {
        u8* slot = base_ + (i - 1) * slot_size_;
        *reinterpret_cast<void**>(slot) = free_list_;
        free_list_ = slot;
    }
}

PoolAllocator::~PoolAllocator() {
    if (base_ != nullptr) {
        os::ReleaseMemory(base_, slot_size_ * slot_count_);
    }
}

void* PoolAllocator::Alloc() {
    if (free_list_ == nullptr) {
        return nullptr;
    }
    void* slot = free_list_;
    free_list_ = *reinterpret_cast<void**>(slot);
    return slot;
}

void PoolAllocator::Free(void* ptr) {
    *reinterpret_cast<void**>(ptr) = free_list_;
    free_list_ = ptr;
}

} // namespace engine_placeholder
