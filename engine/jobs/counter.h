#pragma once

#include "sync/atomic.h"
#include <coroutine>
#include "lang/utility.h"

#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "log/assert.h"
#include "types.h"

//! The join primitive of the job system.
//!
//! A [[jobs::Counter]] tracks outstanding work; a coroutine `co_await`s it and
//! is resumed once the count reaches zero.
//!
//! The usage contract is as follows.
//! - All increments for a wait happen before the `co_await`: the spawner
//!   increments, then submits work that decrements (see [[jobs::Spawn]]).
//! - At most one coroutine awaits a counter at a time (asserted).
//! - The counter must outlive every decrement and the await. A typical
//!   placement is a local in the awaiting coroutine's frame.
//!
//! The waiter is never resumed inline on the decrementer's stack:
//! [[jobs::Counter::Decrement]] hands it to [[jobs::Scheduler::Schedule]],
//! which pushes onto the current participant's deque, or onto the injection
//! queue when called from a non-participant such as the I/O thread. That
//! bounds native stack depth however deep a chain of dependent jobs is.
//!
//! Whether the awaiter or a decrementer resumes the coroutine is decided by a
//! single atomic read-modify-write on one state word, so exactly one of them
//! resumes it, and neither touches the counter afterwards unless it owns the
//! wake-up. The awaiter therefore cannot finish and destroy the counter while
//! a decrementer still reads it.
namespace engine_placeholder::jobs {

/// Counts outstanding work and lets one coroutine wait for it to reach zero.
///
/// Increment before submitting the work, decrement when it completes, and
/// `co_await` the counter to resume once the count is zero. After the await
/// completes the counter may be reused for another round. It cannot be
/// copied.
///
/// Thread-safety: thread-safe. Any thread may call [[jobs::Counter::Increment]],
///   [[jobs::Counter::Decrement]], [[jobs::Counter::IsZero]] and
///   [[jobs::Counter::Value]]; at most one coroutine may await it at a time.
///   Increments for a wait must happen before the await begins.
/// Ownership: the counter does not own the scheduler it is bound to, which
///   must outlive it. The counter itself must outlive every decrement and the await.
/// See: [[jobs::Spawn]], [[jobs::Job::SetCompletion(Counter&)]]
class Counter {
public:
    /// Creates a zeroed counter bound to an explicit scheduler.
    /// Param scheduler: the scheduler that resumes the waiter; must outlive the counter.
    explicit Counter(Scheduler& scheduler) : scheduler_(&scheduler) {}
    /// Creates a zeroed counter bound to the scheduler of the calling participant thread.
    ///
    /// Aborts: called on a thread that is not a scheduler participant.
    Counter() : scheduler_(Scheduler::Current()) {
        ENGINE_ASSERT(scheduler_ != nullptr, "Counter() default-constructed on a non-participant thread");
    }

    Counter(const Counter&) = delete;
    Counter& operator=(const Counter&) = delete;

    /// Gives the scheduler this counter resumes its waiter on.
    ///
    /// Returns: the bound scheduler.
    Scheduler& GetScheduler() const { return *scheduler_; }

    /// Adds outstanding units of work.
    ///
    /// Do it before submitting the work that will decrement.
    ///
    /// Param n: how many units to add.
    /// Thread-safety: thread-safe.
    void Increment(u64 n = 1) { state_.FetchAdd(n << 1, MemoryOrder::AcqRel); }

    /// Marks one unit of work done and, if it was the last one and a waiter is parked, schedules the waiter.
    ///
    /// The waiter is scheduled, never resumed inline. The counter is not
    /// touched after the waiter has been handed over.
    ///
    /// Aborts: the count is already zero.
    /// Thread-safety: thread-safe. Callable from any thread, including non-participants such as the I/O thread.
    void Decrement() {
        const u64 old = state_.FetchSub(2, MemoryOrder::AcqRel);
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

    /// Tells whether no work is outstanding.
    ///
    /// Returns: `true` if the count is zero at the moment of the call.
    /// Thread-safety: thread-safe.
    bool IsZero() const { return (state_.Load(MemoryOrder::Acquire) >> 1) == 0; }
    /// Reads the number of outstanding units of work.
    ///
    /// Returns: the current count; may be stale by the time the caller uses it.
    /// Thread-safety: thread-safe.
    u64 Value() const { return state_.Load(MemoryOrder::Acquire) >> 1; }

    /// Process-wide tallies of which await path was taken, so tests can show both halves of the race ran.
    ///
    /// Thread-safety: thread-safe. Relaxed atomic counters.
    struct PathCounters {
        /// Awaits that found the count already zero and continued inline.
        Atomic<u64> inline_continues{0};
        /// Awaits that suspended and were woken by a decrementer.
        Atomic<u64> parked{0};
    };

    /// Gives the process-wide await-path tallies.
    ///
    /// Returns: a reference to a function-local static that lives for the whole process.
    static PathCounters& PathStats() {
        static PathCounters counters;
        return counters;
    }

    /// Completion-hook adapter that decrements the [[jobs::Counter]] passed as `counter`.
    ///
    /// This is what [[jobs::Job::SetCompletion(Counter&)]] registers.
    ///
    /// Param counter: a `Counter*` that must outlive the call.
    /// Aborts: the counter's count is already zero.
    /// Ownership: borrows the counter; ownership is not transferred.
    static void DecrementThunk(void* counter) { static_cast<Counter*>(counter)->Decrement(); }

    /// Awaiter produced by `co_await` on a counter.
    ///
    /// Thread-safety: not-thread-safe. It belongs to the one coroutine awaiting the counter.
    struct Awaitable {
        /// The awaited counter.
        Counter* counter;

        /// Tells the coroutine machinery whether suspension can be skipped.
        ///
        /// Returns: `true` if the count is already zero.
        bool await_ready() const noexcept { return counter->IsZero(); }

        /// Parks the awaiting coroutine unless the work finished in the meantime.
        ///
        /// Param h: the awaiting coroutine's handle.
        /// Returns: `false` if the count reached zero first and the coroutine continues inline; `true` if it was parked and the decrementer that reaches zero will schedule it.
        /// Aborts: another coroutine is already awaiting this counter.
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            // Check BEFORE touching slot_: a second awaiter must not
            // clobber the first one's handle (ENGINE_ASSERT is always on).
            ENGINE_ASSERT((counter->state_.Load(MemoryOrder::Acquire) & 1) == 0,
                          "Counter awaited by more than one coroutine");
            counter->slot_ = h.address();
            const u64 old = counter->state_.FetchOr(1, MemoryOrder::AcqRel);
            ENGINE_ASSERT((old & 1) == 0, "Counter awaited by more than one coroutine");
            if ((old >> 1) == 0) {
                // Work finished between await_ready and here; no
                // decrementer will resume us. Continue inline.
                PathStats().inline_continues.FetchAdd(1, MemoryOrder::Relaxed);
                return false;
            }
            PathStats().parked.FetchAdd(1, MemoryOrder::Relaxed);
            return true; // the decrementer that reaches zero resumes us
        }

        /// Clears the waiter mark so the counter can be reused for another round.
        void await_resume() noexcept {
            // Single awaiter, count is zero: clear the waiter bit so the
            // Counter can be reused for another round.
            counter->state_.FetchAnd(~u64{1}, MemoryOrder::AcqRel);
        }
    };

    /// Makes the counter awaitable; the coroutine resumes once the count is zero.
    ///
    /// Returns: an awaiter bound to this counter.
    Awaitable operator co_await() { return Awaitable{this}; }

private:
    friend struct Awaitable;

    Scheduler* scheduler_;
    // One atomic word: bit 0 = "a waiter has parked its handle", bits 1.. =
    // outstanding count. `slot_` holds the waiter's handle; it is written
    // BEFORE bit 0 is set (acq_rel RMW) and read only by the Decrement that
    // observes (count == 1 && bit 0), which therefore has exclusive access.
    //
    // Whether the awaiter or a decrementer resumes the coroutine is decided by
    // a SINGLE read-modify-write on `state_`: await_suspend does fetch_or(1)
    // (a count of 0 means the work already finished: continue inline), and
    // Decrement does fetch_sub(2); only the call that observes (count 1,
    // bit 0) resumes the waiter. Neither side touches the Counter after its
    // RMW unless it owns the wake-up, so the awaiter can never destroy the
    // Counter while a decrementer still reads it. Any edit to Increment,
    // Decrement or await_suspend must preserve this.
    Atomic<u64> state_{0};
    void* slot_ = nullptr;
};

inline void Job::SetCompletion(Counter& counter) { SetCompletion(&Counter::DecrementThunk, &counter); }

/// Counts a job against a counter and submits it; the job's completion decrements the counter.
///
/// Param counter: the counter to increment now and decrement on completion; must outlive the job.
/// Param job: the job to submit; its completion hook must not be set yet. It is moved from.
/// Aborts: `job` is empty or already has a completion hook.
/// Thread-safety: thread-safe. Callable from any thread; the submit follows the rules of [[jobs::Scheduler::Submit]].
/// Ownership: the scheduler takes the job's frame via [[jobs::Job::Release]].
inline void Spawn(Counter& counter, Job&& job) {
    counter.Increment();
    job.SetCompletion(counter);
    counter.GetScheduler().Submit(Move(job));
}

} // namespace engine_placeholder::jobs
