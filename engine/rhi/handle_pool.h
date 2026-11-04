#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>
#include <utility>

#include "log/assert.h"
#include "rhi/handle.h"
#include "types.h"

//! Slot storage for device-owned resources, addressed by generation-checked handles, with deferred destruction.

namespace engine_placeholder::rhi {

/// A pool of `T` values addressed by [[rhi::Handle]], with generation checking and a deferred-destruction queue.
///
/// Slots live in fixed-size chunks that are never moved, so a pointer from `Get` stays valid until that
/// slot is freed or retired and collected. Freeing a slot bumps its generation at once, so every older
/// handle to it fails `Get`. `Retire` does the same but keeps the value alive until the frame it was
/// retired in has completed on the GPU; `CollectRetired` then hands the value back to the caller for
/// backend cleanup and recycles the slot.
///
/// The pool holds at most [[rhi::HandlePool::kMaxCapacity]] slots; `Allocate` fails past that.
///
/// Thread-safety: not-thread-safe. The owning device serializes access behind its mutex.
/// Ownership: owns the stored values and the chunk memory (libc `malloc`); both are released by the
///   destructor, which also destroys live and retired values without calling any cleanup hook.
template <typename T, typename Tag>
class HandlePool {
public:
    /// The handle type this pool hands out.
    using HandleType = Handle<Tag>;

    /// Slots per chunk.
    static constexpr u32 kChunkSlots = 256;
    /// Largest number of chunks.
    static constexpr u32 kMaxChunks = 1024;
    /// Largest number of slots the pool can ever hold.
    static constexpr u32 kMaxCapacity = kChunkSlots * kMaxChunks;

    /// Creates an empty pool; no memory is allocated until the first `Allocate`.
    HandlePool() = default;

    HandlePool(const HandlePool&) = delete;
    HandlePool& operator=(const HandlePool&) = delete;

    /// Destroys every live and retired value and frees the chunk memory.
    ~HandlePool() {
        for (u32 c = 0; c < chunk_count_; ++c) {
            for (u32 i = 0; i < kChunkSlots; ++i) {
                Slot& s = chunks_[c][i];
                if (s.state != SlotState::Free) {
                    ValuePtr(s)->~T();
                }
            }
            std::free(chunks_[c]);
        }
    }

    /// Stores `value` in a free slot and returns its handle.
    ///
    /// Param value: moved into the pool.
    /// Returns: a valid handle, or the invalid handle if the pool already holds [[rhi::HandlePool::kMaxCapacity]] slots
    ///   or memory is exhausted.
    /// Fails: reported through the return value; `value` is left untouched in that case.
    HandleType Allocate(T&& value) {
        if (free_head_ == kNone && !AddChunk()) {
            return HandleType{};
        }
        u32 index = free_head_;
        Slot& s = SlotAt(index);
        free_head_ = s.next;
        new (s.storage) T(std::move(value));
        s.state = SlotState::Live;
        s.next = kNone;
        ++live_count_;
        return HandleType{index, s.generation};
    }

    /// Returns a pointer to the value `handle` refers to, or null if the handle is invalid, stale or retired.
    ///
    /// Param handle: any handle, including a garbage one.
    /// Returns: a pointer valid until the slot is freed or retired.
    /// Ownership: the pool keeps ownership of the value.
    T* Get(HandleType handle) {
        Slot* s = Find(handle);
        return s != nullptr ? ValuePtr(*s) : nullptr;
    }

    /// Const overload of `Get`.
    ///
    /// Param handle: any handle, including a garbage one.
    /// Returns: a pointer valid until the slot is freed or retired, or null.
    /// Ownership: the pool keeps ownership of the value.
    const T* Get(HandleType handle) const {
        const Slot* s = const_cast<HandlePool*>(this)->Find(handle);
        return s != nullptr ? ValuePtr(*s) : nullptr;
    }

    /// Destroys the value at once and recycles the slot with a new generation.
    ///
    /// Use for resources the GPU can never be using. Resources the GPU may still read go through `Retire`.
    ///
    /// Param handle: the handle to free.
    /// Returns: true if the handle was live and has been freed.
    /// Fails: returns false, changing nothing, for an invalid, stale or already retired handle.
    bool Free(HandleType handle) {
        Slot* s = Find(handle);
        if (s == nullptr) {
            return false;
        }
        ValuePtr(*s)->~T();
        Recycle(handle.index, *s);
        --live_count_;
        return true;
    }

    /// Invalidates the handle at once but keeps the value alive until `CollectRetired` is told `frame` has completed.
    ///
    /// Param handle: the handle to retire.
    /// Param frame: the frame number during which the resource was last possibly used; must not decrease
    ///   between calls.
    /// Returns: true if the handle was live and has been retired.
    /// Fails: returns false, changing nothing, for an invalid, stale or already retired handle.
    /// Aborts: `frame` is lower than the frame passed to the previous `Retire`.
    bool Retire(HandleType handle, u64 frame) {
        Slot* s = Find(handle);
        if (s == nullptr) {
            return false;
        }
        ENGINE_ASSERT(frame >= last_retire_frame_, "HandlePool::Retire: frame numbers must not decrease");
        last_retire_frame_ = frame;
        s->state = SlotState::Retired;
        s->retire_frame = frame;
        s->generation = NextGeneration(s->generation);  // old handles die now
        s->next = kNone;
        if (retire_tail_ == kNone) {
            retire_head_ = handle.index;
        } else {
            SlotAt(retire_tail_).next = handle.index;
        }
        retire_tail_ = handle.index;
        --live_count_;
        ++retired_count_;
        return true;
    }

    /// Releases every retired value whose frame is at most `completed_frame`, oldest first.
    ///
    /// For each one `on_release(T&)` is called with the value still alive, so the caller can destroy the
    /// GPU object it describes; the value is then destroyed and the slot recycled.
    ///
    /// Param completed_frame: the newest frame the GPU has finished.
    /// Param on_release: callable taking `T&`.
    /// Returns: the number of values released.
    template <typename F>
    usize CollectRetired(u64 completed_frame, F&& on_release) {
        usize released = 0;
        while (retire_head_ != kNone) {
            u32 index = retire_head_;
            Slot& s = SlotAt(index);
            if (s.retire_frame > completed_frame) {
                break;
            }
            retire_head_ = s.next;
            if (retire_head_ == kNone) {
                retire_tail_ = kNone;
            }
            on_release(*ValuePtr(s));
            ValuePtr(s)->~T();
            // The generation was already bumped by Retire; just return the slot to the free list.
            s.state = SlotState::Free;
            s.next = free_head_;
            free_head_ = index;
            --retired_count_;
            ++released;
        }
        return released;
    }

    /// Releases every retired value regardless of frame; use after the GPU is idle.
    ///
    /// Param on_release: callable taking `T&`, as for [[rhi::HandlePool::CollectRetired]].
    /// Returns: the number of values released.
    template <typename F>
    usize CollectAllRetired(F&& on_release) {
        return CollectRetired(~0ull, std::forward<F>(on_release));
    }

    /// Calls `fn(HandleType, T&)` for every live value, in slot order.
    ///
    /// Param fn: callable; it must not allocate, free or retire in this pool.
    template <typename F>
    void ForEachLive(F&& fn) {
        for (u32 c = 0; c < chunk_count_; ++c) {
            for (u32 i = 0; i < kChunkSlots; ++i) {
                Slot& s = chunks_[c][i];
                if (s.state == SlotState::Live) {
                    fn(HandleType{c * kChunkSlots + i, s.generation}, *ValuePtr(s));
                }
            }
        }
    }

    /// Returns the number of live values.
    usize LiveCount() const { return live_count_; }
    /// Returns the number of retired values still waiting for their frame.
    usize RetiredCount() const { return retired_count_; }
    /// Returns the number of slots currently allocated (live, retired and free).
    usize Capacity() const { return static_cast<usize>(chunk_count_) * kChunkSlots; }

private:
    enum class SlotState : u8 { Free, Live, Retired };
    static constexpr u32 kNone = 0xFFFFFFFFu;

    struct Slot {
        alignas(T) unsigned char storage[sizeof(T)];
        u32 generation = 1;
        u32 next = kNone;
        u64 retire_frame = 0;
        SlotState state = SlotState::Free;
    };

    static_assert(alignof(T) <= alignof(std::max_align_t), "HandlePool: over-aligned values are not supported");

    static u32 NextGeneration(u32 g) {
        ++g;
        return g == 0 ? 1 : g;  // 0 is the invalid generation
    }

    static T* ValuePtr(Slot& s) { return std::launder(reinterpret_cast<T*>(s.storage)); }
    static const T* ValuePtr(const Slot& s) { return std::launder(reinterpret_cast<const T*>(s.storage)); }

    Slot& SlotAt(u32 index) { return chunks_[index / kChunkSlots][index % kChunkSlots]; }

    Slot* Find(HandleType handle) {
        if (handle.generation == 0 || handle.index >= Capacity()) {
            return nullptr;
        }
        Slot& s = SlotAt(handle.index);
        if (s.state != SlotState::Live || s.generation != handle.generation) {
            return nullptr;
        }
        return &s;
    }

    void Recycle(u32 index, Slot& s) {
        s.generation = NextGeneration(s.generation);
        s.state = SlotState::Free;
        s.next = free_head_;
        free_head_ = index;
    }

    bool AddChunk() {
        if (chunk_count_ >= kMaxChunks) {
            return false;
        }
        Slot* chunk = static_cast<Slot*>(std::malloc(sizeof(Slot) * kChunkSlots));
        if (chunk == nullptr) {
            return false;
        }
        u32 base = chunk_count_ * kChunkSlots;
        for (u32 i = 0; i < kChunkSlots; ++i) {
            Slot* s = new (&chunk[i]) Slot();
            // Chain the new slots so that low indices are handed out first.
            s->next = (i + 1 < kChunkSlots) ? base + i + 1 : free_head_;
        }
        free_head_ = base;
        chunks_[chunk_count_++] = chunk;
        return true;
    }

    Slot* chunks_[kMaxChunks] = {};
    u32 chunk_count_ = 0;
    u32 free_head_ = kNone;
    u32 retire_head_ = kNone;
    u32 retire_tail_ = kNone;
    u64 last_retire_frame_ = 0;
    usize live_count_ = 0;
    usize retired_count_ = 0;
};

} // namespace engine_placeholder::rhi
