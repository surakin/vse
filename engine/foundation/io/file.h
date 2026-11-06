#pragma once

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "io/error.h"
#include "io/path.h"
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "string/string.h"
#include "types.h"

//! [[io::File]]: an open file as an abstract interface, so the same code reads a local file, a pak entry or a file on a
//! server. Only the local file exists now. Also the helpers that work through any `File`, the scheme registry that picks a
//! backend, and [[io::WriteFileAtomic]].
namespace engine_placeholder::io {

/// The log channel of the file layer (`io`).
extern LogChannel LogIo;

/// How a file is opened.
enum class OpenMode : u8 {
    Read,      ///< Read only; the file must exist.
    Write,     ///< Write only; created if missing, truncated if present.
    Append,    ///< Write only, every write goes to the end; created if missing. Offset writes are not supported.
    ReadWrite, ///< Read and write; the file must exist unless a flag creates it.
};

/// Extra options for [[io::File::Open]], combined with `|`.
enum OpenFlag : u32 {
    kOpenNone = 0,      ///< No extra options.
    kOpenCreate = 1,    ///< Create the file when it is missing (already implied by `Write` and `Append`).
    kOpenCreateNew = 2, ///< Create the file and fail with `AlreadyExists` when it exists.
    kOpenTruncate = 4,  ///< Empty an existing file (already implied by `Write`).
};

/// What a backend can do; test the bits of [[io::File::Capabilities]].
enum FileCapability : u32 {
    kCapSeekable = 1,      ///< Reads and writes at arbitrary offsets work.
    kCapSizeKnown = 2,     ///< [[io::File::Size]] succeeds. A size can be wrong for files the OS generates (`/proc`): read until 0.
    kCapWritable = 4,      ///< The file was opened for writing.
    kCapNativelyAsync = 8, ///< The backend completes asynchronous operations on its own (a network file), not on an executor.
    kCapAtomicReplace = 16, ///< The backend can replace a whole file atomically (see [[io::WriteFileAtomic]]).
    kCapAppendOnly = 32,   ///< Opened with `OpenMode::Append`: writes go to the end and offset writes fail.
};

/// An open file. Implemented per backend; use [[io::File::Open]] to get one.
///
/// Offsets are 64-bit. `Read` and `Write` at an offset carry no shared state, so many threads may read different (or the
/// same) offsets of one file at once. The stream operations (`Seek`, `Tell`, the offset-less `Read` and `Write`) share one
/// position: use them from one thread. No operation exposes a descriptor or handle.
///
/// After `Close`, every operation except `Close` fails with `Invalid`; `Close` is idempotent. The destructor closes the file
/// and logs a failed close (it cannot report it): call `Close` yourself where the result matters.
///
/// Thread-safety: thread-compatible; offset reads and writes may run concurrently, everything else needs external synchronization.
/// Ownership: owned through `UniquePtr<File>`; not copyable.
class File {
public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    virtual ~File() = default;

    /// Opens the local file `path` (the path is never read as a URL; see [[io::File::OpenLocation]]).
    ///
    /// Param path: the file.
    /// Param mode: how to open it.
    /// Param flags: [[io::OpenFlag]] bits.
    /// Param out: receives the file; unchanged on failure.
    /// A FIFO opened for reading blocks until a writer appears; a terminal device does not become the controlling terminal.
    ///
    /// Fails: `NotFound`, `PermissionDenied`, `IsADirectory` (also when the path names a directory and the mode is `Read`), `AlreadyExists`
    ///   (`kOpenCreateNew`), `Invalid` (`OpenMode::Read` with `kOpenCreate`, `kOpenCreateNew` or `kOpenTruncate`), `NotADirectory`,
    ///   `TooManyOpenFiles`, `SymlinkLoop`, `TooLong`.
    static Status Open(const Path& path, OpenMode mode, u32 flags, UniquePtr<File>* out);
    /// Opens `location`, which is either a local path or `scheme://rest`. `file:///abs/path` (percent escapes such as `%20` are
    /// decoded; no query or fragment) and a text without a scheme are local files;
    /// other schemes go to the backend registered with [[io::RegisterFileScheme]].
    ///
    /// Param location: path text or URL.
    /// Fails: `Unsupported` for a scheme nobody registered, plus the errors of [[io::File::Open]].
    static Status OpenLocation(StringView location, OpenMode mode, u32 flags, UniquePtr<File>* out);

    /// Returns true until the file is closed.
    virtual bool IsOpen() const = 0;
    /// Returns the [[io::FileCapability]] bits of this file.
    virtual u32 Capabilities() const = 0;
    /// Returns the size in bytes.
    ///
    /// Fails: `Unsupported` when the size is not known (see `kCapSizeKnown`), `Invalid` after `Close`.
    virtual Status Size(u64* out) = 0;
    /// Reads up to `length` bytes at `offset`. Returns fewer bytes than asked only at the end of the file or for a reason the
    /// backend has; zero bytes mean the end. A short read is not an error: use [[io::ReadExact]] when you need all of it.
    ///
    /// Param buffer: destination, at least `length` bytes.
    /// Param bytes_read: receives the count (also on failure after a partial transfer: zero).
    /// Fails: `Invalid` (closed, or not readable), `Io`, `Interrupted`.
    virtual Status Read(void* buffer, usize length, u64 offset, usize* bytes_read) = 0;
    /// Writes all `length` bytes at `offset`, completing partial writes.
    ///
    /// Fails: `Invalid` (closed or not writable), `Unsupported` (an `Append`-mode file), `NoSpace`, `Io`.
    virtual Status Write(const void* data, usize length, u64 offset) = 0;
    /// Writes all `length` bytes at the end of the file. Only files opened with `OpenMode::Append` support it.
    ///
    /// Fails: `Unsupported` (not opened for appending), `Invalid`, `NoSpace`, `Io`.
    virtual Status Append(const void* data, usize length) = 0;
    /// Pushes data the backend buffers to the operating system. A local file has no user-space buffer, so this succeeds.
    virtual Status Flush() = 0;
    /// Pushes the data to the storage device (`fsync`); returns when it is durable.
    ///
    /// Fails: `Unsupported`, `Io`, `NoSpace`.
    virtual Status Sync() = 0;
    /// Sets the size, cutting data off or extending with zeros.
    ///
    /// Fails: `Invalid` (closed or not writable), `Unsupported`, `NoSpace`.
    virtual Status Truncate(u64 size) = 0;
    /// Closes the file. Idempotent: later calls succeed and do nothing.
    ///
    /// Fails: `Io` or `NoSpace` when the system reports a deferred write error at close.
    virtual Status Close() = 0;

    /// Moves the stream position (used by the offset-less `Read` and `Write`).
    ///
    /// Fails: `Unsupported` without `kCapSeekable`, `Invalid` after `Close`.
    Status Seek(u64 position);
    /// Returns the stream position.
    u64 Tell() const { return position_; }
    /// Reads at the stream position and advances it by the bytes read; `Read` rules apply.
    Status Read(void* buffer, usize length, usize* bytes_read);
    /// Writes all bytes at the stream position and advances it; on an `Append`-mode file it appends.
    Status Write(const void* data, usize length);

private:
    u64 position_ = 0;
};

/// Creates the backend object for a URL scheme: `location` is the whole text (`scheme://rest`).
using FileFactory = Status (*)(StringView location, OpenMode mode, u32 flags, UniquePtr<File>* out);

/// Registers a backend for `scheme` (lower case letters, digits, `+`, `-`, `.`; first a letter).
///
/// Param scheme: for example `pak` or `http`; `file` is reserved.
/// Fails: `Invalid` (a bad or reserved scheme, a null factory), `AlreadyExists`, `OutOfMemory` (the registry holds 16).
/// Thread-safety: thread-safe.
Status RegisterFileScheme(StringView scheme, FileFactory factory);
/// Removes a registration; unknown schemes are ignored. Files already opened are unaffected.
void UnregisterFileScheme(StringView scheme);

/// Reads exactly `length` bytes at `offset`, looping over short reads.
///
/// Fails: `EndOfFile` when the file ends first (`buffer` holds what was read), plus the errors of [[io::File::Read(void*,usize,u64,usize*)]].
Status ReadExact(File& file, void* buffer, usize length, u64 offset);
/// Reads from `offset 0` to the end into `out` (replacing its contents), trusting no size: it reads until a read returns zero.
///
/// Param max_bytes: refuse files longer than this.
/// Fails: `OutOfMemory` when the vector cannot grow, `TooLong` when `max_bytes` is exceeded, plus the errors of [[io::File::Read(void*,usize,u64,usize*)]].
Status ReadAll(File& file, Vector<u8>* out, u64 max_bytes = u64{1} << 32);
/// Like the vector overload but allocates the contents from `arena` (the replacement of `os::ReadFileToArena`).
///
/// Param data: receives the arena block; `size` the byte count. A zero-length file gives a valid pointer and size 0.
/// Fails: `OutOfMemory` when the arena is full, `TooLong` above `max_bytes`, plus the errors of [[io::File::Read(void*,usize,u64,usize*)]].
/// Ownership: the block belongs to `arena`.
Status ReadAll(File& file, ArenaAllocator& arena, u8** data, usize* size, u64 max_bytes = u64{1} << 32);

/// Opens `path`, reads it whole into `out`, closes it.
///
/// Fails: the errors of [[io::File::Open]] and [[io::ReadAll(File&,Vector<u8>*,u64)]].
Status ReadFile(const Path& path, Vector<u8>* out, u64 max_bytes = u64{1} << 32);
/// Opens `path`, reads it whole into the arena, closes it (the path-based replacement of `os::ReadFileToArena`).
///
/// Fails: as [[io::ReadFile(const Path&,Vector<u8>*,u64)]].
Status ReadFile(const Path& path, ArenaAllocator& arena, u8** data, usize* size, u64 max_bytes = u64{1} << 32);
/// Reads a whole text file into `out`. The bytes are not validated as UTF-8.
///
/// Fails: as [[io::ReadFile(const Path&,Vector<u8>*,u64)]].
Status ReadTextFile(const Path& path, String<HeapAllocator>* out, u64 max_bytes = u64{1} << 28);
/// Creates or truncates `path` and writes `length` bytes. Not atomic: a failure can leave a partial file; see [[io::WriteFileAtomic]].
///
/// Fails: the errors of [[io::File::Open]] and [[io::File::Write(const void*,usize,u64)]].
Status WriteFile(const Path& path, const void* data, usize length);
/// Replaces the contents of `path` so that a reader sees the old file or the new one, never a mix, and a crash leaves one of them.
///
/// Writes a temporary file in the same directory, flushes and syncs it, then renames it over `path`. The temporary is removed on
/// every failure path, and the directory is synced after the rename so the new entry survives a crash. A symbolic link at `path` is
/// followed (the file it points to is replaced, the link stays). A replaced file keeps its permission bits; a new file gets mode 0600.
///
/// Param path: the target; its directory must exist.
/// Fails: `NotFound` (no directory), `PermissionDenied`, `NoSpace`, `IsADirectory` (the target is a directory), `Io`; the old content is intact.
Status WriteFileAtomic(const Path& path, const void* data, usize length);

namespace detail {
/// Which step of [[io::WriteFileAtomic]] a test makes fail: 0 none, 1 write, 2 flush, 3 sync, 4 rename.
void SetAtomicWriteFailureStep(int step);
} // namespace detail

} // namespace engine_placeholder::io
