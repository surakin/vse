// Phase 27 Day 464: the polling directory watcher, the same on every platform.
#include "io/detail/watch_queue.h"
#include "io/directory.h"
#include "io/directory_watcher.h"
#include "os/os.h"
#include "sync/mutex.h"

namespace engine_placeholder::io
{

namespace
{

struct Seen
{
    String<HeapAllocator> name{HeapAllocator::Default()};
    FileType type = FileType::None;
    u64 size = 0;
    i64 modified = 0;
};

class PollingWatcher final : public DirectoryWatcher
{
public:
    PollingWatcher(const Path& directory, u32 interval_ms) : directory_(directory), interval_ms_(interval_ms) {}

    Status Start() { return Scan(false); }

    Status Poll(Vector<WatchEvent>* out) override
    {
        LockGuard<Mutex> lock(mutex_);
        const Status status = Scan(true);
        queue_.Take(out);
        return status;
    }

    bool Wait(u32 timeout_ms) override
    {
        const u64 start = os::MonotonicTimeNanos();
        UniqueLock<Mutex> lock(mutex_);
        for (;;)
        {
            if (!queue_.Empty())
            {
                return true;
            }
            if (wake_)
            {
                wake_ = false;
                return false;
            }
            const u64 elapsed_ms = (os::MonotonicTimeNanos() - start) / 1000000ull;
            if (elapsed_ms >= timeout_ms)
            {
                return false;
            }
            const u64 left = timeout_ms - elapsed_ms;
            wake_cv_.WaitForMilliseconds(lock, left < interval_ms_ ? left : interval_ms_);
            if (wake_)
            {
                wake_ = false;
                return !queue_.Empty();
            }
            Scan(true);  // a failed scan queues its `Overflow`
        }
    }

    void Wake() override
    {
        {
            LockGuard<Mutex> lock(mutex_);
            wake_ = true;
        }
        wake_cv_.NotifyAll();
    }

    bool IsNative() const override { return false; }

private:
    // Lists the directory and queues what differs from the last scan (nothing when `emit` is false: the first scan).
    Status Scan(bool emit)
    {
        Vector<DirectoryItem> items;
        const Status listed = ListDirectory(directory_, true, &items);
        if (listed.Failed())
        {
            if (emit && !gone_)
            {
                gone_ = true;
                queue_.Push(WatchEventKind::Overflow, StringView());
            }
            return emit ? Status::Of(Error::Invalid) : listed;
        }
        Vector<Seen> now;
        for (usize i = 0; i < items.Size(); ++i)
        {
            Seen s;
            s.name = String<HeapAllocator>(HeapAllocator::Default(), items[i].name.View());
            s.type = items[i].type;
            FileInfo info;
            Path full;
            if (items[i].type == FileType::Regular && directory_.TryJoin(items[i].name, &full) &&
                Lstat(full, &info).Ok())
            {
                s.size = info.size;
                s.modified = info.modified_unix_ns;
            }
            now.Push(Move(s));
        }
        if (emit)
        {
            usize a = 0;
            usize b = 0;
            while (a < seen_.Size() || b < now.Size())
            {
                const int order = a == seen_.Size() ? 1
                                  : b == now.Size() ? -1
                                                    : Compare(seen_[a].name.View(), now[b].name.View());
                if (order < 0)
                {
                    queue_.Push(WatchEventKind::Removed, seen_[a].name.View());
                    ++a;
                }
                else if (order > 0)
                {
                    queue_.Push(WatchEventKind::Created, now[b].name.View());
                    ++b;
                }
                else
                {
                    if (seen_[a].type != now[b].type || seen_[a].size != now[b].size ||
                        seen_[a].modified != now[b].modified)
                    {
                        queue_.Push(WatchEventKind::Modified, now[b].name.View());
                    }
                    ++a;
                    ++b;
                }
            }
        }
        seen_ = Move(now);
        return Status::Success();
    }

    static int Compare(StringView x, StringView y)
    {
        const usize n = x.Length() < y.Length() ? x.Length() : y.Length();
        for (usize i = 0; i < n; ++i)
        {
            const u8 cx = static_cast<u8>(x[i]);
            const u8 cy = static_cast<u8>(y[i]);
            if (cx != cy)
            {
                return cx < cy ? -1 : 1;
            }
        }
        return x.Length() == y.Length() ? 0 : (x.Length() < y.Length() ? -1 : 1);
    }

    Path directory_;
    u32 interval_ms_;
    Mutex mutex_;
    ConditionVariable wake_cv_;
    bool wake_ = false;
    bool gone_ = false;
    detail::WatchQueue queue_;
    Vector<Seen> seen_;
};

}  // namespace

Status OpenPollingWatcher(const Path& directory, u32 interval_ms, UniquePtr<DirectoryWatcher>* out)
{
    if (interval_ms < 1 || interval_ms > 60000)
    {
        return Status::Of(Error::Invalid);
    }
    UniquePtr<PollingWatcher> watcher = MakeUnique<PollingWatcher>(directory, interval_ms);
    const Status started = watcher->Start();
    if (started.Failed())
    {
        return started;
    }
    *out = UniquePtr<DirectoryWatcher>(watcher.Release());
    return Status::Success();
}

}  // namespace engine_placeholder::io
