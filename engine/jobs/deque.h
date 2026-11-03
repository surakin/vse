#pragma once

#include <atomic>
#include <coroutine>

#include "log/assert.h"
#include "types.h"

// Phase 2 Day 30: a fixed-capacity Chase-Lev work-stealing deque of
// coroutine handles, one per participant. The OWNER pushes and pops at
// the bottom (LIFO — hot, cache-friendly); THIEVES take from the top
// (FIFO — oldest, likely the biggest subtree of work).
//
// Based on Chase & Lev, "Dynamic Circular Work-Stealing Deque" (2005)
// with the C11 formulation of Lê et al. (2013), with ONE deliberate
// change: wherever the published algorithm places a standalone
// atomic_thread_fence(seq_cst), the ordering is instead put on the
// neighbouring atomic operations (seq_cst loads/stores/CAS). That is
// slightly stronger than strictly necessary, but ThreadSanitizer does
// not model standalone fences (it would report false races), and
// correctness comes first; Day 35 measures whether it matters.
//
// Capacity is a power of two and fixed: no resize. PushBottom returns
// false when full and never overwrites; the caller (Scheduler::Submit)
// spills to the injection queue.
//
// Index discipline: `top_` only ever increases (CAS in PopBottom's
// last-element case and in StealTop); `bottom_` is written only by the
// owner. Indices are 64-bit and never wrap in practice. Slot reuse
// (index & mask) is safe: PushBottom only writes slot b when b - top <
// capacity, so the slot's previous occupant (index b - capacity) has
// already been consumed, and a thief that read a slot before losing its
// CAS discards the value.
//
// Handles are stored as raw addresses. A null handle is never stored.
namespace engine_placeholder::jobs {

enum class StealStatus : u8 {
    Success, // `out` holds a handle
    Empty,   // nothing to steal
    Lost,    // raced with another thief or the owner; worth retrying
};

class WorkStealingDeque {
public:
    static constexpr usize kDefaultCapacity = 4096;

    // (C++20: the atomics are value-initialized; slots start null.)
    // ~34 KB: allocate on the heap / inside the scheduler, not on a
    // thread stack.
    WorkStealingDeque() = default;

    WorkStealingDeque(const WorkStealingDeque&) = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

    static constexpr usize Capacity() { return kDefaultCapacity; }

    // OWNER ONLY. Returns false (and stores nothing) if full.
    bool PushBottom(std::coroutine_handle<> h) {
        ENGINE_ASSERT(h.address() != nullptr, "WorkStealingDeque: null handle");
        const i64 b = bottom_.load(std::memory_order_relaxed);
        // acquire is enough: a stale (smaller) top only makes the deque look
        // fuller than it is.
        const i64 t = top_.load(std::memory_order_acquire);
        if (b - t >= static_cast<i64>(kDefaultCapacity)) {
            return false;
        }
        slots_[static_cast<usize>(b) & kMask].store(h.address(), std::memory_order_relaxed);
        // Publishes the slot write to thieves (their seq_cst load of
        // bottom_ acquires it).
        bottom_.store(b + 1, std::memory_order_seq_cst);
        return true;
    }

    // OWNER ONLY. Returns a null handle if empty.
    std::coroutine_handle<> PopBottom() {
        const i64 b = bottom_.load(std::memory_order_relaxed) - 1;
        // seq_cst store then seq_cst load of top_: the Dekker-style pair
        // that makes the owner and a thief agree on who gets the last
        // element (the published algorithm uses a fence here).
        bottom_.store(b, std::memory_order_seq_cst);
        i64 t = top_.load(std::memory_order_seq_cst);
        if (t > b) {
            // Was empty: restore bottom. Must be a release (seq_cst) store:
            // a thief that later reads THIS value must still synchronize
            // with the slot writes of the pushes that preceded it (a plain
            // store continues no release sequence in C++20).
            bottom_.store(b + 1, std::memory_order_seq_cst);
            return {};
        }
        void* addr = slots_[static_cast<usize>(b) & kMask].load(std::memory_order_relaxed);
        if (t == b) {
            // Last element: race the thieves for it.
            if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                addr = nullptr; // a thief won
            }
            bottom_.store(b + 1, std::memory_order_seq_cst); // release: see above
        }
        return std::coroutine_handle<>::from_address(addr);
    }

    // ANY THREAD. On Success `out` is set; otherwise left untouched.
    StealStatus StealTop(std::coroutine_handle<>& out) {
        i64 t = top_.load(std::memory_order_seq_cst);
        const i64 b = bottom_.load(std::memory_order_seq_cst);
        if (t >= b) {
            return StealStatus::Empty;
        }
        // Read before the CAS: if the CAS succeeds, `top_` was still `t`
        // when we read, so the owner could not have reused this slot.
        void* addr = slots_[static_cast<usize>(t) & kMask].load(std::memory_order_relaxed);
        if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
            return StealStatus::Lost;
        }
        out = std::coroutine_handle<>::from_address(addr);
        return StealStatus::Success;
    }

    // Approximate (racy) size, for heuristics and tests only.
    i64 ApproxSize() const {
        const i64 b = bottom_.load(std::memory_order_relaxed);
        const i64 t = top_.load(std::memory_order_relaxed);
        return b > t ? b - t : 0;
    }

private:
    static constexpr usize kMask = kDefaultCapacity - 1;
    static_assert((kDefaultCapacity & kMask) == 0, "capacity must be a power of two");

    alignas(64) std::atomic<i64> top_{0};
    alignas(64) std::atomic<i64> bottom_{0};
    alignas(64) std::atomic<void*> slots_[kDefaultCapacity];
};

} // namespace engine_placeholder::jobs
