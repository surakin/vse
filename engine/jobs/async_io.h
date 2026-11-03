#pragma once

#include <condition_variable>
#include <coroutine>
#include <mutex>
#include <thread>

#include "jobs/counter.h"
#include "mem/arena_allocator.h"
#include "string/string.h"
#include "types.h"

// Phase 2 Day 34 (design decision D8): async file reads.
//
// A blocking read must never occupy a compute worker, and the awaiting
// coroutine must never resume on an ad hoc thread. So:
//   * reads run on ONE dedicated IoThread (a plain std::thread, NOT a
//     participant: WorkerPool::IsParticipantThread() is false on it),
//     started and stopped with the Scheduler;
//   * completion is Counter::Decrement() called from the IoThread, which
//     (being a non-participant) hands the continuation to the
//     scheduler's injection queue — so the coroutine resumes on a
//     participant. await_resume ENGINE_ASSERTs exactly that.
//
// Usage inside a job:
//     FileReadResult r = co_await ReadFileAsync(scheduler, "path");
//     if (r.ok) { use r.data[0 .. r.size) }   // owned by `r`
// The bytes live in an arena owned by the FileReadResult and are freed
// when it is destroyed.
//
// SHUTDOWN. IoThread::Shutdown() DRAINS: requests already queued are
// still executed, then the thread is joined; a ReadFileAsync issued after
// that completes immediately with ok == false. The awaiting coroutines
// are only guaranteed to run if the caller joins its outstanding work
// BEFORE destroying the Scheduler (scheduler.h): the Scheduler shuts the
// I/O thread down first, but a continuation still queued when the workers
// stop is destroyed unrun. A request blocked in the OS (open() on a FIFO
// nobody writes to) blocks shutdown until the OS call returns. There is
// one I/O thread and one FIFO queue, so a slow request delays the ones
// behind it (head-of-line blocking) — by design (D8); a pool of I/O
// threads can replace it later without changing the awaitable.
namespace engine_placeholder::jobs {

struct IoRequest {
    IoRequest* next = nullptr;
    void (*run)(IoRequest* self) = nullptr; // executed on the I/O thread
};

class IoThread {
public:
    IoThread();
    ~IoThread(); // Shutdown()

    IoThread(const IoThread&) = delete;
    IoThread& operator=(const IoThread&) = delete;

    // Any thread. The request must stay valid until its run() has
    // finished. Returns false (request NOT queued, run() never called) if
    // the thread is shutting down; the caller must then complete the
    // request itself.
    [[nodiscard]] bool Enqueue(IoRequest* request);

    // Runs everything already queued, then joins. Idempotent and safe to
    // call concurrently: every caller returns only after the thread has
    // been joined.
    void Shutdown();

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

// Owns the bytes read. Move-only.
class FileReadResult {
public:
    FileReadResult() = default;
    ~FileReadResult();
    FileReadResult(FileReadResult&& other) noexcept;
    FileReadResult& operator=(FileReadResult&& other) noexcept;
    FileReadResult(const FileReadResult&) = delete;
    FileReadResult& operator=(const FileReadResult&) = delete;

    bool ok = false;
    const u8* data = nullptr; // valid while this object lives
    usize size = 0;

private:
    friend class ReadFileAwaitable;
    ReadState* state_ = nullptr;
};

class ReadFileAwaitable {
public:
    ReadFileAwaitable(ReadFileAwaitable&& other) noexcept : state_(other.state_) { other.state_ = nullptr; }
    ReadFileAwaitable(const ReadFileAwaitable&) = delete;
    ReadFileAwaitable& operator=(const ReadFileAwaitable&) = delete;
    ~ReadFileAwaitable();

    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> h) noexcept;
    FileReadResult await_resume() noexcept;

private:
    friend ReadFileAwaitable ReadFileAsync(Scheduler&, StringView, usize);
    explicit ReadFileAwaitable(ReadState* state) : state_(state) {}

    ReadState* state_;
};

inline constexpr usize kDefaultMaxReadBytes = usize{64} << 20; // address space reserved per request
inline constexpr usize kMaxAsyncPathBytes = 1023;

// Starts nothing until awaited. Files larger than `max_bytes`, paths
// longer than kMaxAsyncPathBytes, missing/unreadable files all yield
// ok == false.
ReadFileAwaitable ReadFileAsync(Scheduler& scheduler, StringView path, usize max_bytes = kDefaultMaxReadBytes);

} // namespace engine_placeholder::jobs
