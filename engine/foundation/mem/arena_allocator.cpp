#include "mem/arena_allocator.h"

#include "os/os.h"

namespace vse
{

ArenaAllocator::ArenaAllocator(usize capacity) : capacity_(capacity)
{
    void* mem = os::ReserveMemory(capacity);
    if (mem == nullptr)
    {
        base_ = nullptr;
        capacity_ = 0;
        return;
    }
    base_ = static_cast<u8*>(mem);
}

ArenaAllocator::~ArenaAllocator()
{
    FreeAll();
}

void* ArenaAllocator::Alloc(usize size, usize align)
{
    usize aligned = (offset_ + align - 1) & ~(align - 1);
    // Split into two comparisons (rather than `aligned + size >
    // capacity_`) so a huge `size` can't integer-overflow the sum and
    // wrap past the bounds check.
    if (aligned > capacity_ || size > capacity_ - aligned)
    {
        return nullptr;
    }
    offset_ = aligned + size;
    return base_ + aligned;
}

void ArenaAllocator::Reset()
{
    offset_ = 0;
}

void ArenaAllocator::FreeAll()
{
    if (base_ != nullptr)
    {
        os::ReleaseMemory(base_, capacity_);
        base_ = nullptr;
        capacity_ = 0;
        offset_ = 0;
    }
}

}  // namespace vse
