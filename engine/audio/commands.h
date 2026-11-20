// The command queue and the voice events (Phase 26 Day 421): how the game thread controls the mixer without locks. The
// game thread posts commands into a single-producer single-consumer ring; each carries the block number it takes
// effect at, and the audio thread applies the commands of a block, in the order posted, before it mixes the block.
// Events travel back through a second ring.
#pragma once

#include "containers/vector.h"
#include "lang/utility.h"
#include "sync/atomic.h"
#include "types.h"

namespace vse::audio
{

/// A bounded single-producer single-consumer ring of `T`.
///
/// One thread calls `Push`, one thread calls `Front` and `Pop`. Neither blocks, locks or allocates after `Init`.
/// `T` must be default-constructible and movable; a slot keeps its last value until it is overwritten, so `Front` is
/// moved from by the consumer.
///
/// - Thread-safety: thread-compatible; safe for exactly one producer thread and one consumer thread, and `Init` must
/// finish before either starts.
/// - Ownership: owns the slots.
template <class T> class SpscRing
{
public:
    /// Allocates the slots.
    ///
    /// - Param capacity: the number of slots; rounded up to a power of two, at least 2.
    /// - Returns: false if the heap is exhausted.
    /// - Fails: reported by return value.
    bool Init(u32 capacity)
    {
        u32 size = 2;
        while (size < capacity)
        {
            size *= 2;
        }
        slots_.Reset();
        if (!slots_.TryReserve(size))
        {
            return false;
        }
        for (u32 i = 0; i < size; ++i)
        {
            slots_.Push(T());
        }
        mask_ = size - 1;
        head_.Store(0, MemoryOrder::Relaxed);
        tail_.Store(0, MemoryOrder::Relaxed);
        return true;
    }

    /// Adds a value (producer).
    ///
    /// - Param value: the value, moved in.
    /// - Returns: false if the ring is full (the value is left untouched).
    /// - Fails: reported by return value.
    bool Push(T&& value)
    {
        const u64 tail = tail_.Load(MemoryOrder::Relaxed);
        if (tail - head_.Load(MemoryOrder::Acquire) > mask_)
        {
            return false;
        }
        slots_[static_cast<usize>(tail & mask_)] = Move(value);
        tail_.Store(tail + 1, MemoryOrder::Release);
        return true;
    }

    /// The oldest value (consumer), or null if the ring is empty. Valid until `Pop`.
    ///
    /// - Returns: a pointer the consumer may move from.
    /// - Ownership: borrowed from the ring until `Pop`.
    /// - Fails: never.
    T* Front()
    {
        const u64 head = head_.Load(MemoryOrder::Relaxed);
        if (head == tail_.Load(MemoryOrder::Acquire))
        {
            return nullptr;
        }
        return &slots_[static_cast<usize>(head & mask_)];
    }

    /// Releases the slot `Front` returned (consumer).
    ///
    /// - Fails: never.
    void Pop() { head_.Store(head_.Load(MemoryOrder::Relaxed) + 1, MemoryOrder::Release); }

    /// The number of slots.
    u32 Capacity() const { return static_cast<u32>(mask_ + 1); }

    /// How many values are waiting (a snapshot).
    u32 Size() const { return static_cast<u32>(tail_.Load(MemoryOrder::Acquire) - head_.Load(MemoryOrder::Acquire)); }

private:
    Vector<T> slots_;
    u64 mask_ = 1;
    Atomic<u64> head_{0};
    Atomic<u64> tail_{0};
};

}  // namespace vse::audio
