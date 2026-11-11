#include "mem/stack_allocator.h"

#include "os/os.h"

namespace engine_placeholder
{

StackAllocator::StackAllocator(usize capacity) : capacity_(capacity)
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

StackAllocator::~StackAllocator()
{
    if (base_ != nullptr)
    {
        os::ReleaseMemory(base_, capacity_);
    }
}

void* StackAllocator::Push(usize size, usize align)
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

StackAllocator::Marker StackAllocator::CurrentMarker() const
{
    return offset_;
}

void StackAllocator::PopToMarker(Marker marker)
{
    offset_ = marker;
}

}  // namespace engine_placeholder
