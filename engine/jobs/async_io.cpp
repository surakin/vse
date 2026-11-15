#include "jobs/async_io.h"

#include "log/diagnostics_hooks.h"
#include "jobs/frame_pool.h"
#include "jobs/jobs_log.h"
#include "jobs/scheduler.h"
#include "jobs/worker_pool.h"
#include "log/assert.h"
#include "io/file.h"
#include "os/os.h"
#include "lang/utility.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::jobs
{

// ---- IoThread -----------------------------------------------------------

IoThread::IoThread()
{
    thread_ = Thread([this] { Loop(); });
    thread_id_ = thread_.Id();
}

IoThread::~IoThread()
{
    Shutdown();
}  // Shutdown also withdraws this thread as the default file executor

bool IoThread::Enqueue(IoRequest* request)
{
    request->next = nullptr;
    {
        LockGuard<Mutex> lock(mutex_);
        if (stopping_)
        {
            return false;
        }
        if (tail_ != nullptr)
        {
            tail_->next = request;
        }
        else
        {
            head_ = request;
        }
        tail_ = request;
    }
    cv_.NotifyOne();
    return true;
}

void IoThread::Shutdown()
{
    io::ClearDefaultExecutorIf(this);  // files must not be handed to a stopped executor
    LockGuard<Mutex> serialize(shutdown_mutex_);
    {
        LockGuard<Mutex> lock(mutex_);
        if (joined_)
        {
            return;
        }
        joined_ = true;
        stopping_ = true;
    }
    cv_.NotifyAll();
    if (thread_.Joinable())
    {
        thread_.Join();
    }
}

void IoThread::Loop()
{
    NotifyThreadStart("io");
    for (;;)
    {
        IoRequest* req = nullptr;
        {
            UniqueLock<Mutex> lock(mutex_);
            cv_.Wait(lock, [&] { return head_ != nullptr || stopping_; });
            if (head_ == nullptr)
            {
                break;  // stopping and drained
            }
            req = head_;
            head_ = req->next;
            if (head_ == nullptr)
            {
                tail_ = nullptr;
            }
        }
        req->run(req);  // may free *req (the awaiter's state) once it decrements
    }
    FramePool::ReleaseThreadCache();  // no thread_local destructor does it (the engine library must stay unloadable)
    NotifyThreadEnd();
}

// ---- read request state ---------------------------------------------------

struct ReadState : IoRequest
{
    Counter counter;
    char path[kMaxAsyncPathBytes + 1];
    usize path_len = 0;
    bool path_too_long = false;
    usize max_bytes;
    Scheduler* scheduler;
    // The arena is sized from the file's real length on the I/O thread
    // (not reserved up front for every request), so it is built lazily.
    alignas(ArenaAllocator) byte arena_storage[sizeof(ArenaAllocator)];
    ArenaAllocator* arena = nullptr;
    // Result, written by the I/O thread before the counter decrement.
    bool ok = false;
    u8* data = nullptr;
    usize size = 0;

    ReadState(Scheduler& s, usize max) : counter(s), max_bytes(max), scheduler(&s) { run = &Run; }
    ~ReadState()
    {
        if (arena != nullptr)
        {
            arena->~ArenaAllocator();
        }
    }

    static void Run(IoRequest* self)
    {
        ReadState* st = static_cast<ReadState*>(self);
        io::Path path;
        io::FilePtr file;
        u64 length = 0;
        if (io::Path::Parse(StringView(st->path, st->path_len), io::NativePathStyle(), &path) &&
            io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &file).Ok() && file->Size(&length).Ok() &&
            length <= st->max_bytes)
        {
            // +1 so an empty file still gets a valid non-null buffer; the size is only a hint, a longer file simply
            // fails.
            st->arena = new (st->arena_storage) ArenaAllocator(static_cast<usize>(length) + 1);
            u8* data = nullptr;
            usize size = 0;
            io::Status status = io::ReadAll(*file, *st->arena, &data, &size, st->max_bytes);
            if (status.Is(io::Error::OutOfMemory))
            {
                // The size was only a hint (a /proc file reports 0, a file may have grown): read it whole, then size
                // the arena for it.
                Vector<u8> bytes;
                status = io::ReadAll(*file, &bytes, st->max_bytes);
                if (status.Ok())
                {
                    st->arena->~ArenaAllocator();
                    st->arena = new (st->arena_storage) ArenaAllocator(bytes.Size() + 1);
                    data = static_cast<u8*>(st->arena->Alloc(bytes.Size() == 0 ? 1 : bytes.Size(), 1));
                    if (data == nullptr)
                    {
                        status = io::Status::Of(io::Error::OutOfMemory);
                    }
                    else
                    {
                        if (bytes.Size() != 0)
                        {
                            MemCopy(data, bytes.Data(), bytes.Size());
                        }
                        size = bytes.Size();
                    }
                }
            }
            st->ok = status.Ok();
            st->data = status.Ok() ? data : nullptr;
            st->size = status.Ok() ? size : 0;
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

ReadFileAwaitable ReadFileAsync(Scheduler& scheduler, StringView path, usize max_bytes)
{
    UniquePtr<ReadState> st = MakeUnique<ReadState>(scheduler, max_bytes);
    ENGINE_ASSERT(st, "ReadFileAsync: out of host memory for the request");
    if (path.Length() > kMaxAsyncPathBytes)
    {
        st->path_too_long = true;
        st->path[0] = '\0';
    }
    else
    {
        MemCopy(st->path, path.Data(), path.Length());
        st->path[path.Length()] = '\0';
        st->path_len = path.Length();
    }
    return ReadFileAwaitable(Move(st));
}

ReadFileAwaitable::ReadFileAwaitable(UniquePtr<ReadState> state) : state_(Move(state)) {}

ReadFileAwaitable::ReadFileAwaitable(ReadFileAwaitable&& other) noexcept : state_(Move(other.state_)) {}

ReadFileAwaitable::~ReadFileAwaitable() = default;

bool ReadFileAwaitable::await_suspend(CoroutineHandle<> h) noexcept
{
    ReadState* st = state_.Get();
    if (st->path_too_long)
    {
        return false;  // fail inline: ok stays false
    }
    // Count the request, hand it to the I/O thread, then wait on the
    // counter. If the read already finished, the counter's await returns
    // false and we continue inline; otherwise the I/O thread's Decrement
    // reschedules us on a participant.
    st->counter.Increment();
    if (!st->scheduler->Io().Enqueue(st))
    {
        // The I/O thread is shut down: complete the request as failed
        // right here (ok stays false); the counter then reads zero and
        // the await below continues inline.
        st->counter.Decrement();
    }
    return st->counter.operator co_await().await_suspend(h);
}

FileReadResult ReadFileAwaitable::await_resume() noexcept
{
    ReadState* st = state_.Get();
    if (!st->path_too_long)
    {
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
    r.state_ = Move(state_);  // ownership of the state moves into the result
    return r;
}

// ---- result -----------------------------------------------------------------

FileReadResult::~FileReadResult() = default;

FileReadResult::FileReadResult(FileReadResult&& o) noexcept
    : ok(o.ok), data(o.data), size(o.size), state_(Move(o.state_))
{
    o.data = nullptr;
    o.ok = false;
    o.size = 0;
}

FileReadResult& FileReadResult::operator=(FileReadResult&& o) noexcept
{
    if (this != &o)
    {
        ok = o.ok;
        data = o.data;
        size = o.size;
        state_ = Move(o.state_);
        o.data = nullptr;
        o.ok = false;
        o.size = 0;
    }
    return *this;
}

}  // namespace engine_placeholder::jobs
