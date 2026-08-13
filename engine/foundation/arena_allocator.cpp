#include "arena_allocator.h"

#include "os.h"

namespace engine_placeholder {

ArenaAllocator::ArenaAllocator(usize capacity) : capacity_(capacity) {
    void* mem = os::ReserveMemory(capacity);
    if (mem == nullptr) {
        base_ = nullptr;
        capacity_ = 0;
        return;
    }
    base_ = static_cast<u8*>(mem);
}

ArenaAllocator::~ArenaAllocator() {
    FreeAll();
}

void* ArenaAllocator::Alloc(usize size, usize align) {
    usize aligned = (offset_ + align - 1) & ~(align - 1);
    if (aligned + size > capacity_) {
        return nullptr;
    }
    offset_ = aligned + size;
    return base_ + aligned;
}

void ArenaAllocator::Reset() {
    offset_ = 0;
}

void ArenaAllocator::FreeAll() {
    if (base_ != nullptr) {
        os::ReleaseMemory(base_, capacity_);
        base_ = nullptr;
        capacity_ = 0;
        offset_ = 0;
    }
}

} // namespace engine_placeholder
