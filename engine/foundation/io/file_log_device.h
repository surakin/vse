#pragma once

#include "containers/unique_ptr.h"
#include "io/error.h"
#include "io/file.h"
#include "io/path.h"
#include "log/log_device.h"
#include "sync/mutex.h"
#include "types.h"

//! A log device that appends lines to a file (Maintenance: log devices).
namespace vse::io
{

/// Options of a [[FileLogDevice]].
struct FileLogOptions
{
    /// Size at which the file is rotated; a line that would pass it starts a new file. 0 never rotates.
    u64 max_bytes = 10u * 1024u * 1024u;
    /// Rotated files kept: `name.1` is the newest, `name.<keep>` the oldest, which the next rotation removes. 0 keeps
    /// none (the file is emptied instead).
    u32 keep = 3;
    /// Lines at this level or more severe reach the file at once; the rest wait in a buffer until it fills or
    /// [[FileLogDevice::Flush]] runs.
    LogVerbosity flush_level = LogVerbosity::Error;
};

/// Appends every line it receives to a file, with a buffer, a flush level and size-based rotation.
///
/// The line is written as formatted (`[Channel] LEVEL: message`), followed by the stack trace when the record has one.
/// A line is never split across a rotation. A write that fails drops the line and counts it ([[DroppedLines]]); the
/// device never logs about itself (that would recurse).
///
/// - Thread-safety: thread-safe; one lock around the buffer and the file.
/// - Ownership: owns the file; register it with [[AddLogDevice]] and remove it before destroying it. The destructor
///   flushes.
class FileLogDevice final : public LogDevice
{
public:
    /// Opens `path` for appending (created if missing) and makes the device.
    ///
    /// - Param path: the log file; its directory must exist.
    /// - Param options: rotation and flushing.
    /// - Param out: receives the device on success; unchanged on failure.
    /// - Returns: the file layer's status.
    /// - Fails: `NotFound` (no directory), `PermissionDenied`, `IsADirectory`, or any error of [[File::Open]].
    static Status Open(const Path& path, const FileLogOptions& options, UniquePtr<FileLogDevice>* out);

    /// Flushes the buffered lines and closes the file.
    ~FileLogDevice() override;

    /// Appends the line (and trace) to the file, rotating first if it would pass the size limit.
    void Write(const LogRecord& record) override;

    /// Writes the buffered lines to the file.
    void Flush() override;

    /// Returns how many lines were dropped because the file could not be written or reopened.
    /// - Thread-safety: thread-safe.
    u64 DroppedLines();

    /// Returns how many times the file was rotated.
    /// - Thread-safety: thread-safe.
    u32 Rotations();

    /// Use [[Open]]: this takes an already opened file whose size is `size`.
    FileLogDevice(const Path& path, const FileLogOptions& options, FilePtr file, u64 size);

private:
    bool FlushLocked();
    void RotateLocked();
    Path RotatedName(u32 index) const;

    static constexpr usize kBufferBytes = 16 * 1024;

    Mutex mutex_;
    Path path_;
    FileLogOptions options_;
    FilePtr file_;
    u64 size_;  // bytes in the file, buffered ones included
    u64 dropped_ = 0;
    u32 rotations_ = 0;
    usize buffered_ = 0;
    char buffer_[kBufferBytes];
};

}  // namespace vse::io
