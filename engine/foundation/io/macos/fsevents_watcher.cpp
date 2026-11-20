// Phase 27 Day 474: the macOS directory watcher, on FSEvents (Core Services' C API; no Objective-C). The stream's
// callback runs on a private serial dispatch queue and only queues events; `Poll` and `Wait` take them.
//
// FSEvents reports file-level events for the whole tree below the path, with flags that accumulate on a path (a file
// written once carries `ItemModified` on every later event, a chmod included), so the flags do not say what happened.
// The watcher keeps what it knows of each name (type, size, modification time) and maps an event by what is there now:
// a name that exists is `Created` the first time it is seen and `Modified` when its type, size or time changed; a known
// name that is gone is `Removed`; a change of metadata only is not reported (as on Linux). Renames arrive as the two
// names. Files are looked at through the file layer (io::Lstat, io::ListDirectory), so this file makes no file system
// call of its own; they are looked at outside the lock, so `Poll`, `Wait` and `Wake` never wait for the disk.
#include "containers/array.h"
#include "containers/hash_map.h"
#include "io/detail/watch_queue.h"
#include "io/directory.h"
#include "io/directory_watcher.h"
#include "io/file_system.h"
#include "cstr/cstr.h"
#include "sync/mutex.h"

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

namespace vse::io
{

namespace
{

// How long FSEvents gathers events before calling back, in seconds (the day file's 50 ms).
constexpr CFTimeInterval kLatencySeconds = 0.05;

// Events were dropped (a wrap of the event ids, kFSEventStreamEventFlagEventIdsWrapped, loses nothing).
constexpr FSEventStreamEventFlags kLostFlags = kFSEventStreamEventFlagMustScanSubDirs |
                                               kFSEventStreamEventFlagUserDropped |
                                               kFSEventStreamEventFlagKernelDropped;

// The flags of a change to a file's metadata only. FSEvents keeps a path's flags from event to event, so a file once
// written still carries ItemModified on a later chmod: an event with one of these is not taken as a write.
constexpr FSEventStreamEventFlags kMetadataFlags = kFSEventStreamEventFlagItemInodeMetaMod |
                                                   kFSEventStreamEventFlagItemXattrMod |
                                                   kFSEventStreamEventFlagItemChangeOwner |
                                                   kFSEventStreamEventFlagItemFinderInfoMod;

void DrainQueue(void*) {}

using Name = String<HeapAllocator>;

// What the watcher knows of one name.
struct Known
{
    FileType type = FileType::None;
    u64 size = 0;
    i64 modified = 0;
};

// What is known of each name of the directory.
using KnownNames = HashMap<Name, Known, HeapAllocator>;

Known KnownOf(const FileInfo& info)
{
    Known known;
    known.type = info.type;
    known.size = info.size;
    known.modified = info.modified_unix_ns;
    return known;
}

bool SameKnown(const Known& a, const Known& b)
{
    return a.type == b.type && a.size == b.size && a.modified == b.modified;
}

// What a look at one name found.
enum class Look : u8
{
    Exists,  // `info` holds it
    Gone,    // not found
    Failed,  // the look failed otherwise (permission, I/O, a path too long): nothing can be said
};

// One event of a batch, looked at outside the lock.
struct Pending
{
    enum class Kind : u8
    {
        Name,  // a name directly inside the directory
        Root,  // the directory itself (or a path above it) changed
        Lost,  // FSEvents dropped events for the directory: list it again
    };
    Kind kind = Kind::Name;
    Name name{HeapAllocator::Default()};
    Look look = Look::Failed;
    FileInfo info;
    FSEventStreamEventFlags flags = 0;
};

class FsEventsWatcher final : public DirectoryWatcher
{
public:
    ~FsEventsWatcher() override
    {
        if (stream_ != nullptr)
        {
            if (started_)
            {
                FSEventStreamStop(stream_);
            }
            if (scheduled_)
            {
                FSEventStreamInvalidate(stream_);
            }
            FSEventStreamRelease(stream_);
            stream_ = nullptr;
        }
        if (queue_handle_ != nullptr)
        {
            // A callback already running finishes before the watcher goes: the queue is serial.
            dispatch_sync_f(queue_handle_, nullptr, &DrainQueue);
            dispatch_release(queue_handle_);
            queue_handle_ = nullptr;
        }
    }

    Status Start(const Path& directory)
    {
        if (!IsDirectory(directory))
        {
            return Exists(directory) ? Status::Of(Error::NotADirectory) : Status::Of(Error::NotFound);
        }
        // FSEvents reports resolved paths (/private/var/..., not /var/...): compare against the resolved directory.
        const Status canonical = Canonical(directory, &directory_);
        if (canonical.Failed())
        {
            return canonical;
        }
        const StringView root = directory_.View();
        prefix_ = String<HeapAllocator>(HeapAllocator::Default(), root);
        if (root.Length() == 0 || root[root.Length() - 1] != '/')
        {
            prefix_.Append(StringView("/"));  // "/" itself already ends in one
        }
        Array<char, kMaxPathBytes + 1> native;
        if (!directory_.ToNative(native.Data(), native.Size()))
        {
            return Status::Of(Error::TooLong);
        }
        // The names already there, before the stream starts; listed again after it started (below), so a change in
        // between is reported either way.
        KnownNames before(HeapAllocator::Default());
        const Status first = List(&before);
        if (first.Failed())
        {
            return first;
        }
        {
            LockGuard<Mutex> lock(mutex_);
            known_ = Move(before);
        }
        CFStringRef path = CFStringCreateWithCString(kCFAllocatorDefault, native.Data(), kCFStringEncodingUTF8);
        if (path == nullptr)
        {
            return Status::Of(Error::OutOfMemory);
        }
        Array<const void*, 1> values = {{path}};
        CFArrayRef paths = CFArrayCreate(kCFAllocatorDefault, values.Data(), 1, &kCFTypeArrayCallBacks);
        CFRelease(path);
        if (paths == nullptr)
        {
            return Status::Of(Error::OutOfMemory);
        }
        FSEventStreamContext context = {0, this, nullptr, nullptr, nullptr};
        stream_ = FSEventStreamCreate(kCFAllocatorDefault, &FsEventsWatcher::Callback, &context, paths,
                                      kFSEventStreamEventIdSinceNow, kLatencySeconds,
                                      kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer |
                                          kFSEventStreamCreateFlagWatchRoot);
        CFRelease(paths);
        if (stream_ == nullptr)
        {
            return Status::Of(Error::Io);
        }
        queue_handle_ = dispatch_queue_create("engine.io.directory_watcher", DISPATCH_QUEUE_SERIAL);
        if (queue_handle_ == nullptr)
        {
            return Status::Of(Error::OutOfMemory);
        }
        FSEventStreamSetDispatchQueue(stream_, queue_handle_);
        scheduled_ = true;
        if (!FSEventStreamStart(stream_))
        {
            return Status::Of(Error::Io);
        }
        started_ = true;
        KnownNames after(HeapAllocator::Default());
        const Status second = List(&after);
        if (second.Failed())
        {
            return second;
        }
        {
            LockGuard<Mutex> lock(mutex_);
            Replace(Move(after), true);
        }
        ready_.NotifyAll();
        return Status::Success();
    }

    Status Poll(Vector<WatchEvent>* out) override
    {
        LockGuard<Mutex> lock(mutex_);
        queue_.Take(out);
        return gone_ ? Status::Of(Error::Invalid) : Status::Success();
    }

    bool Wait(u32 timeout_ms) override
    {
        UniqueLock<Mutex> lock(mutex_);
        if (queue_.Empty() && !wake_)
        {
            ready_.WaitForMilliseconds(lock, timeout_ms, [this]() { return !queue_.Empty() || wake_; });
        }
        wake_ = false;
        return !queue_.Empty();
    }

    void Wake() override
    {
        {
            LockGuard<Mutex> lock(mutex_);
            wake_ = true;
        }
        ready_.NotifyAll();
    }

    bool IsNative() const override { return true; }

private:
    static void Callback(ConstFSEventStreamRef, void* info, usize count, void* paths,
                         const FSEventStreamEventFlags flags[], const FSEventStreamEventId[])
    {
        static_cast<FsEventsWatcher*>(info)->Take(count, static_cast<char**>(paths), flags);
    }

    // On the dispatch queue: looks at the batch's names without the lock, then queues what changed.
    void Take(usize count, char** paths, const FSEventStreamEventFlags flags[])
    {
        Vector<Pending> batch;
        bool lost = false;
        bool replaced = false;  // the directory was replaced by another at its path: what is in it now is listed
        for (usize i = 0; i < count; ++i)
        {
            Pending pending;
            if (Classify(StringView(paths[i], CStrLength(paths[i])), flags[i], &pending))
            {
                lost = lost || pending.kind == Pending::Kind::Lost;
                replaced = replaced || (pending.kind == Pending::Kind::Root && pending.look == Look::Exists &&
                                        (pending.flags & kFSEventStreamEventFlagRootChanged) != 0);
                batch.Push(Move(pending));
            }
        }
        if (batch.Size() == 0)
        {
            return;
        }
        KnownNames listed(HeapAllocator::Default());
        const bool relisted = (lost || replaced) && List(&listed).Ok();
        {
            LockGuard<Mutex> lock(mutex_);
            if (!gone_)
            {
                if (lost)
                {
                    queue_.Push(WatchEventKind::Overflow, StringView());
                    if (relisted)
                    {
                        Replace(Move(listed), false);
                    }
                }
                else if (replaced)
                {
                    if (relisted)
                    {
                        Replace(Move(listed), true);  // the names of the old directory go, those of the new one come
                    }
                    else
                    {
                        queue_.Push(WatchEventKind::Overflow, StringView());
                    }
                }
                for (usize i = 0; i < batch.Size() && !gone_; ++i)
                {
                    Apply(batch[i]);
                }
            }
        }
        ready_.NotifyAll();
    }

    // Sorts one event out and looks at its name; false for an event that says nothing about the directory's names.
    bool Classify(StringView path, FSEventStreamEventFlags flags, Pending* out) const
    {
        while (path.Length() > 1 && path[path.Length() - 1] == '/')
        {
            path = path.Substring(0, path.Length() - 1);
        }
        const StringView prefix = prefix_.View();
        const StringView root = directory_.View();
        const bool is_root = path == root;
        const bool above_root = !is_root && (path.Length() == 0 || prefix.StartsWith(path));
        StringView name;
        if (!is_root && !above_root)
        {
            if (!path.StartsWith(prefix) || path.Length() == prefix.Length())
            {
                return false;
            }
            name = path.Substring(prefix.Length());
            if (name.Contains(StringView("/")))
            {
                return false;  // below a subdirectory (the stream is recursive), losses there included
            }
        }
        if ((flags & kLostFlags) != 0)
        {
            out->kind = Pending::Kind::Lost;
            return true;
        }
        out->flags = flags;
        if (is_root || above_root || (flags & kFSEventStreamEventFlagRootChanged) != 0)
        {
            out->kind = Pending::Kind::Root;
            out->look = IsDirectory(directory_) ? Look::Exists : Look::Gone;
            return true;
        }
        out->kind = Pending::Kind::Name;
        out->name = String<HeapAllocator>(HeapAllocator::Default(), name);
        out->look = LookAt(name, &out->info);
        return true;
    }

    Look LookAt(StringView name, FileInfo* info) const
    {
        const Status status = Lstat(directory_.Join(name), info);
        if (status.Ok())
        {
            return Look::Exists;
        }
        return status.Is(Error::NotFound) || status.Is(Error::NotADirectory) ? Look::Gone : Look::Failed;
    }

    // Lists the directory with what each name is (outside the lock).
    Status List(KnownNames* out) const
    {
        Vector<DirectoryItem> items;
        const Status listed = ListDirectory(directory_, false, &items);
        if (listed.Failed())
        {
            return listed;
        }
        for (usize i = 0; i < items.Size(); ++i)
        {
            FileInfo info;
            if (LookAt(items[i].name.View(), &info) == Look::Exists)
            {
                out->Insert(Name(HeapAllocator::Default(), items[i].name.View()), KnownOf(info));
            }
        }
        return Status::Success();
    }

    // Takes a fresh listing as what is known; with `emit`, queues the differences first. The caller holds the mutex.
    void Replace(KnownNames listed, bool emit)
    {
        if (emit)
        {
            listed.ForEach(
                [this](const Name& name, const Known& now)
                {
                    const Known* was = known_.Find(name);
                    if (was == nullptr)
                    {
                        queue_.Push(WatchEventKind::Created, name.View());
                    }
                    else if (!SameKnown(*was, now))
                    {
                        queue_.Push(WatchEventKind::Modified, name.View());
                    }
                });
            known_.ForEach(
                [this, &listed](const Name& name, const Known&)
                {
                    if (listed.Find(name) == nullptr)
                    {
                        queue_.Push(WatchEventKind::Removed, name.View());
                    }
                });
        }
        known_ = Move(listed);
    }

    // The caller holds the mutex.
    void Apply(const Pending& pending)
    {
        if (pending.kind == Pending::Kind::Lost)
        {
            return;  // the Overflow and the new listing are queued by Take
        }
        if (pending.kind == Pending::Kind::Root)
        {
            if (pending.look == Look::Gone)
            {
                gone_ = true;  // the directory was removed or moved away: the watch ends
                queue_.Push(WatchEventKind::Overflow, StringView());
            }
            return;
        }
        if (pending.look == Look::Failed)
        {
            queue_.Push(WatchEventKind::Overflow, StringView());  // cannot tell what happened to the name
            return;
        }
        const StringView name = pending.name.View();
        Known* known = known_.Find(name);
        if (pending.look == Look::Exists)
        {
            const Known now = KnownOf(pending.info);
            if (known == nullptr)
            {
                known_.Insert(Name(HeapAllocator::Default(), name), now);
                queue_.Push(WatchEventKind::Created, name);
            }
            else if (!SameKnown(*known, now))
            {
                *known = now;
                queue_.Push(WatchEventKind::Modified, name);  // written, or replaced (a rename over it)
            }
            else if ((pending.flags & kFSEventStreamEventFlagItemModified) != 0 &&
                     (pending.flags & kMetadataFlags) == 0)
            {
                // Written without a change of size or time (a tool that restores the time, a coarse clock on
                // HFS+ or exFAT): the event says the contents changed and nothing says it was metadata.
                queue_.Push(WatchEventKind::Modified, name);
            }
            // else: only metadata (permissions, extended attributes, owner) changed, or nothing; not reported
        }
        else if (known != nullptr)
        {
            known_.Remove(name);
            queue_.Push(WatchEventKind::Removed, name);
        }
        // else: made and removed before it was looked at; nothing to report
    }

    Path directory_;
    String<HeapAllocator> prefix_{HeapAllocator::Default()};  // the directory with one trailing separator
    FSEventStreamRef stream_ = nullptr;
    dispatch_queue_t queue_handle_ = nullptr;
    bool scheduled_ = false;
    bool started_ = false;
    Mutex mutex_;
    ConditionVariable ready_;
    detail::WatchQueue queue_;
    KnownNames known_{HeapAllocator::Default()};  // what is known of each name
    bool gone_ = false;
    bool wake_ = false;
};

}  // namespace

Status OpenNativeWatcher(const Path& directory, UniquePtr<DirectoryWatcher>* out)
{
    UniquePtr<FsEventsWatcher> watcher = MakeUnique<FsEventsWatcher>();
    if (watcher.Get() == nullptr)
    {
        return Status::Of(Error::OutOfMemory);
    }
    const Status started = watcher->Start(directory);
    if (started.Failed())
    {
        return started;
    }
    *out = UniquePtr<DirectoryWatcher>(watcher.Release());
    return Status::Success();
}

}  // namespace vse::io
