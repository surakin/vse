#pragma once

#include "debug/compiler.h"
#include "jobs/coroutine.h"
#include "jobs/frame_pool.h"
#include "lang/new.h"
#include "lang/utility.h"
#include "log/assert.h"
#include "types.h"

//! [[jobs::Task]]: a coroutine that produces a value and is awaited by another coroutine.
//!
//! A [[jobs::Job]] is a fire-and-forget root of work. A task is its building block: a lazy sub-computation that a job
//! (or another task) `co_await`s and that returns a value, so an asynchronous operation with several steps can be a
//! function:
//!
//! ```
//! jobs::Task<int> ReadNumber(Source& source)
//! {
//!     Status s = co_await source.Ready();   // suspends; resumed on a participant
//!     co_return s.Ok() ? source.Take() : -1;
//! }
//! jobs::Job Use(Source* source) { int n = co_await ReadNumber(*source); }
//! ```
//!
//! A task does not start until it is awaited. Awaiting it suspends the caller, runs the task on the same thread, and
//! when the task finishes (maybe after suspending on something else, and then on whichever participant resumed it) it
//! hands control straight back to the caller, with no trip through the scheduler. Reference parameters are safe when
//! the task is awaited in the expression that creates it, because the caller's frame is suspended for the whole life of
//! the task; a task that is stored and awaited later must take its arguments by value. Frames come from
//! [[jobs::FramePool::Global]], as for jobs.
namespace engine_placeholder::jobs
{

namespace detail
{

// What every task promise shares: where to go when the task is done, and the frame allocation.
struct TaskPromiseBase
{
    CoroutineHandle<> continuation;

    SuspendAlways initial_suspend() noexcept { return {}; }

    // Exceptions are off; the member must exist for the coroutine machinery and must not be survivable.
    void unhandled_exception() noexcept { ENGINE_ASSERT(false, "unhandled exception in a Task coroutine"); }

    static void* operator new(usize size) { return FramePool::Global().Allocate(size); }
    static void* operator new(usize, AlignVal)
    {
        ENGINE_ASSERT(false, "Task frame requires over-aligned storage (>16 bytes) which FramePool cannot provide");
        ENGINE_TRAP();
    }
    static void operator delete(void* ptr, usize) noexcept { FramePool::Global().Deallocate(ptr); }
};

// At the end of the body: give control to the awaiting coroutine (symmetric transfer), keeping the frame alive for it
// to read the value; the Task object destroys the frame.
struct TaskFinalAwaiter
{
    bool await_ready() noexcept { return false; }
    template <class P> CoroutineHandle<> await_suspend(CoroutineHandle<P> finished) noexcept
    {
        return finished.promise().continuation;
    }
    void await_resume() noexcept {}
};

}  // namespace detail

/// A lazy coroutine that returns a `T`; `co_await` it from a [[jobs::Job]] or another task.
///
/// `T` must be default-constructible and movable. The result is moved out by the `co_await`.
///
/// - Thread-safety: not-thread-safe. A task belongs to the coroutine that awaits it; the awaiting coroutine may be
/// resumed on another participant after a suspension inside the task, which is fine.
/// - Ownership: owns its coroutine frame and destroys it when the `Task` object is destroyed (the temporary of the
/// `co_await` expression), so the value is read from a live frame. A task that was never awaited destroys its
/// never-started frame.
/// - See: [[jobs::Job]]
template <class T> class [[nodiscard]] Task
{
public:
    /// The coroutine promise of a task.
    ///
    /// - Internal: the compiler's coroutine machinery uses it; nothing else should.
    /// - Thread-safety: not-thread-safe. Only the coroutine machinery touches it.
    struct promise_type : detail::TaskPromiseBase
    {
        /// The value passed to `co_return`.
        T value{};

        /// Creates the task object for the caller.
        Task get_return_object() { return Task(CoroutineHandle<promise_type>::from_promise(*this)); }
        /// At the end of the body, hands control to the awaiting coroutine.
        detail::TaskFinalAwaiter final_suspend() noexcept { return {}; }
        /// Stores the result of `co_return`.
        void return_value(T result) noexcept(noexcept(T(static_cast<T&&>(result))))
        {
            value = static_cast<T&&>(result);
        }
    };

    /// Creates an empty task that owns nothing.
    Task() = default;

    /// Destroys the frame, if the task owns one.
    ///
    /// - Ownership: frees the coroutine frame.
    ~Task()
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    /// Takes the frame of `other`, which becomes empty.
    Task(Task&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    /// Destroys the frame this task owned and takes the frame of `other`, which becomes empty.
    Task& operator=(Task&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
            {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    /// Tells the coroutine machinery that awaiting always suspends the caller (the task has not run yet).
    ///
    /// - Fails: never.
    bool await_ready() const noexcept { return false; }

    /// Starts the task on the awaiting coroutine's thread and records where to return to.
    ///
    /// - Param awaiting: the coroutine that awaits this task.
    /// - Returns: the task's handle, which the machinery resumes at once (symmetric transfer).
    /// - Aborts: the task is empty or was already awaited.
    CoroutineHandle<> await_suspend(CoroutineHandle<> awaiting) noexcept
    {
        ENGINE_ASSERT(handle_ && !handle_.done(), "Task: awaited while empty or already finished");
        handle_.promise().continuation = awaiting;
        return handle_;
    }

    /// Takes the finished task's value.
    ///
    /// - Returns: the value passed to `co_return`.
    T await_resume() noexcept { return static_cast<T&&>(handle_.promise().value); }

private:
    explicit Task(CoroutineHandle<promise_type> handle) : handle_(handle) {}

    CoroutineHandle<promise_type> handle_;
};

}  // namespace engine_placeholder::jobs
