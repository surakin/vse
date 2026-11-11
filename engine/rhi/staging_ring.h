#pragma once

#include "containers/vector.h"
#include "types.h"

//! A ring of staging space whose parts are reclaimed when the GPU's timeline passes them.

namespace engine_placeholder::rhi
{

/// Hands out ranges of a circular buffer and takes them back in order as fence values complete.
///
/// Allocations are grouped: `Retire(fence)` tags everything allocated since the previous `Retire` with `fence`, and
/// `Reclaim(completed)` frees every group whose fence is at most `completed`. Groups are freed in the order they were
/// retired, so fence values passed to `Retire` must not decrease.
///
/// - Thread-safety: thread-compatible. Not synchronized.
class RingAllocator
{
public:
    /// Creates a ring of `capacity` bytes.
    explicit RingAllocator(u64 capacity);

    /// Takes `size` bytes at `alignment` from the ring.
    ///
    /// - Param size: bytes; must be non-zero.
    /// - Param alignment: a power of two.
    /// - Param out_offset: receives the offset of the range.
    /// - Returns: true on success.
    /// - Fails: returns false if the ring has no contiguous room now; reclaim completed groups and try again.
    /// - Aborts: `size` is zero or `alignment` is not a power of two.
    /// - Ownership: the range belongs to the caller until the group it falls in is retired and reclaimed.
    bool Allocate(u64 size, u64 alignment, u64* out_offset);

    /// Tags everything allocated since the last call with `fence`.
    ///
    /// - Param fence: the fence value reached when the GPU has finished with those ranges; at least the previous value.
    /// - Aborts: `fence` is lower than the previous value.
    void Retire(u64 fence);

    /// Frees every retired group whose fence is at most `completed`.
    ///
    /// - Param completed: the highest fence value the GPU has reached.
    void Reclaim(u64 completed);

    /// Returns the fence of the oldest retired group still held, or 0 if none.
    u64 OldestPendingFence() const { return groups_.Empty() ? 0 : groups_[0].fence; }
    /// Returns the ring size in bytes.
    u64 Capacity() const { return capacity_; }
    /// Returns the bytes currently held, including space skipped for alignment and wrapping.
    u64 UsedBytes() const { return used_; }

private:
    struct Group
    {
        u64 fence;
        u64 end;    // head position when the group was retired
        u64 bytes;  // bytes the group holds
    };
    Vector<Group> groups_;
    u64 capacity_;
    u64 head_ = 0;
    u64 tail_ = 0;
    u64 used_ = 0;
    u64 unretired_ = 0;  // bytes allocated since the last Retire
};

}  // namespace engine_placeholder::rhi
