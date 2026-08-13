#include "arena_allocator.h"

#include <sys/mman.h>

#include "os.h"

namespace engine_placeholder {

ArenaAllocator::ArenaAllocator(usize capacity) : capacity_(capacity) {
    void* mem = os::MMap(nullptr, capacity, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
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
        os::MUnmap(base_, capacity_);
        base_ = nullptr;
        capacity_ = 0;
        offset_ = 0;
    }
}

} // namespace engine_placeholder
