#include "jobs/async_io.h"

#include <fcntl.h>

#include <cstdio>
#include <cstring>
#include <new>

#include "jobs/jobs_log.h"
#include "jobs/scheduler.h"
#include "jobs/worker_pool.h"
#include "log/assert.h"
#include "os/file_util.h"
#include "os/os.h"

namespace engine_placeholder::jobs {

// ---- IoThread -----------------------------------------------------------

IoThread::IoThread() {
    thread_ = std::thread([this] { Loop(); });
    thread_id_ = thread_.get_id();
}

IoThread::~IoThread() { Shutdown(); }

bool IoThread::Enqueue(IoRequest* request) {
    request->next = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        if (tail_ != nullptr) {
            tail_->next = request;
        } else {
            head_ = request;
        }
        tail_ = request;
    }
    cv_.notify_one();
    return true;
}

void IoThread::Shutdown() {
    std::lock_guard<std::mutex> serialize(shutdown_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (joined_) return;
        joined_ = true;
        stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void IoThread::Loop() {
    for (;;) {
        IoRequest* req = nullptr;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return head_ != nullptr || stopping_; });
            if (head_ == nullptr) return; // stopping and drained
            req = head_;
            head_ = req->next;
            if (head_ == nullptr) tail_ = nullptr;
        }
        req->run(req); // may free *req (the awaiter's state) once it decrements
    }
}

// ---- read request state ---------------------------------------------------

struct ReadState : IoRequest {
    Counter counter;
    char path[kMaxAsyncPathBytes + 1];
    usize path_len = 0;
    bool path_too_long = false;
    usize max_bytes;
    Scheduler* scheduler;
    // The arena is sized from the file's real length on the I/O thread
    // (not reserved up front for every request), so it is built lazily.
    alignas(ArenaAllocator) unsigned char arena_storage[sizeof(ArenaAllocator)];
    ArenaAllocator* arena = nullptr;
    // Result, written by the I/O thread before the counter decrement.
    bool ok = false;
    u8* data = nullptr;
    usize size = 0;

    ReadState(Scheduler& s, usize max) : counter(s), max_bytes(max), scheduler(&s) { run = &Run; }
    ~ReadState() {
        if (arena != nullptr) arena->~ArenaAllocator();
    }

    // File length via the portable os:: primitives, or -1.
    static long LengthOf(StringView path) {
        int fd = os::Open(path, O_RDONLY);
        if (fd < 0) return -1;
        long end = os::Lseek(fd, 0, SEEK_END);
        os::Close(fd);
        return end;
    }

    static void Run(IoRequest* self) {
        ReadState* st = static_cast<ReadState*>(self);
        StringView path(st->path, st->path_len);
        long length = LengthOf(path);
        if (length >= 0 && static_cast<usize>(length) <= st->max_bytes) {
            // +1 so an empty file still gets a valid non-null buffer.
            st->arena = new (st->arena_storage) ArenaAllocator(static_cast<usize>(length) + 1);
            usize size = 0;
            u8* data = os::ReadFileToArena(path, *st->arena, size);
            st->ok = data != nullptr;
            st->data = data;
            st->size = data != nullptr ? size : 0;
        }
        JobsLog(LogVerbosity::Debug, "io thread: read of %s %s (%zu bytes)", st->path, st->ok ? "ok" : "failed",
                static_cast<size_t>(st->size));
        // Last touch of *st by this thread (the awaiter may free it once
        // it has been resumed): decrementing routes the continuation
        // through the injection queue because this thread is not a
        // participant.
        st->counter.Decrement();
    }
};

// ---- awaitable --------------------------------------------------------------

ReadFileAwaitable ReadFileAsync(Scheduler& scheduler, StringView path, usize max_bytes) {
    UniquePtr<ReadState> st = MakeUnique<ReadState>(scheduler, max_bytes);
    ENGINE_ASSERT(st, "ReadFileAsync: out of host memory for the request");
    if (path.Length() > kMaxAsyncPathBytes) {
        st->path_too_long = true;
        st->path[0] = '\0';
    } else {
        std::memcpy(st->path, path.Data(), path.Length());
        st->path[path.Length()] = '\0';
        st->path_len = path.Length();
    }
    return ReadFileAwaitable(std::move(st));
}

ReadFileAwaitable::ReadFileAwaitable(UniquePtr<ReadState> state) : state_(std::move(state)) {}

ReadFileAwaitable::ReadFileAwaitable(ReadFileAwaitable&& other) noexcept : state_(std::move(other.state_)) {}

ReadFileAwaitable::~ReadFileAwaitable() = default;

bool ReadFileAwaitable::await_suspend(std::coroutine_handle<> h) noexcept {
    ReadState* st = state_.Get();
    if (st->path_too_long) {
        return false; // fail inline: ok stays false
    }
    // Count the request, hand it to the I/O thread, then wait on the
    // counter. If the read already finished, the counter's await returns
    // false and we continue inline; otherwise the I/O thread's Decrement
    // reschedules us on a participant.
    st->counter.Increment();
    if (!st->scheduler->Io().Enqueue(st)) {
        // The I/O thread is shut down: complete the request as failed
        // right here (ok stays false); the counter then reads zero and
        // the await below continues inline.
        st->counter.Decrement();
    }
    return st->counter.operator co_await().await_suspend(h);
}

FileReadResult ReadFileAwaitable::await_resume() noexcept {
    ReadState* st = state_.Get();
    if (!st->path_too_long) {
        st->counter.operator co_await().await_resume();
        // D8 contract: continuations never run on the I/O thread, always
        // on a registered participant.
        ENGINE_ASSERT(WorkerPool::IsParticipantThread(), "async I/O continuation resumed on a non-participant");
        JobsLog(LogVerbosity::Debug, "async read resumed on participant %d (io thread is a separate thread)",
                static_cast<int>(WorkerPool::CurrentParticipantIndex()));
    }
    FileReadResult r;
    r.ok = st->ok;
    r.data = st->data;
    r.size = st->size;
    r.state_ = std::move(state_); // ownership of the state moves into the result
    return r;
}

// ---- result -----------------------------------------------------------------

FileReadResult::~FileReadResult() = default;

FileReadResult::FileReadResult(FileReadResult&& o) noexcept
    : ok(o.ok), data(o.data), size(o.size), state_(std::move(o.state_)) {
    o.data = nullptr;
    o.ok = false;
    o.size = 0;
}

FileReadResult& FileReadResult::operator=(FileReadResult&& o) noexcept {
    if (this != &o) {
        ok = o.ok;
        data = o.data;
        size = o.size;
        state_ = std::move(o.state_);
        o.data = nullptr;
        o.ok = false;
        o.size = 0;
    }
    return *this;
}

} // namespace engine_placeholder::jobs
