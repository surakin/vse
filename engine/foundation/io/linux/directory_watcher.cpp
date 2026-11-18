// Phase 27 Day 464: the Linux directory watcher, on inotify. One of the places allowed to call the operating system's
// file API directly (tools/file_access_check/allowlist.txt).
#include "io/detail/watch_queue.h"
#include "io/directory_watcher.h"
#include "os/last_error.h"
#include "os/os.h"
#include "sync/mutex.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace engine_placeholder::io
{

namespace
{

class InotifyWatcher final : public DirectoryWatcher
{
public:
    ~InotifyWatcher() override
    {
        if (inotify_ >= 0)
        {
            ::close(inotify_);
        }
        if (wake_ >= 0)
        {
            ::close(wake_);
        }
    }

    Status Start(const Path& directory)
    {
        char native[kMaxPathBytes + 1];
        if (!directory.ToNative(native, sizeof(native)))
        {
            return Status::Of(Error::TooLong);
        }
        inotify_ = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        if (inotify_ < 0)
        {
            return Status::FromOs(os::LastError());
        }
        wake_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (wake_ < 0)
        {
            return Status::FromOs(os::LastError());
        }
        const int watch = ::inotify_add_watch(inotify_, native,
                                              IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO |
                                                  IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR);
        if (watch < 0)
        {
            return Status::FromOs(os::LastError());
        }
        return Status::Success();
    }

    Status Poll(Vector<WatchEvent>* out) override
    {
        LockGuard<Mutex> lock(mutex_);
        Drain();
        queue_.Take(out);
        return gone_reported_ ? Status::Of(Error::Invalid) : Status::Success();
    }

    bool Wait(u32 timeout_ms) override
    {
        {
            LockGuard<Mutex> lock(mutex_);
            Drain();
            if (!queue_.Empty())
            {
                return true;
            }
        }
        struct pollfd fds[2];
        fds[0].fd = inotify_;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = wake_;
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        ::poll(fds, 2, static_cast<int>(timeout_ms > 0x7fffffffu ? 0x7fffffffu : timeout_ms));
        if ((fds[1].revents & POLLIN) != 0)
        {
            u64 value = 0;
            [[maybe_unused]] const isize got = ::read(wake_, &value, sizeof(value));
        }
        LockGuard<Mutex> lock(mutex_);
        Drain();
        return !queue_.Empty();
    }

    void Wake() override
    {
        const u64 one = 1;
        [[maybe_unused]] const isize put = ::write(wake_, &one, sizeof(one));
    }

    bool IsNative() const override { return true; }

private:
    // Reads every event the kernel has queued (the descriptor is non-blocking). The caller holds the mutex.
    void Drain()
    {
        alignas(struct inotify_event) char buffer[16384];
        String<HeapAllocator> from(HeapAllocator::Default());
        u32 from_cookie = 0;
        for (;;)
        {
            const isize got = ::read(inotify_, buffer, sizeof(buffer));
            if (got <= 0)
            {
                break;
            }
            usize at = 0;
            while (at + sizeof(struct inotify_event) <= static_cast<usize>(got))
            {
                const struct inotify_event* e = reinterpret_cast<const struct inotify_event*>(buffer + at);
                const usize total = sizeof(struct inotify_event) + e->len;
                if (total > static_cast<usize>(got) - at)
                {
                    break;
                }
                const char* text = e->len != 0 ? e->name : "";
                usize length = 0;
                while (length < e->len && text[length] != 0)
                {
                    ++length;
                }
                const StringView name(text, length);
                if (from.Length() != 0 && !((e->mask & IN_MOVED_TO) != 0 && e->cookie == from_cookie))
                {
                    queue_.Push(WatchEventKind::Removed, from.View());  // a move out of the directory
                    from = String<HeapAllocator>(HeapAllocator::Default());
                }
                if ((e->mask & IN_Q_OVERFLOW) != 0)
                {
                    queue_.Push(WatchEventKind::Overflow, StringView());
                }
                else if ((e->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) != 0)
                {
                    if (!gone_reported_)
                    {
                        gone_reported_ = true;
                        queue_.Push(WatchEventKind::Overflow, StringView());
                    }
                }
                else if (name.Length() == 0)
                {
                    // an event without a name has nothing to report
                }
                else if ((e->mask & IN_MOVED_FROM) != 0)
                {
                    from = String<HeapAllocator>(HeapAllocator::Default(), name);
                    from_cookie = e->cookie;
                }
                else if ((e->mask & IN_MOVED_TO) != 0)
                {
                    if (from.Length() != 0 && e->cookie == from_cookie)
                    {
                        queue_.Push(WatchEventKind::Renamed, name, from.View());
                        from = String<HeapAllocator>(HeapAllocator::Default());
                    }
                    else
                    {
                        queue_.Push(WatchEventKind::Created, name);  // a move in from elsewhere
                    }
                }
                else if ((e->mask & IN_CREATE) != 0)
                {
                    queue_.Push(WatchEventKind::Created, name);
                }
                else if ((e->mask & IN_CLOSE_WRITE) != 0)
                {
                    queue_.Push(WatchEventKind::Modified, name);
                }
                else if ((e->mask & IN_DELETE) != 0)
                {
                    queue_.Push(WatchEventKind::Removed, name);
                }
                at += total;
            }
        }
        if (from.Length() != 0)
        {
            queue_.Push(WatchEventKind::Removed, from.View());
        }
    }

    int inotify_ = -1;
    int wake_ = -1;
    Mutex mutex_;
    detail::WatchQueue queue_;
    bool gone_reported_ = false;
};

}  // namespace

Status OpenNativeWatcher(const Path& directory, UniquePtr<DirectoryWatcher>* out)
{
    UniquePtr<InotifyWatcher> watcher = MakeUnique<InotifyWatcher>();
    const Status started = watcher->Start(directory);
    if (started.Failed())
    {
        return started;
    }
    *out = UniquePtr<DirectoryWatcher>(watcher.Release());
    return Status::Success();
}

}  // namespace engine_placeholder::io
