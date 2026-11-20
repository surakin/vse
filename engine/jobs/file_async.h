#pragma once

#include "jobs/coroutine.h"

#include "containers/vector.h"
#include "io/file.h"
#include "jobs/counter.h"
#include "types.h"

//! Awaitable adapters over the asynchronous operations of [[io::File]].
//!
//! `co_await ReadAsync(scheduler, file, buffer, length, offset)` starts the read and parks the job; the continuation
//! resumes on a scheduler participant, never on the I/O thread, as with [[jobs::ReadFileAsync]]. The file does its work
//! on its own executor (see [[io::SetDefaultExecutor]]; pass the scheduler's [[jobs::IoThread]] so blocking reads stay
//! off the workers).
//!
//! The awaitables are not movable once created: await the temporary (`co_await ReadAsync(...)`).
namespace vse::jobs
{

class Scheduler;

/// Awaitable for one read, write or flush; awaiting yields the [[io::AsyncResult]].
///
/// - Thread-safety: not-thread-safe; belongs to the awaiting coroutine.
/// - Ownership: borrows the file and the buffer, which must stay valid until the await returns.
class FileIoAwaitable
{
public:
    FileIoAwaitable(const FileIoAwaitable&) = delete;
    FileIoAwaitable& operator=(const FileIoAwaitable&) = delete;

    /// Tells the coroutine machinery whether suspension can be skipped.
    ///
    /// - Returns: always false; the operation is started in `await_suspend`.
    bool await_ready() const noexcept { return false; }
    /// Starts the operation and parks the coroutine until it completes.
    ///
    /// - Returns: false when it already finished (continue inline), true when parked.
    bool await_suspend(CoroutineHandle<> h) noexcept;
    /// Collects the result.
    ///
    /// - Aborts: resumed on a thread that is not a scheduler participant.
    io::AsyncResult await_resume() noexcept;

private:
    friend FileIoAwaitable ReadAsync(Scheduler&, io::File&, void*, usize, u64);
    friend FileIoAwaitable WriteAsync(Scheduler&, io::File&, const void*, usize, u64);
    friend FileIoAwaitable FlushAsync(Scheduler&, io::File&);
    FileIoAwaitable(Scheduler& scheduler, io::File& file, io::AsyncKind kind, void* buffer, const void* data,
                    usize length, u64 offset)
        : counter_(scheduler), file_(&file), kind_(kind), buffer_(buffer), data_(data), length_(length), offset_(offset)
    {
    }
    static void OnDone(void* user, const io::AsyncResult& result);

    Counter counter_;
    io::File* file_;
    io::AsyncKind kind_;
    void* buffer_;
    const void* data_;
    usize length_;
    u64 offset_;
    io::AsyncRequest request_;
    io::AsyncResult result_;
};

/// Reads up to `length` bytes at `offset` (`io::File::Read` rules: fewer is allowed, zero is the end).
///
/// - Ownership: borrows the file and the buffer until the await returns.
/// - Thread-safety: thread-safe. Await it from a job.
FileIoAwaitable ReadAsync(Scheduler& scheduler, io::File& file, void* buffer, usize length, u64 offset);
/// Writes all `length` bytes at `offset`.
FileIoAwaitable WriteAsync(Scheduler& scheduler, io::File& file, const void* data, usize length, u64 offset);
/// Flushes the file.
FileIoAwaitable FlushAsync(Scheduler& scheduler, io::File& file);

/// What [[jobs::ReadAllAsync]] produces.
///
/// - Thread-safety: not-thread-safe; a value owned by the awaiting coroutine.
/// - Ownership: owns the bytes.
struct FileReadAllResult
{
    io::Status status;  ///< Success, or why the read stopped; `bytes` holds what was read before an error.
    Vector<u8> bytes;   ///< The file contents from offset 0 to the end.
};

/// Awaitable that reads a whole file in chunks, asynchronously, and yields a [[jobs::FileReadAllResult]]. The size is
/// never trusted: it reads until a read returns zero.
///
/// - Thread-safety: not-thread-safe; belongs to the awaiting coroutine. Not movable once created.
class FileReadAllAwaitable
{
public:
    FileReadAllAwaitable(const FileReadAllAwaitable&) = delete;
    FileReadAllAwaitable& operator=(const FileReadAllAwaitable&) = delete;

    /// Tells the coroutine machinery whether suspension can be skipped.
    ///
    /// - Returns: always false.
    bool await_ready() const noexcept { return false; }
    /// Starts the first read and parks the coroutine.
    ///
    /// - Returns: false when it already finished (continue inline), true when parked.
    bool await_suspend(CoroutineHandle<> h) noexcept;
    /// Collects the bytes.
    ///
    /// - Aborts: resumed on a thread that is not a scheduler participant.
    FileReadAllResult await_resume() noexcept;

private:
    friend FileReadAllAwaitable ReadAllAsync(Scheduler&, io::File&, u64);
    FileReadAllAwaitable(Scheduler& scheduler, io::File& file, u64 max_bytes)
        : counter_(scheduler), file_(&file), max_bytes_(max_bytes)
    {
    }
    void Issue();
    static void OnChunk(void* user, const io::AsyncResult& result);

    static constexpr usize kChunk = 64 * 1024;
    Counter counter_;
    io::File* file_;
    u64 max_bytes_;
    u64 offset_ = 0;
    io::Status status_;
    Vector<u8> bytes_;
    io::AsyncRequest request_;
    Vector<u8> chunk_;  // allocated when the read starts, so the awaiting coroutine's frame stays small
};

/// Reads the whole of `file` asynchronously.
///
/// - Param max_bytes: stop with `TooLong` beyond this size.
/// - Thread-safety: thread-safe. Await it from a job.
FileReadAllAwaitable ReadAllAsync(Scheduler& scheduler, io::File& file, u64 max_bytes = u64{1} << 30);

}  // namespace vse::jobs
