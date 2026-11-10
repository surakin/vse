#pragma once

#include "jobs/coroutine.h"
#include "lang/new.h"

#include "debug/compiler.h"
#include "jobs/frame_pool.h"
#include "log/assert.h"
#include "types.h"

//! The job coroutine type.
//!
//! A job is a C++20 coroutine returning [[jobs::Job]]; the scheduler resumes
//! its handle on whichever worker picks it up. Jobs start suspended, so
//! creating one runs no user code until it is submitted. Jobs return void:
//! results travel through pointers to data that outlives the join.
//!
//! Parameter lifetime. The compiler copies coroutine parameters into the
//! frame, but a reference parameter stays a reference to the caller's object,
//! and a lambda coroutine's captures live in the closure object, not in the
//! frame. Both dangle once the creating function returns, which for a job is
//! almost always before it runs. Write job bodies as free functions (or
//! static members) that take everything by value or as a pointer to data that
//! outlives the job's completion, for example data the spawner keeps alive
//! until it has joined. Never write a job as a capture-by-reference lambda
//! and never pass it a reference parameter.
//!
//! Frame memory comes from [[jobs::FramePool::Global]], never the global
//! heap, so frame allocation does not return null.
namespace engine_placeholder::jobs {

class Counter;

/// Completion hook: a plain function pointer called with the `user` pointer registered next to it.
///
/// It runs exactly once per job, after the job's frame has been freed.
using CompletionFn = void (*)(void* user);

/// Move-only handle to a not-yet-started job coroutine frame.
///
/// Jobs start suspended: creating a `Job` runs no user code. Submitting it
/// (see [[jobs::Scheduler::Submit]]) calls [[jobs::Job::Release]], which hands
/// the raw handle over and leaves the `Job` empty. From then on the frame
/// owns itself and destroys itself when the body finishes. When the body
/// finishes, the frame is freed first and only then does the completion hook
/// run, so anything woken in response observes the frame already gone.
/// Awaiting a `Job` directly is not supported; join with a [[jobs::Counter]].
///
/// Thread-safety: not-thread-safe. A `Job` object is owned by one thread at a
///   time; the frame it hands off is resumed by whichever worker runs it.
/// Ownership: owns its frame until [[jobs::Job::Release]] transfers it; a
///   `Job` destroyed without being released destroys its never-started frame
///   (and never runs its completion hook).
/// See: [[jobs::Spawn]], [[jobs::Scheduler::Submit]]
class Job {
public:
    struct promise_type;

    /// Typed coroutine handle to a job frame.
    using Handle = CoroutineHandle<promise_type>;

    /// Coroutine promise for [[jobs::Job]].
    ///
    /// Thread-safety: not-thread-safe. Only the compiler-generated coroutine machinery touches it.
    /// Internal: must be visible to the compiler as the coroutine promise type.
    struct promise_type {
        CompletionFn on_complete = nullptr;
        void* on_complete_user = nullptr;

        Job get_return_object() { return Job(Handle::from_promise(*this)); }

        // Start suspended; the scheduler decides when and where to run.
        SuspendAlways initial_suspend() noexcept { return {}; }

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
        static void* operator new(usize, AlignVal) {
            ENGINE_ASSERT(false, "Job frame requires over-aligned storage (>16 bytes) which FramePool cannot provide");
            ENGINE_TRAP(); // never hand out misaligned storage, even where the assert is compiled out
        }
        static void operator delete(void* ptr, usize) noexcept { FramePool::Global().Deallocate(ptr); }
    };

    Job() = default;

    /// Destroys the frame if it was never released.
    ///
    /// Ownership: frees a still-owned, never-started frame; a released job is untouched.
    ~Job() {
        // Still owned => never released, never started: free its frame.
        if (handle_) {
            handle_.destroy();
        }
    }

    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    /// Takes over the frame of `other`, which becomes empty.
    /// Param other: the job to move from.
    Job(Job&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    /// Replaces this job with the frame of `other`, destroying any frame this job still owned.
    /// Param other: the job to move from; becomes empty.
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

    /// Tells whether this object still owns an unreleased frame.
    ///
    /// Returns: `true` while a frame is owned; `false` for an empty, moved-from or released job.
    bool Valid() const { return static_cast<bool>(handle_); }

    /// Registers a completion hook to run when the job finishes.
    ///
    /// Call it before the job is released, at most once. `fn` runs exactly
    /// once, after the frame has been freed, on the thread that executed the
    /// job's final resume. A job destroyed without being released never runs
    /// its hook, so a [[jobs::Counter]] that already counted it must be
    /// corrected by whoever counted it; [[jobs::Spawn]] counts only at
    /// submit time.
    ///
    /// Param fn: the hook; may be null to register nothing.
    /// Param user: passed to `fn` unchanged; must stay valid until the hook has run.
    /// Aborts: the job is empty (moved-from or released), or a completion hook was already set.
    /// Ownership: `user` is borrowed, not owned; the job never frees it.
    void SetCompletion(CompletionFn fn, void* user) {
        ENGINE_ASSERT(handle_, "Job::SetCompletion on an empty Job");
        ENGINE_ASSERT(handle_.promise().on_complete == nullptr, "Job::SetCompletion called twice");
        handle_.promise().on_complete = fn;
        handle_.promise().on_complete_user = user;
    }

    /// Binds the job to a counter so that completing the job decrements it.
    ///
    /// The decrement happens after the frame has been freed. Defined in
    /// `jobs/counter.h`; include that header to use this overload. Same
    /// preconditions as the hook overload.
    ///
    /// Param counter: the counter to decrement; must outlive the job's completion.
    /// Aborts: the job is empty (moved-from or released), or a completion hook was already set.
    void SetCompletion(Counter& counter);

    /// Transfers ownership of the frame to the caller, normally the scheduler.
    ///
    /// The returned handle destroys itself on completion and this job becomes
    /// empty. The caller must eventually resume the handle to completion.
    ///
    /// Returns: the owning handle to the not-yet-started frame.
    /// Aborts: the job is empty.
    /// Ownership: the caller takes over the frame; it is freed when the body finishes.
    CoroutineHandle<> Release() {
        ENGINE_ASSERT(handle_, "Job::Release on an empty Job");
        CoroutineHandle<> h = handle_;
        handle_ = nullptr;
        return h;
    }

private:
    explicit Job(Handle h) : handle_(h) {}

    Handle handle_;
};

} // namespace engine_placeholder::jobs
