#pragma once

#include <coroutine>
#include <new>

#include "jobs/frame_pool.h"
#include "log/assert.h"
#include "types.h"

// Phase 2 Day 28: the job coroutine type. A job is a C++20 coroutine
// returning `Job`; the scheduler (Day 31) resumes its handle on whichever
// worker picks it up. Design decisions D2/D3 (see
// plans/phase-02-job-system.md):
//
//  * Jobs start SUSPENDED (initial_suspend = suspend_always): creating a
//    Job runs no user code. Nothing happens until it is scheduled.
//  * OWNERSHIP. A `Job` is a move-only handle to a not-yet-started frame.
//    Submitting it to the scheduler calls Release(), which hands the raw
//    handle over and leaves the Job empty; from then on the frame owns
//    itself and destroys itself when the body finishes. A Job destroyed
//    without having been released destroys its never-started frame. There
//    is no other ownership mode — `co_await`-ing a Job directly is out of
//    scope (join with a Counter, Day 32).
//  * COMPLETION. When the body finishes, final_suspend frees the frame
//    FIRST and only then runs the optional completion hook, so anything
//    that wakes up in response (a parent resumed by a Counter reaching
//    zero) observes the frame already gone. The hook is a plain function
//    pointer + user pointer (exactly once, from whichever thread ran the
//    job's last resume); Day 32 adds a Counter overload that points it at
//    Counter::Decrement.
//  * FRAMES come from FramePool::Global() via promise operator new/delete
//    (never the global heap), so `operator new` never returns nullptr and
//    no get_return_object_on_allocation_failure is needed.
//  * Jobs return void. Results travel through pointers to data that
//    outlives the join.
//
// PARAMETER-LIFETIME RULE (read before writing a job): the compiler copies
// coroutine *parameters* into the frame, but a reference parameter stays a
// reference to the caller's object, and a lambda coroutine's captures live
// in the closure object, not in the frame — both dangle once the creating
// function returns, which for a job is almost always before it runs. So
// job bodies are free functions (or static members) that take everything
// BY VALUE or as a POINTER to data that provably outlives the job's
// completion (e.g. data the spawner keeps alive until it has joined). Never
// write a job as a capture-by-reference lambda, and never pass a
// reference parameter.
namespace engine_placeholder::jobs {

class Counter;

using CompletionFn = void (*)(void* user);

class Job {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type {
        CompletionFn on_complete = nullptr;
        void* on_complete_user = nullptr;

        Job get_return_object() { return Job(Handle::from_promise(*this)); }

        // Start suspended; the scheduler decides when and where to run.
        std::suspend_always initial_suspend() noexcept { return {}; }

        // Runs when the body finishes: frees the frame, then notifies.
        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            void await_suspend(Handle h) noexcept {
                // Copy out of the promise: the frame (and promise) are
                // destroyed before the hook runs.
                CompletionFn fn = h.promise().on_complete;
                void* user = h.promise().on_complete_user;
                h.destroy();
                if (fn != nullptr) {
                    fn(user);
                }
            }
            void await_resume() noexcept {}
        };
        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_void() noexcept {}

        // Exceptions are disabled project-wide, so this is unreachable;
        // it must exist for the coroutine machinery and must not be
        // silently survivable if it ever were reached.
        void unhandled_exception() noexcept { ENGINE_ASSERT(false, "unhandled exception in a Job coroutine"); }

        static void* operator new(usize size) { return FramePool::Global().Allocate(size); }
        // Over-aligned frames (a local with alignas > 16 live across a
        // suspension) select this overload; the pool only guarantees 16.
        static void* operator new(usize, std::align_val_t) {
            ENGINE_ASSERT(false, "Job frame requires over-aligned storage (>16 bytes) which FramePool cannot provide");
            __builtin_trap(); // assert is a no-op in release; never hand out misaligned storage
        }
        static void operator delete(void* ptr, usize) noexcept { FramePool::Global().Deallocate(ptr); }
    };

    Job() = default;
    ~Job() {
        // Still owned => never released, never started: free its frame.
        if (handle_) {
            handle_.destroy();
        }
    }

    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    Job(Job&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    Job& operator=(Job&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    // True while this object still owns an unreleased frame.
    bool Valid() const { return static_cast<bool>(handle_); }

    // Registers the completion hook. Must be called before the job is
    // released/started, and at most once. NOTE: a Job destroyed without
    // being released never runs its hook, so a Counter that already
    // counted it must be handled by the spawner (Day 32 only counts at
    // submit time). `fn` runs exactly once, after the frame has been
    // freed, on the thread that executed the job's final resume.
    void SetCompletion(CompletionFn fn, void* user) {
        ENGINE_ASSERT(handle_, "Job::SetCompletion on an empty Job");
        ENGINE_ASSERT(handle_.promise().on_complete == nullptr, "Job::SetCompletion called twice");
        handle_.promise().on_complete = fn;
        handle_.promise().on_complete_user = user;
    }

    // Binds the job to a Counter: when the job completes, the Counter is
    // decremented (after the frame is freed). Defined in counter.h —
    // include it to use this overload.
    void SetCompletion(Counter& counter);

    // Transfers ownership of the frame to the caller (the scheduler): the
    // returned handle destroys itself on completion; this Job becomes
    // empty. The caller must eventually resume it to completion.
    std::coroutine_handle<> Release() {
        ENGINE_ASSERT(handle_, "Job::Release on an empty Job");
        std::coroutine_handle<> h = handle_;
        handle_ = nullptr;
        return h;
    }

private:
    explicit Job(Handle h) : handle_(h) {}

    Handle handle_;
};

} // namespace engine_placeholder::jobs
