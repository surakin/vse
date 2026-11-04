#include "rhi/vulkan/vk_ring.h"

#include "log/assert.h"

namespace engine_placeholder::rhi::vulkan {

RingAllocator::RingAllocator(u64 capacity) : capacity_(capacity) {}

bool RingAllocator::Allocate(u64 size, u64 alignment, u64* out_offset) {
    ENGINE_ASSERT(size > 0, "RingAllocator::Allocate: size must be non-zero");
    ENGINE_ASSERT(alignment > 0 && (alignment & (alignment - 1)) == 0, "RingAllocator::Allocate: alignment must be a power of two");
    if (size > capacity_) {
        return false;
    }
    if (used_ == 0) {
        head_ = 0;
        tail_ = 0; // an empty ring restarts at the front so big requests get the whole buffer
    }
    // Free space is [head_, limit) and, when head_ is ahead of the tail, also [0, tail_).
    const bool wrapped = head_ <= tail_ && used_ > 0; // head has wrapped past the end, tail has not
    const u64 limit = wrapped ? tail_ : capacity_;
    u64 start = (head_ + alignment - 1) & ~(alignment - 1);
    if (start + size <= limit) {
        used_ += (start - head_) + size;
        unretired_ += (start - head_) + size;
        head_ = start + size;
        *out_offset = start;
        return true;
    }
    if (!wrapped && size <= tail_) {
        // Skip the unusable end of the buffer and start again at the front.
        used_ += (capacity_ - head_) + size;
        unretired_ += (capacity_ - head_) + size;
        head_ = size;
        *out_offset = 0;
        return true;
    }
    return false;
}

void RingAllocator::Retire(u64 fence) {
    if (unretired_ == 0) {
        return;
    }
    ENGINE_ASSERT(groups_.Empty() || fence >= groups_.Back().fence, "RingAllocator::Retire: fence values must not decrease");
    groups_.Push({fence, head_, unretired_});
    unretired_ = 0;
}

void RingAllocator::Reclaim(u64 completed) {
    usize freed = 0;
    while (freed < groups_.Size() && groups_[freed].fence <= completed) {
        used_ -= groups_[freed].bytes;
        tail_ = groups_[freed].end >= capacity_ ? 0 : groups_[freed].end;
        ++freed;
    }
    groups_.EraseFront(freed);
}

} // namespace engine_placeholder::rhi::vulkan
