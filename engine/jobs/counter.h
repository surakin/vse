#pragma once

#include <atomic>
#include <coroutine>
#include <utility>

#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "log/assert.h"
#include "types.h"

// Phase 2 Day 32 (design decision D7): the join primitive. A Counter
// tracks outstanding work; a coroutine `co_await`s it and is resumed once
// the count reaches zero.
//
// USAGE CONTRACT
//   * All Increment()s for a wait happen BEFORE the co_await (the spawner
//     increments, then submits work that Decrement()s — Spawn() below).
//   * At most ONE coroutine awaits a Counter at a time (asserted).
//   * The Counter must outlive every Decrement() call and the await.
//     Typical placement: a local in the awaiting coroutine's frame.
//
// STATE. One atomic word: bit 0 = "a waiter has parked its handle",
// bits 1.. = outstanding count. `slot_` holds the waiter's handle and is
// written BEFORE bit 0 is set (acq_rel RMW), read only by the Decrement
// that observes (count == 1 && bit 0) — which therefore has exclusive
// access to the handle.
//
// EXACTLY-ONE-RESUMER INVARIANT. Whether the awaiter or a decrementer
// resumes the coroutine is decided by a SINGLE atomic read-modify-write
// on `state_`:
//   * await_suspend does fetch_or(1). If the count it observes is 0 the
//     work already finished: it returns false and the coroutine continues
//     inline (no decrementer ever sees the waiter bit set at count 1->0).
//     Otherwise it returns true and the decrementer that takes the count
//     to zero (fetch_sub observes count 1 with bit 0) owns the wake-up.
//   * Decrement does fetch_sub(2); only the one call that observes
//     (count 1, bit 0) resumes the waiter.
// Because both sides decide with one RMW on the same word and neither
// touches the Counter after its RMW unless it owns the wake-up, there is
// no window in which the awaiter can run to completion and destroy the
// Counter while a decrementer still reads it.
//
// RESUMPTION. The waiter is never resumed inline on the decrementer's
// stack: Decrement hands it to Scheduler::Schedule, which pushes onto the
// current participant's deque, or onto the injection queue when called
// from a non-participant (e.g. the I/O thread). That bounds native stack
// depth no matter how deep a chain of dependent jobs is.
namespace engine_placeholder::jobs {

class Counter {
public:
    explicit Counter(Scheduler& scheduler) : scheduler_(&scheduler) {}
    // Binds to the scheduler of the calling participant thread.
    Counter() : scheduler_(Scheduler::Current()) {
        ENGINE_ASSERT(scheduler_ != nullptr, "Counter() default-constructed on a non-participant thread");
    }

    Counter(const Counter&) = delete;
    Counter& operator=(const Counter&) = delete;

    Scheduler& GetScheduler() const { return *scheduler_; }

    void Increment(u64 n = 1) { state_.fetch_add(n << 1, std::memory_order_acq_rel); }

    void Decrement() {
        const u64 old = state_.fetch_sub(2, std::memory_order_acq_rel);
        ENGINE_ASSERT((old >> 1) >= 1, "Counter::Decrement below zero");
        if ((old >> 1) == 1 && (old & 1) != 0) {
            // We took the count to zero with a waiter parked: we own the
            // wake-up. The awaiter cannot proceed (or free *this) until
            // we schedule it, so reading slot_ here is safe; nothing of
            // *this is touched after Schedule.
            std::coroutine_handle<> h = std::coroutine_handle<>::from_address(slot_);
            Scheduler* s = scheduler_;
            s->Schedule(h);
        }
    }

    bool IsZero() const { return (state_.load(std::memory_order_acquire) >> 1) == 0; }
    u64 Value() const { return state_.load(std::memory_order_acquire) >> 1; }

    // Process-wide counts of which await_suspend path was taken, so tests
    // can prove both halves of the race were exercised.
    struct PathCounters {
        std::atomic<u64> inline_continues{0}; // finished before suspend; resumed inline
        std::atomic<u64> parked{0};           // suspended; a decrementer woke it
    };
    static PathCounters& PathStats() {
        static PathCounters counters;
        return counters;
    }

    // Hook for Job::SetCompletion(Counter&).
    static void DecrementThunk(void* counter) { static_cast<Counter*>(counter)->Decrement(); }

    struct Awaitable {
        Counter* counter;

        bool await_ready() const noexcept { return counter->IsZero(); }

        bool await_suspend(std::coroutine_handle<> h) noexcept {
            // Check BEFORE touching slot_: a second awaiter must not
            // clobber the first one's handle (ENGINE_ASSERT is always on).
            ENGINE_ASSERT((counter->state_.load(std::memory_order_acquire) & 1) == 0,
                          "Counter awaited by more than one coroutine");
            counter->slot_ = h.address();
            const u64 old = counter->state_.fetch_or(1, std::memory_order_acq_rel);
            ENGINE_ASSERT((old & 1) == 0, "Counter awaited by more than one coroutine");
            if ((old >> 1) == 0) {
                // Work finished between await_ready and here; no
                // decrementer will resume us. Continue inline.
                PathStats().inline_continues.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            PathStats().parked.fetch_add(1, std::memory_order_relaxed);
            return true; // the decrementer that reaches zero resumes us
        }

        void await_resume() noexcept {
            // Single awaiter, count is zero: clear the waiter bit so the
            // Counter can be reused for another round.
            counter->state_.fetch_and(~u64{1}, std::memory_order_acq_rel);
        }
    };

    Awaitable operator co_await() { return Awaitable{this}; }

private:
    friend struct Awaitable;

    Scheduler* scheduler_;
    std::atomic<u64> state_{0};
    void* slot_ = nullptr;
};

inline void Job::SetCompletion(Counter& counter) { SetCompletion(&Counter::DecrementThunk, &counter); }

// Counts `job` against `counter` and submits it: the job's completion
// decrements the counter.
inline void Spawn(Counter& counter, Job&& job) {
    counter.Increment();
    job.SetCompletion(counter);
    counter.GetScheduler().Submit(std::move(job));
}

} // namespace engine_placeholder::jobs
