#pragma once

#include <atomic>
#include <coroutine>

#include "log/assert.h"
#include "types.h"

//! A fixed-capacity Chase-Lev work-stealing deque of coroutine handles.
//!
//! There is one deque per scheduler participant. The owner pushes and pops at
//! the bottom (LIFO: hot and cache-friendly); thieves take from the top (FIFO:
//! the oldest entry, likely the biggest subtree of work).
//!
//! The algorithm is Chase and Lev, "Dynamic Circular Work-Stealing Deque"
//! (2005), in the C11 formulation of Le et al. (2013), with one deliberate
//! change: wherever the published algorithm places a standalone
//! `atomic_thread_fence(seq_cst)`, the ordering is put on the neighbouring
//! atomic operations instead (seq_cst loads, stores and CAS). That is slightly
//! stronger than strictly necessary, but ThreadSanitizer does not model
//! standalone fences and would report false races.
//!
//! Capacity is a power of two and fixed: there is no resize. A push on a full
//! deque fails and never overwrites; the scheduler then spills to its
//! injection queue. The top index only ever increases and the bottom index is
//! written only by the owner; both are 64-bit and never wrap in practice.
//! Slot reuse (index & mask) is safe: a push writes slot b only when
//! b - top < capacity, so the slot's previous occupant (index b - capacity)
//! has already been consumed, and a thief that read a slot before losing its
//! CAS discards the value. Changing the capacity or the full check must keep
//! that argument true.
//! Handles are stored as raw addresses and a null handle is never stored.
namespace engine_placeholder::jobs {

/// Outcome of one steal attempt on a [[jobs::WorkStealingDeque]].
enum class StealStatus : u8 {
    Success, ///< The output handle holds a stolen entry.
    Empty,   ///< There was nothing to steal.
    Lost,    ///< Raced with another thief or the owner; worth retrying.
};

/// Fixed-capacity work-stealing deque of coroutine handles: one owner at the bottom, any number of thieves at the top.
///
/// The object is about 34 KB: allocate it on the heap or inside the
/// scheduler, not on a thread stack. It cannot be copied.
///
/// Thread-safety: thread-safe under a role split. One owner thread calls
///   [[jobs::WorkStealingDeque::PushBottom]] and [[jobs::WorkStealingDeque::PopBottom]];
///   any thread may call [[jobs::WorkStealingDeque::StealTop]] concurrently
///   with them. Calling the owner operations from two threads is a bug.
/// Ownership: stores handles without owning them; whoever pops or steals one
///   becomes responsible for resuming or destroying it.
/// See: [[jobs::Scheduler]]
class WorkStealingDeque {
public:
    /// Number of handles the deque holds; a power of two.
    static constexpr usize kDefaultCapacity = 4096;

    WorkStealingDeque() = default;

    WorkStealingDeque(const WorkStealingDeque&) = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

    /// Gives the fixed capacity.
    ///
    /// Returns: the maximum number of handles held at once.
    static constexpr usize Capacity() { return kDefaultCapacity; }

    /// Pushes a handle at the bottom.
    ///
    /// Param h: the handle to store; must not be null.
    /// Returns: `true` if stored.
    /// Fails: returns `false` and stores nothing if the deque is full.
    /// Aborts: `h` is a null handle.
    /// Thread-safety: owner-thread-only.
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

    /// Pops the most recently pushed handle from the bottom.
    ///
    /// Returns: the handle, or a null handle if the deque is empty or a thief won the race for the last entry.
    /// Thread-safety: owner-thread-only.
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

    /// Tries to take the oldest handle from the top.
    ///
    /// Param out: receives the handle on `Success`; left untouched otherwise.
    /// Returns: `Success`, `Empty` (nothing to steal) or `Lost` (raced with another thief or the owner; worth retrying).
    /// Thread-safety: thread-safe. Any thread.
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

    /// Estimates how many handles are stored.
    ///
    /// The value is racy and meant for heuristics and tests only.
    ///
    /// Returns: an approximate entry count, never negative.
    /// Thread-safety: thread-safe.
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
