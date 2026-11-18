#pragma once

#include "containers/vector.h"
#include "io/error.h"
#include "io/path.h"
#include "mem/heap_allocator.h"
#include "containers/unique_ptr.h"
#include "string/string.h"
#include "types.h"

//! Watching one directory for changes without reading it again and again: [[io::DirectoryWatcher]]. Phase 27 Day 464
//! (hot reload). Linux has a native backend (inotify); every platform has the polling one, which the other native
//! backends (Windows and macOS, Days 473 and 474) replace.
namespace engine_placeholder::io
{

/// What happened to a name in the watched directory.
enum class WatchEventKind : u8
{
    Created,   ///< The name appeared (a file or directory made, or moved in).
    Modified,  ///< The contents of a file were written and closed, or its size or time changed (polling).
    Removed,   ///< The name went away (deleted, or moved out).
    Renamed,   ///< `old_name` became `name` inside the directory (the polling backend reports `Removed` and `Created`).
    Overflow,  ///< The backend lost events: look at the directory again. `name` is empty.
};

/// One change. Names are one component, relative to the watched directory, and never contain a separator.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns its names.
struct WatchEvent
{
    WatchEventKind kind = WatchEventKind::Overflow;            ///< What happened.
    String<HeapAllocator> name{HeapAllocator::Default()};      ///< The name affected (the new name of a rename).
    String<HeapAllocator> old_name{HeapAllocator::Default()};  ///< The name before a `Renamed`; else empty.
};

/// How a watcher is opened.
///
/// - Thread-safety: thread-compatible plain data.
struct WatchOptions
{
    /// Use the polling backend even where a native one exists (tests, network directories).
    bool force_polling = false;
    /// How often the polling backend looks, in milliseconds (1 to 60000).
    u32 poll_interval_ms = 200;
};

/// A watch on one directory (not its subdirectories). Events are queued by the backend and taken with
/// [[io::DirectoryWatcher::Poll]]; within one call, repeated events for one name are merged (a create followed by
/// writes is one `Created`; a create then a remove is nothing), and no event names a path outside the directory.
///
/// An event is a hint, not a record: a consumer reads the file again and compares what it finds, and treats `Overflow`
/// as "everything may have changed".
///
/// - Thread-safety: thread-compatible: one thread calls `Poll` and `Wait` at a time; [[io::DirectoryWatcher::Wake]] may
/// be called from any thread. Destroy the watcher only when no thread is inside `Wait` (wake it, join, then destroy).
/// - Ownership: owns the watch and any descriptor behind it; destroying it ends the watch.
class DirectoryWatcher
{
public:
    virtual ~DirectoryWatcher() = default;
    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

    /// Starts watching `directory`.
    ///
    /// - Param directory: the directory; it must exist.
    /// - Param options: how to watch.
    /// - Param out: receives the watcher; unchanged on failure.
    /// - Returns: the status.
    /// - Fails: `NotFound`, `NotADirectory`, `PermissionDenied`, `Invalid` (an interval out of range). A native backend
    /// that cannot start (watch limit reached, a file system without notifications) falls back to the polling backend
    /// instead of failing; [[io::DirectoryWatcher::IsNative]] tells which one was made.
    /// - Backend: Linux: inotify; Windows: ReadDirectoryChangesW (Day 473, polling until then); macOS: FSEvents (Day
    /// 474, polling until then).
    static Status Open(const Path& directory, const WatchOptions& options, UniquePtr<DirectoryWatcher>* out);

    /// Takes the queued events, appending them to `out` (which is not cleared) in the order they happened.
    ///
    /// - Param out: receives the events; not null.
    /// - Returns: `Success`, or the failure of the backend.
    /// - Fails: `Invalid` (the watched directory is gone and the watch ended: an `Overflow` is queued first), `Io`.
    virtual Status Poll(Vector<WatchEvent>* out) = 0;

    /// Waits until an event is queued, [[io::DirectoryWatcher::Wake]] is called, or the time passes.
    ///
    /// - Param timeout_ms: the longest wait, in milliseconds; 0 only checks.
    /// - Returns: true when events are waiting to be taken, false on a timeout or a wake with nothing queued.
    /// - Fails: never.
    virtual bool Wait(u32 timeout_ms) = 0;

    /// Makes a thread inside `Wait` return at once (it returns false unless events are queued), and makes the next
    /// `Wait` return at once too, once.
    ///
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    virtual void Wake() = 0;

    /// True for a native backend, false for the polling one.
    ///
    /// - Fails: never.
    virtual bool IsNative() const = 0;

protected:
    DirectoryWatcher() = default;
};

/// Makes the platform's native watcher; the polling fallback in [[io::DirectoryWatcher::Open]] covers a failure.
///
/// - Param directory: the directory.
/// - Param out: receives the watcher on success.
/// - Returns: `Success`, `Unsupported` (this platform has no native backend yet), or the failure of the backend.
/// - Backend: Linux: inotify_init1 and inotify_add_watch; Windows: CreateFileW with FILE_FLAG_OVERLAPPED and
/// ReadDirectoryChangesW; macOS: FSEventStreamCreate.
Status OpenNativeWatcher(const Path& directory, UniquePtr<DirectoryWatcher>* out);

/// Makes the polling watcher: it lists the directory every `poll_interval_ms` and compares names, sizes and times.
///
/// - Param directory: the directory.
/// - Param interval_ms: the interval, 1 to 60000.
/// - Param out: receives the watcher.
/// - Returns: `Success`, or the failure of the first listing.
/// - Fails: `NotFound`, `NotADirectory`, `PermissionDenied`, `Invalid`.
Status OpenPollingWatcher(const Path& directory, u32 interval_ms, UniquePtr<DirectoryWatcher>* out);

}  // namespace engine_placeholder::io
