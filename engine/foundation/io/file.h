#pragma once

#include "sync/atomic.h"

#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "io/error.h"
#include "io/path.h"
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "string/string.h"
#include "types.h"

//! [[io::File]]: an open file as an abstract interface, so the same code reads a local file, a pak entry or a file on a
//! server. Only the local file exists now. Also the helpers that work through any `File`, the scheme registry that
//! picks a backend, and [[io::WriteFileAtomic(const Path&,const void*,usize,u32)]].
namespace vse::io
{

/// The log channel of the file layer (`io`).
extern LogChannel LogIo;

/// How a file is opened.
enum class OpenMode : u8
{
    Read,       ///< Read only; the file must exist.
    Write,      ///< Write only; created if missing, truncated if present.
    Append,     ///< Write only, every write goes to the end; created if missing. Offset writes are not supported.
    ReadWrite,  ///< Read and write; the file must exist unless a flag creates it.
};

/// Extra options for [[io::File::Open]], combined with `|`.
enum OpenFlag : u32
{
    kOpenNone = 0,       ///< No extra options.
    kOpenCreate = 1,     ///< Create the file when it is missing (already implied by `Write` and `Append`).
    kOpenCreateNew = 2,  ///< Create the file and fail with `AlreadyExists` when it exists.
    kOpenTruncate = 4,   ///< Empty an existing file (already implied by `Write`).
};

/// What a backend can do; test the bits of [[io::File::Capabilities]].
enum FileCapability : u32
{
    kCapSeekable = 1,   ///< Reads and writes at arbitrary offsets work.
    kCapSizeKnown = 2,  ///< [[io::File::Size]] succeeds. A size can be wrong for files the OS generates (`/proc`): read
                        ///< until 0.
    kCapWritable = 4,   ///< The file was opened for writing.
    kCapNativelyAsync = 8,   ///< The backend completes asynchronous operations on its own (a network file), not on an
                             ///< executor.
    kCapAtomicReplace = 16,  ///< The backend can replace a whole file atomically (see [[io::WriteFileAtomic(const
                             ///< Path&,const void*,usize,u32)]]).
    kCapAppendOnly = 32,     ///< Opened with `OpenMode::Append`: writes go to the end and offset writes fail.
};

class File;

/// An owning pointer to an open [[io::File]]; what [[io::File::Open]] hands out.
using FilePtr = UniquePtr<File>;

/// A unit of work queued on an [[io::IoExecutor]]; embed it as the first member of the request type. The queue owns
/// `next` while the work is queued.
///
/// - Thread-safety: thread-compatible. Ownership: owned by the submitter, which keeps it valid until `run` has
/// returned.
struct IoWork
{
    IoWork* next = nullptr;  ///< Managed by the executor.
    void (*run)(IoWork* self) =
        nullptr;  ///< Called once on an executor thread; it may free the work as its last action.
};

/// Something that runs [[io::IoWork]] on another thread: the seam that lets the foundation do asynchronous I/O without
/// depending on the job system. `jobs::IoThread` implements it.
///
/// The executor promises that work accepted by `Submit` is always run, in submission order per executor thread, even
/// during shutdown (drain). After shutdown `Submit` returns false and the caller completes the work itself.
///
/// - Thread-safety: thread-safe.
class IoExecutor
{
public:
    virtual ~IoExecutor() = default;
    /// Queues `work`. Returns false, without queueing, once the executor is shut down.
    [[nodiscard]] virtual bool Submit(IoWork* work) = 0;
};

/// Sets the executor files use for asynchronous operations when the backend is not natively asynchronous. `nullptr`
/// restores the built-in single-thread executor, which is created on first use. The executor must outlive every file
/// with requests in flight.
///
/// - Thread-safety: thread-safe.
void SetDefaultExecutor(IoExecutor* executor);
/// Clears the default executor if it is `executor` (an executor calls this before it goes away, so the process never
/// keeps a pointer to a dead one). Does nothing when another executor is installed.
void ClearDefaultExecutorIf(IoExecutor* executor);
/// Returns the current default executor (the built-in one when none was set).
IoExecutor& DefaultExecutor();

/// The outcome of an asynchronous operation.
struct AsyncResult
{
    Status status;    ///< Success or why it failed (`Interrupted` for a cancelled request or a shut-down executor).
    usize bytes = 0;  ///< Bytes transferred (reads and writes).
};

/// Called exactly once when an asynchronous operation finishes, on an unspecified thread (the executor's or the
/// backend's own; only an operation that fails before it starts completes on the caller's thread, from inside the start
/// call). It must not block for long. It may start further requests, and destroy the request. It may destroy the file
/// only if no other request on that file is in flight (the destructor waits for them, and they may be queued behind
/// this very completion).
using Completion = void (*)(void* user, const AsyncResult& result);

/// What an [[io::AsyncRequest]] does.
enum class AsyncKind : u8
{
    Read,   ///< Read into a buffer at an offset.
    Write,  ///< Write a buffer at an offset.
    Flush,  ///< Flush buffered data.
};

/// Progress of an [[io::AsyncRequest]], for backends.
enum AsyncState : u8
{
    kAsyncPending = 0,    ///< Started, not yet running.
    kAsyncRunning = 1,    ///< A thread is performing it.
    kAsyncCancelled = 2,  ///< Cancelled before it ran; it completes with `Interrupted`.
    kAsyncDone = 3,       ///< Completed.
};

/// Caller-owned state of one asynchronous operation, so starting one allocates nothing. Fill it with
/// [[io::File::ReadAsync]], [[io::File::WriteAsync]] or [[io::File::FlushAsync]]; it, and the buffer it names, must
/// stay valid and unmoved until the completion has been called. Do not reuse it before then.
///
/// - Thread-safety: thread-compatible; the file layer owns the fields from the start call until the completion.
struct AsyncRequest
{
    IoWork work;                       ///< First member: executors see the request as plain work.
    File* file = nullptr;              ///< The file; set by the start call.
    AsyncKind kind = AsyncKind::Read;  ///< The operation.
    void* buffer = nullptr;            ///< Read destination.
    const void* data = nullptr;        ///< Write source.
    usize length = 0;                  ///< Bytes to transfer.
    u64 offset = 0;                    ///< File offset.
    Completion done = nullptr;         ///< Called once with `user` and the result.
    void* user = nullptr;              ///< Passed to `done`.
    Atomic<u8> state{kAsyncPending};   ///< An [[io::AsyncState]], managed by the file layer.
};

/// An open file. Implemented per backend; use [[io::File::Open]] to get one.
///
/// Offsets are 64-bit. `Read` and `Write` at an offset carry no shared state, so many threads may read different (or
/// the same) offsets of one file at once. The stream operations (`Seek`, `Tell`, the offset-less `Read` and `Write`)
/// share one position: use them from one thread. No operation exposes a descriptor or handle.
///
/// After `Close`, every operation except `Close` fails with `Invalid`; `Close` is idempotent. The destructor closes the
/// file and logs a failed close (it cannot report it): call `Close` yourself where the result matters.
///
/// - Thread-safety: thread-compatible; offset reads and writes may run concurrently, everything else needs external
/// synchronization.
/// - Ownership: owned through `FilePtr`; not copyable.
class File
{
public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    /// Reports a file destroyed with requests still in flight (a backend must call `WaitForAsyncRequests` first).
    virtual ~File() { CheckNoRequestsInFlight(); }

    /// Opens the local file `path` (the path is never read as a URL; see [[io::File::OpenLocation]]).
    ///
    /// - Param path: the file.
    /// - Param mode: how to open it.
    /// - Param flags: [[io::OpenFlag]] bits.
    /// - Param out: receives the file; unchanged on failure.
    /// A FIFO opened for reading blocks until a writer appears; a terminal device does not become the controlling
    /// terminal. Nothing is locked: the same file can be open any number of times at once, in any modes, and renamed,
    /// replaced or removed while open (engine/foundation/io/BACKEND.md, "Sharing").
    ///
    /// - Fails: `NotFound`, `PermissionDenied`, `IsADirectory` (also when the path names a directory and the mode is
    /// `Read`), `AlreadyExists`
    ///   (`kOpenCreateNew`), `Invalid` (`OpenMode::Read` with `kOpenCreate`, `kOpenCreateNew` or `kOpenTruncate`; on
    ///   Windows also a reserved device name, a name ending in a dot or a space, or a colon in a name),
    ///   `NotADirectory`, `TooManyOpenFiles`, `SymlinkLoop`, `TooLong`, `Busy` (Windows: another program opened the
    ///   file without sharing).
    /// - Backend: Windows: CreateFileW, asynchronous work on the executor as on Linux (BACKEND.md, Open); macOS: open
    /// with O_CLOEXEC, asynchronous work on the executor as on Linux (dispatch_io measured slower).
    static Status Open(const Path& path, OpenMode mode, u32 flags, FilePtr* out);
    /// Opens `location`, which is either a local path or `scheme://rest`. `file:///abs/path` (percent escapes such as
    /// `%20` are decoded; no query or fragment) and a text without a scheme are local files; other schemes go to the
    /// backend registered with [[io::RegisterFileScheme]].
    ///
    /// - Param location: path text or URL.
    /// - Fails: `Unsupported` for a scheme nobody registered, `Invalid` for a malformed percent escape or an encoded
    /// NUL byte, plus the errors of [[io::File::Open]].
    static Status OpenLocation(StringView location, OpenMode mode, u32 flags, FilePtr* out);

    /// Returns true until the file is closed.
    virtual bool IsOpen() const = 0;
    /// Returns the [[io::FileCapability]] bits of this file.
    virtual u32 Capabilities() const = 0;
    /// Returns the size in bytes.
    ///
    /// - Fails: `Unsupported` when the size is not known (see `kCapSizeKnown`), `Invalid` after `Close`.
    virtual Status Size(u64* out) = 0;
    /// Reads up to `length` bytes at `offset`. Returns fewer bytes than asked only at the end of the file or for a
    /// reason the backend has; zero bytes mean the end. A short read is not an error: use [[io::ReadExact]] when you
    /// need all of it.
    ///
    /// - Param buffer: destination, at least `length` bytes.
    /// - Param bytes_read: receives the count (also on failure after a partial transfer: zero).
    /// - Fails: `Invalid` (closed, or not readable), `Io`, `Interrupted`.
    virtual Status Read(void* buffer, usize length, u64 offset, usize* bytes_read) = 0;
    /// Writes all `length` bytes at `offset`, completing partial writes.
    ///
    /// - Fails: `Invalid` (closed or not writable), `Unsupported` (an `Append`-mode file), `NoSpace`, `Io`.
    virtual Status Write(const void* data, usize length, u64 offset) = 0;
    /// Writes all `length` bytes at the end of the file. Only files opened with `OpenMode::Append` support it.
    ///
    /// - Fails: `Unsupported` (not opened for appending), `Invalid`, `NoSpace`, `Io`.
    virtual Status Append(const void* data, usize length) = 0;
    /// Pushes data the backend buffers to the operating system. A local file has no user-space buffer, so this
    /// succeeds.
    virtual Status Flush() = 0;
    /// Pushes the data to the storage device (`fsync`); returns when it is durable.
    ///
    /// On macOS the local file uses `fcntl(F_FULLFSYNC)`: `fsync` there only hands the data to the drive, whose cache
    /// can still lose it. A file system that refuses `F_FULLFSYNC` (some network and FAT volumes) gets `fsync`. It
    /// costs far more: 18 ms against 0.4 ms for 4 KiB on the development Mac's SSD.
    /// - Fails: `Unsupported`, `Io`, `NoSpace`.
    virtual Status Sync() = 0;
    /// Sets the size, cutting data off or extending with zeros.
    ///
    /// - Fails: `Invalid` (closed or not writable), `Unsupported`, `NoSpace`.
    virtual Status Truncate(u64 size) = 0;
    /// Closes the file. Idempotent: later calls succeed and do nothing.
    ///
    /// - Fails: `Io` or `NoSpace` when the system reports a deferred write error at close.
    virtual Status Close() = 0;

    /// Moves the stream position (used by the offset-less `Read` and `Write`).
    ///
    /// - Fails: `Unsupported` without `kCapSeekable`, `Invalid` after `Close`.
    Status Seek(u64 position);
    /// Returns the stream position.
    u64 Tell() const { return position_; }
    /// Reads at the stream position and advances it by the bytes read; `Read` rules apply.
    Status Read(void* buffer, usize length, usize* bytes_read);
    /// Writes all bytes at the stream position and advances it; on an `Append`-mode file it appends.
    Status Write(const void* data, usize length);
    /// Reads into `buffer` at the stream position; as `Read(void*,usize,usize*)` with the span's size.
    Status Read(MutableByteSpan buffer, usize* bytes_read) { return Read(buffer.Data(), buffer.Size(), bytes_read); }
    /// Writes all of `data` at the stream position; as `Write(const void*,usize)`.
    Status Write(ByteSpan data) { return Write(data.Data(), data.Size()); }

    /// Starts reading up to `length` bytes at `offset` into `buffer`; `request` completes with the bytes read (`Read`
    /// rules: fewer is allowed, zero is the end). Returns at once. The buffer and `request` must stay valid until
    /// `done` has run. Many requests may be in flight on one file; they may complete in any order (an in-order backend
    /// completes them in order).
    ///
    /// - Param request: caller-owned; must not be in flight.
    /// - Param done: called exactly once with `user`; must not be null.
    /// - Aborts: `request` or `done` is null.
    void ReadAsync(AsyncRequest* request, void* buffer, usize length, u64 offset, Completion done, void* user);
    /// Starts writing all `length` bytes at `offset`; the data must stay valid until `done` has run. `Write` rules
    /// apply.
    ///
    /// - Aborts: `request` or `done` is null.
    void WriteAsync(AsyncRequest* request, const void* data, usize length, u64 offset, Completion done, void* user);
    /// Starts a [[io::File::Flush]].
    ///
    /// - Aborts: `request` or `done` is null.
    void FlushAsync(AsyncRequest* request, Completion done, void* user);
    /// Asks for `request` not to run. Best effort: it succeeds only while the request has not started, and the request
    /// still completes (exactly once) with `Interrupted`. Returns false when the request is already running or
    /// finished. Call it only while the request is certainly not complete (the completion may free the request, and
    /// `Cancel` reads it): for example while holding a lock the completion also takes.
    virtual bool Cancel(AsyncRequest* request) { return CancelPending(request); }

protected:
    /// Starts a prepared request. The default runs it on [[io::DefaultExecutor]] using the synchronous operations, so
    /// every backend gets asynchronous I/O; a natively asynchronous backend overrides this and calls
    /// [[io::File::CompleteAsync]] when done. A backend completes a request from `StartAsync` itself only to report a
    /// failure; success is completed from another thread, so that a chain of requests started from completions (such as
    /// `jobs::ReadAllAsync`) cannot recurse without bound.
    virtual void StartAsync(AsyncRequest* request) { StartOnExecutor(request); }
    /// For backends: cancels a request that has not started (the default of [[io::File::Cancel]]).
    static bool CancelPending(AsyncRequest* request);
    /// For backends: runs a prepared request on [[io::DefaultExecutor]] with the synchronous operations (the default of
    /// `StartAsync`).
    void StartOnExecutor(AsyncRequest* request);
    /// For backends: moves a pending request to running; returns false when it was cancelled (complete it with
    /// `Interrupted`).
    static bool ClaimAsync(AsyncRequest* request);
    /// For backends: finishes `request` and calls its completion exactly once. Touches nothing of the file afterwards,
    /// so the completion may destroy it.
    void CompleteAsync(AsyncRequest* request, const AsyncResult& result);
    /// For backends: call at the start of the destructor. Waits until every started request has completed (or begun
    /// completing).
    void WaitForAsyncRequests();

private:
    // Every virtual member is defined inline, so the class has no key function and each translation unit that derives
    // from it (including RTTI-enabled test code, against the RTTI-free library) emits its own type information.
    void CheckNoRequestsInFlight();
    static void RunOnExecutor(IoWork* work);

    u64 position_ = 0;
    Atomic<u32> in_flight_{0};
};

/// Creates the backend object for a URL scheme: `location` is the whole text (`scheme://rest`).
using FileFactory = Status (*)(StringView location, OpenMode mode, u32 flags, FilePtr* out);

/// Registers a backend for `scheme` (lower case letters, digits, `+`, `-`, `.`; first a letter).
///
/// - Param scheme: for example `pak` or `http`; `file` is reserved.
/// - Fails: `Invalid` (a bad or reserved scheme, a null factory), `AlreadyExists`, `OutOfMemory` (the registry holds
/// 16).
/// - Thread-safety: thread-safe.
Status RegisterFileScheme(StringView scheme, FileFactory factory);
/// Removes a registration; unknown schemes are ignored. Files already opened are unaffected.
void UnregisterFileScheme(StringView scheme);

/// Reads exactly `length` bytes at `offset`, looping over short reads.
///
/// - Fails: `EndOfFile` when the file ends first (`buffer` holds what was read), plus the errors of
/// [[io::File::Read(void*,usize,u64,usize*)]].
Status ReadExact(File& file, void* buffer, usize length, u64 offset);
/// Reads from `offset 0` to the end into `out` (replacing its contents), trusting no size: it reads until a read
/// returns zero.
///
/// - Param max_bytes: refuse files longer than this.
/// - Fails: `OutOfMemory` when the vector cannot grow, `TooLong` when `max_bytes` is exceeded, plus the errors of
/// [[io::File::Read(void*,usize,u64,usize*)]].
Status ReadAll(File& file, Vector<u8>* out, u64 max_bytes = u64{1} << 32);
/// Like the vector overload but allocates the contents from `arena` (the replacement of `os::ReadFileToArena`).
///
/// - Param data: receives the arena block; `size` the byte count. A zero-length file gives a valid pointer and size 0.
/// - Fails: `OutOfMemory` when the arena is full, `TooLong` above `max_bytes`, plus the errors of
/// [[io::File::Read(void*,usize,u64,usize*)]].
/// - Ownership: the block belongs to `arena`.
Status ReadAll(File& file, ArenaAllocator& arena, u8** data, usize* size, u64 max_bytes = u64{1} << 32);

/// Opens `path`, reads it whole into `out`, closes it.
///
/// - Fails: the errors of [[io::File::Open]] and [[io::ReadAll(File&,Vector<u8>*,u64)]].
Status ReadFile(const Path& path, Vector<u8>* out, u64 max_bytes = u64{1} << 32);
/// Opens `path`, reads it whole into the arena, closes it (the path-based replacement of `os::ReadFileToArena`).
///
/// - Fails: as [[io::ReadFile(const Path&,Vector<u8>*,u64)]].
Status ReadFile(const Path& path, ArenaAllocator& arena, u8** data, usize* size, u64 max_bytes = u64{1} << 32);
/// Reads a whole text file into `out`. The bytes are not validated as UTF-8.
///
/// - Fails: as [[io::ReadFile(const Path&,Vector<u8>*,u64)]].
Status ReadTextFile(const Path& path, String<HeapAllocator>* out, u64 max_bytes = u64{1} << 28);
/// Creates or truncates `path` and writes `length` bytes. Not atomic: a failure can leave a partial file; see
/// [[io::WriteFileAtomic(const Path&,const void*,usize,u32)]].
///
/// - Fails: the errors of [[io::File::Open]] and [[io::File::Write(const void*,usize,u64)]].
Status WriteFile(const Path& path, const void* data, usize length);
/// Like [[io::WriteFile(const Path&,const void*,usize)]] for a span of bytes.
inline Status WriteFile(const Path& path, ByteSpan data)
{
    return WriteFile(path, data.Data(), data.Size());
}
/// Replaces the contents of `path` so that a reader sees the old file or the new one, never a mix, and a crash leaves
/// one of them.
///
/// Writes a temporary file in the same directory, flushes and syncs it, then renames it over `path`. The temporary is
/// removed on every failure path, and the directory is synced after the rename so the new entry survives a crash. A
/// symbolic link at `path` is followed (the file it points to is replaced, the link stays). The replacement is a new
/// file: a replaced file keeps its permission bits, but its owner, hard links, ACLs and extended attributes are not
/// carried over (the writing user owns the result). A new file gets `new_file_permissions` (private to the owner by
/// default: pass 0644 for ordinary generated files).
///
/// - Param path: the target; its directory must exist.
/// - Param new_file_permissions: the permission bits of a file that did not exist.
/// - Fails: `NotFound` (no directory), `PermissionDenied`, `NoSpace`, `IsADirectory` (the target is a directory), `Io`;
/// the old content is intact.
Status WriteFileAtomic(const Path& path, const void* data, usize length, u32 new_file_permissions = 0600);
/// Like [[io::WriteFileAtomic(const Path&,const void*,usize,u32)]] for a span of bytes.
inline Status WriteFileAtomic(const Path& path, ByteSpan data, u32 new_file_permissions = 0600)
{
    return WriteFileAtomic(path, data.Data(), data.Size(), new_file_permissions);
}

namespace detail
{
/// Which step of [[io::WriteFileAtomic(const Path&,const void*,usize,u32)]] a test makes fail: 0 none, 1 write, 2
/// flush, 3 sync, 4 rename.
void SetAtomicWriteFailureStep(int step);
}  // namespace detail

}  // namespace vse::io
