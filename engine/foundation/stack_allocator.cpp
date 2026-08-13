#include "stack_allocator.h"

#include <sys/mman.h>

#include "os.h"

namespace engine_placeholder {

StackAllocator::StackAllocator(usize capacity) : capacity_(capacity) {
    void* mem = os::MMap(nullptr, capacity, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        base_ = nullptr;
        capacity_ = 0;
        return;
    }
    base_ = static_cast<u8*>(mem);
}

StackAllocator::~StackAllocator() {
    if (base_ != nullptr) {
        os::MUnmap(base_, capacity_);
    }
}

void* StackAllocator::Push(usize size, usize align) {
    usize aligned = (offset_ + align - 1) & ~(align - 1);
    if (aligned + size > capacity_) {
        return nullptr;
    }
    offset_ = aligned + size;
    return base_ + aligned;
}

StackAllocator::Marker StackAllocator::CurrentMarker() const {
    return offset_;
}

void StackAllocator::PopToMarker(Marker marker) {
    offset_ = marker;
}

} // namespace engine_placeholder
