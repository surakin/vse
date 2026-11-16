// A pool of values in chunks that never move, with generations and a last-freed-first free list (Phase 25 Day 376).
// Internal to the physics module.
#pragma once

#include "containers/array.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "types.h"

namespace engine_placeholder::physics
{

/// Values of type `T` in fixed chunks that never move.
///
/// A pointer to a value stays valid until that value is released. A slot's generation grows each time it is released,
/// so an old id is told from a new one in the same slot.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its chunks and the values.
template <class T> class SlotPool
{
public:
    /// The number of slots in one chunk.
    static constexpr u32 kChunkSize = 256;

    /// Takes a slot, in the last-released-first order.
    ///
    /// - Param index_out: receives the slot index.
    /// - Param generation_out: receives the generation (never zero).
    /// - Returns: the value (default made), or null when memory ran out.
    /// - Ownership: the pool owns the value; the pointer is valid until the slot is released.
    /// - Fails: null on out of memory.
    T* Acquire(u32* index_out, u32* generation_out)
    {
        u32 index;
        if (free_.Size() != 0)
        {
            index = free_[free_.Size() - 1];
            free_.Pop();
        }
        else
        {
            index = count_;
            if (index / kChunkSize >= chunks_.Size())
            {
                UniquePtr<Chunk> chunk = MakeUnique<Chunk>();
                if (!chunk)
                {
                    return nullptr;
                }
                chunks_.Push(Move(chunk));
            }
            ++count_;
        }
        Slot& slot = SlotAt(index);
        slot.alive = true;
        slot.value = T();
        ++live_;
        *index_out = index;
        *generation_out = slot.generation;
        return &slot.value;
    }

    /// Releases a slot if the id is current.
    ///
    /// - Param index: the slot.
    /// - Param generation: the generation of the id.
    /// - Returns: false for a stale or unknown id.
    /// - Fails: as the return says.
    bool Release(u32 index, u32 generation)
    {
        if (Find(index, generation) == nullptr)
        {
            return false;
        }
        Slot& slot = SlotAt(index);
        slot.alive = false;
        ++slot.generation;
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }
        free_.Push(index);
        --live_;
        return true;
    }

    /// Looks a value up by id.
    ///
    /// - Param index: the slot.
    /// - Param generation: the generation of the id.
    /// - Returns: the value, or null for a stale or unknown id.
    /// - Ownership: the pool owns the value; the pointer is valid until the slot is released.
    /// - Fails: as the return says.
    T* Find(u32 index, u32 generation)
    {
        if (index >= count_ || generation == 0)
        {
            return nullptr;
        }
        Slot& slot = SlotAt(index);
        return slot.alive && slot.generation == generation ? &slot.value : nullptr;
    }

    /// Looks a value up by id.
    ///
    /// - Param index: the slot.
    /// - Param generation: the generation of the id.
    /// - Returns: the value, or null for a stale or unknown id.
    /// - Ownership: the pool owns the value; the pointer is valid until the slot is released.
    /// - Fails: as the return says.
    const T* Find(u32 index, u32 generation) const { return const_cast<SlotPool*>(this)->Find(index, generation); }

    /// Returns the number of live values.
    ///
    /// - Returns: the count.
    /// - Fails: never.
    u32 Live() const { return live_; }

    /// Returns the number of slots ever made (live and free): it does not grow while released slots are reused.
    ///
    /// - Returns: the count.
    /// - Fails: never.
    u32 Slots() const { return count_; }

    /// Returns the generation of a slot that is alive, or zero.
    ///
    /// - Param index: the slot.
    /// - Returns: the generation of a live slot, else zero.
    /// - Fails: never.
    u32 LiveGeneration(u32 index) const
    {
        if (index >= count_)
        {
            return 0;
        }
        const Slot& slot = const_cast<SlotPool*>(this)->SlotAt(index);
        return slot.alive ? slot.generation : 0;
    }

private:
    struct Slot
    {
        T value;
        u32 generation = 1;
        bool alive = false;
    };
    struct Chunk
    {
        Array<Slot, kChunkSize> slots;
    };

    Slot& SlotAt(u32 index) { return chunks_[index / kChunkSize]->slots[index % kChunkSize]; }

    Vector<UniquePtr<Chunk>> chunks_;
    Vector<u32> free_;
    u32 count_ = 0;
    u32 live_ = 0;
};

}  // namespace engine_placeholder::physics
