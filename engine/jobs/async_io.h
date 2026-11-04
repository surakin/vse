#pragma once

#include <condition_variable>
#include <coroutine>
#include <mutex>
#include <thread>

#include "jobs/counter.h"
#include "mem/arena_allocator.h"
#include "string/string.h"
#include "types.h"

//! Asynchronous file reads for jobs.
//!
//! A blocking read must never occupy a compute worker, and the awaiting
//! coroutine must never resume on an ad hoc thread. So reads run on one
//! dedicated [[jobs::IoThread]], a plain `std::thread` that is not a
//! participant ([[jobs::WorkerPool::IsParticipantThread]] is false on it),
//! started and stopped with the scheduler. Completion is
//! [[jobs::Counter::Decrement]] called from the I/O thread, which, being a
//! non-participant, hands the continuation to the scheduler's injection
//! queue, so the coroutine resumes on a participant.
//!
//! Usage inside a job:
//!
//! ```
//! FileReadResult r = co_await ReadFileAsync(scheduler, "path");
//! if (r.ok) { /* use r.data[0 .. r.size), owned by r */ }
//! ```
//!
//! The bytes live in an arena owned by the [[jobs::FileReadResult]] and are
//! freed when it is destroyed.
//!
//! Shutdown drains: requests already queued are still executed, then the I/O
//! thread is joined, and a read issued after that completes immediately with
//! `ok == false`. Awaiting coroutines are only guaranteed to run if the
//! caller joins its outstanding work before destroying the scheduler (see
//! [[jobs::Scheduler::Shutdown]]): the scheduler shuts the I/O thread down
//! first, but a continuation still queued when the workers stop is destroyed
//! unrun. A request blocked in the OS (an `open()` on a FIFO nobody writes to)
//! blocks shutdown until the OS call returns. There is one I/O thread and one
//! FIFO queue, so a slow request delays the ones behind it (head-of-line
//! blocking); a pool of I/O threads can replace it later without changing the
//! awaitable.
namespace engine_placeholder::jobs {

/// Intrusive node for work queued on the [[jobs::IoThread]]; embed it as the first base of a request type.
///
/// Thread-safety: thread-compatible. The queue owns `next` while the request is queued; `run` executes on the I/O thread.
/// Ownership: the request is owned by whoever created it and must stay valid until its `run` has finished.
struct IoRequest {
    /// Next queued request; managed by [[jobs::IoThread]], not by the user.
    IoRequest* next = nullptr;
    /// Function executed on the I/O thread; it may free the request itself as its last action.
    void (*run)(IoRequest* self) = nullptr;
};

/// The dedicated thread that runs blocking I/O requests in FIFO order.
///
/// It is a plain `std::thread`, not a scheduler participant. Started with the
/// scheduler and stopped by [[jobs::Scheduler::Shutdown]] before the workers.
/// It cannot be copied.
///
/// Thread-safety: thread-safe. Any thread may enqueue or shut it down.
/// Ownership: owns the I/O thread and joins it on destruction; queued
///   requests stay owned by their creators.
/// See: [[jobs::ReadFileAsync]]
class IoThread {
public:
    /// Starts the I/O thread.
    IoThread();

    /// Drains queued requests and joins the thread, as [[jobs::IoThread::Shutdown]] does.
    ///
    /// Ownership: joins and releases the I/O thread.
    ~IoThread();

    IoThread(const IoThread&) = delete;
    IoThread& operator=(const IoThread&) = delete;

    /// Queues a request for the I/O thread.
    ///
    /// The request must stay valid until its `run` has finished.
    ///
    /// Param request: the request to queue; must not already be queued.
    /// Returns: `true` if queued.
    /// Fails: returns `false` if the thread is shutting down or already stopped; the request is not queued, its `run` is never called, and the caller must complete it itself.
    /// Ownership: the thread borrows the request until `run` returns; ownership stays with the caller.
    /// Thread-safety: thread-safe. Any thread.
    [[nodiscard]] bool Enqueue(IoRequest* request);

    /// Runs everything already queued, then joins the thread.
    ///
    /// Idempotent and safe to call concurrently: every caller returns only
    /// after the thread has been joined. Blocks as long as the running
    /// request does.
    ///
    /// Thread-safety: thread-safe.
    void Shutdown();

    /// Gives the id of the I/O thread, for telling it apart from other threads.
    ///
    /// Returns: the thread id recorded at construction.
    std::thread::id ThreadId() const { return thread_id_; }

private:
    void Loop();

    std::mutex shutdown_mutex_; // serializes Shutdown() callers through the join
    std::mutex mutex_;
    std::condition_variable cv_;
    IoRequest* head_ = nullptr;
    IoRequest* tail_ = nullptr;
    bool stopping_ = false;
    bool joined_ = false;
    std::thread thread_;
    std::thread::id thread_id_;
};

struct ReadState; // heap-allocated per request (defined in async_io.cpp)

/// Result of an asynchronous file read; owns the bytes read.
///
/// Move-only. The data pointer stays valid while the object lives.
///
/// Thread-safety: not-thread-safe. A value owned by the awaiting coroutine.
/// Ownership: owns the buffer behind `data` (an arena) and frees it on destruction; a moved-from result is empty.
/// See: [[jobs::ReadFileAsync]]
class FileReadResult {
public:
    FileReadResult() = default;

    /// Frees the buffer.
    ///
    /// Ownership: releases the bytes behind `data`.
    ~FileReadResult();

    /// Takes over the bytes of `other`, which becomes empty (`ok == false`).
    /// Param other: the result to move from.
    FileReadResult(FileReadResult&& other) noexcept;

    /// Replaces this result with `other`, freeing the bytes this one owned.
    /// Param other: the result to move from; becomes empty.
    FileReadResult& operator=(FileReadResult&& other) noexcept;
    FileReadResult(const FileReadResult&) = delete;
    FileReadResult& operator=(const FileReadResult&) = delete;

    /// `true` if the whole file was read; `false` for a missing or unreadable file, an oversize file or path, or a shut-down I/O thread.
    bool ok = false;
    /// The file contents; valid while this object lives. Null when `ok` is false.
    const u8* data = nullptr;
    /// Number of bytes at `data`.
    usize size = 0;

private:
    friend class ReadFileAwaitable;
    ReadState* state_ = nullptr;
};

/// Awaitable returned by [[jobs::ReadFileAsync]]; awaiting it yields a [[jobs::FileReadResult]].
///
/// Must be awaited from a job. Moving transfers the pending request; an
/// awaitable destroyed without being awaited discards it.
///
/// Thread-safety: not-thread-safe. Belongs to the coroutine that awaits it.
/// Ownership: owns the per-request state until `await_resume` hands it to the result.
/// See: [[jobs::IoThread]]
class ReadFileAwaitable {
public:
    /// Takes over the pending request of `other`, which becomes empty.
    /// Param other: the awaitable to move from.
    ReadFileAwaitable(ReadFileAwaitable&& other) noexcept : state_(other.state_) { other.state_ = nullptr; }
    ReadFileAwaitable(const ReadFileAwaitable&) = delete;
    ReadFileAwaitable& operator=(const ReadFileAwaitable&) = delete;

    /// Frees the request state if it was never handed to a result.
    ///
    /// Ownership: releases the per-request state that has not moved into a [[jobs::FileReadResult]].
    ~ReadFileAwaitable();

    /// Tells the coroutine machinery whether suspension can be skipped.
    ///
    /// Returns: always `false`; the read is started in `await_suspend`.
    bool await_ready() const noexcept { return false; }

    /// Queues the read on the I/O thread and parks the coroutine until it completes.
    ///
    /// Param h: the awaiting coroutine's handle.
    /// Returns: `false` if the coroutine should continue inline, which happens when the path is too long, the I/O thread is shut down, or the read already finished; `true` if it was parked and will be resumed on a participant.
    bool await_suspend(std::coroutine_handle<> h) noexcept;

    /// Collects the outcome of the read.
    ///
    /// Returns: the result; `ok` is false when the read failed.
    /// Aborts: the coroutine resumed on a thread that is not a scheduler participant.
    /// Ownership: the result takes over the request state and its buffer.
    FileReadResult await_resume() noexcept;

private:
    friend ReadFileAwaitable ReadFileAsync(Scheduler&, StringView, usize);
    explicit ReadFileAwaitable(ReadState* state) : state_(state) {}

    ReadState* state_;
};

/// Default limit, in bytes, on the size of a file [[jobs::ReadFileAsync]] will read (64 MiB).
inline constexpr usize kDefaultMaxReadBytes = usize{64} << 20;

/// Longest path, in bytes, that [[jobs::ReadFileAsync]] accepts.
inline constexpr usize kMaxAsyncPathBytes = 1023;

/// Reads a whole file on the I/O thread without blocking a worker.
///
/// Nothing starts until the returned awaitable is awaited. Files larger than
/// `max_bytes`, paths longer than [[jobs::kMaxAsyncPathBytes]], and
/// missing or unreadable files all yield a result with `ok == false`. The
/// path is copied before the call returns.
///
/// Param scheduler: the scheduler whose I/O thread performs the read and whose participant resumes the awaiter; must outlive the read.
/// Param path: file to read; copied.
/// Param max_bytes: largest acceptable file size in bytes.
/// Returns: an awaitable that produces a [[jobs::FileReadResult]].
/// Thread-safety: thread-safe. Await it from a job.
ReadFileAwaitable ReadFileAsync(Scheduler& scheduler, StringView path, usize max_bytes = kDefaultMaxReadBytes);

} // namespace engine_placeholder::jobs
