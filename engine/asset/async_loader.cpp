#include "asset/async_loader.h"

#include "asset/blob.h"
#include "asset/level.h"
#include "asset/resource_type.h"
#include "jobs/job.h"
#include "lang/status_try.h"
#include "log/assert.h"
#include "os/os.h"
#include "string/string.h"
#include "sync/thread.h"

namespace engine_placeholder::asset
{

namespace
{

enum RequestState : u32
{
    kQueued = 0,
    kWaitingTable = 1,
    kReading = 2,
    kReady = 3,
    kDone = 4,
};

}  // namespace

struct LoadRequest
{
    LoadRequest() : path(HeapAllocator::Default()) {}

    ResourceId id;
    ResourceType type;
    u32 key = 0;
    LoadPriority priority = LoadPriority::Normal;
    const storage::CancelToken* cancel = nullptr;
    u64 sequence = 0;
    String<HeapAllocator> path;  // for a request by path, until it is resolved
    bool by_path = false;
    Atomic<bool> cancelled{false};  // every ticket let go, or the loader is shutting down
    Atomic<u32> waiters{1};         // tickets that still want the result
    Atomic<u32> state{kQueued};
    Atomic<bool> done{false};
    Atomic<u32> read{0};
    Atomic<u32> total{0};
    LoadRequestPtr leader;       // a request by path that resolved to a load already in progress follows it
    PrefetchedBlobs prefetched;  // written by the reading job, then read by the build, never both at once
    HandleBase handle;
    Status result = Status::Make(ErrorCode::NotReady, "the load has not ended");

    bool IsCancelled() const { return cancelled.Load() || (cancel != nullptr && cancel->IsCancelled()); }
};

// ---------------------------------------------------------------------------------------------------------------------
// LoadTicket

LoadTicket::LoadTicket(LoadTicket&& other) noexcept : request_(static_cast<LoadRequestPtr&&>(other.request_))
{
    detached_.Store(other.detached_.Exchange(true));
}

LoadTicket& LoadTicket::operator=(LoadTicket&& other) noexcept
{
    if (this != &other)
    {
        Detach();
        request_ = static_cast<LoadRequestPtr&&>(other.request_);
        detached_.Store(other.detached_.Exchange(true));
    }
    return *this;
}

LoadTicket::~LoadTicket()
{
    Detach();
}

void LoadTicket::Detach()
{
    if (request_ && !detached_.Exchange(true))
    {
        if (request_->waiters.FetchSub(1) == 1)
        {
            request_->cancelled.Store(true);
        }
    }
}

bool LoadTicket::IsDone() const
{
    return request_ && request_->done.Load(MemoryOrder::Acquire);
}

Status LoadTicket::Result() const
{
    if (!request_)
    {
        return Status::Make(ErrorCode::Invalid, "the ticket is empty");
    }
    if (!request_->done.Load(MemoryOrder::Acquire))
    {
        return Status::Make(ErrorCode::NotReady, "the load has not ended");
    }
    return request_->result;
}

Status LoadTicket::Finish(HandleBase* out) const
{
    out->Reset();
    const Status result = Result();
    if (result.Ok())
    {
        *out = request_->handle;
    }
    return result;
}

void LoadTicket::Cancel()
{
    Detach();
}

void LoadTicket::Progress(u32* read, u32* total) const
{
    if (read != nullptr)
    {
        *read = request_ ? request_->read.Load() : 0;
    }
    if (total != nullptr)
    {
        *total = request_ ? request_->total.Load() : 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// AsyncLoader

namespace
{

jobs::Job ReadJob(AsyncLoader* loader, LoadRequestPtr request)
{
    loader->RunRead(*request);
    co_return;
}

bool Before(const LoadRequestPtr& a, const LoadRequestPtr& b)
{
    if (a->priority != b->priority)
    {
        return static_cast<u8>(a->priority) < static_cast<u8>(b->priority);
    }
    return a->sequence < b->sequence;
}

usize BestOf(const Vector<LoadRequestPtr>& list)
{
    usize best = 0;
    for (usize i = 1; i < list.Size(); ++i)
    {
        if (Before(list[i], list[best]))
        {
            best = i;
        }
    }
    return best;
}

LoadRequestPtr Take(Vector<LoadRequestPtr>* list, usize at)
{
    LoadRequestPtr taken = list->Data()[at];
    (*list)[at] = (*list)[list->Size() - 1];
    list->Pop();
    return taken;
}

void WaitForJobs(jobs::Scheduler& scheduler, jobs::Counter& counter)
{
    if (counter.IsZero())
    {
        return;
    }
    if (scheduler.ParticipantCount() == 1)
    {
        scheduler.HelpUntil(counter);
        return;
    }
    while (!counter.IsZero())
    {
        ThisThread::Yield();
    }
}

}  // namespace

AsyncLoader::AsyncLoader(ResourceManager& manager, jobs::Scheduler& scheduler, const AsyncLoaderOptions& options)
    : manager_(&manager), scheduler_(&scheduler), options_(options), outstanding_(scheduler)
{
    if (options_.max_inflight == 0)
    {
        options_.max_inflight = manager.Config().max_inflight;
    }
    if (options_.max_queued == 0)
    {
        options_.max_queued = manager.Config().max_queued;
    }
}

AsyncLoader::~AsyncLoader()
{
    {
        LockGuard<Mutex> lock(mutex_);
        for (usize i = 0; i < active_.Size(); ++i)
        {
            active_[i]->cancelled.Store(true);
        }
    }
    WaitForJobs(*scheduler_, outstanding_);
    LockGuard<Mutex> lock(mutex_);
    while (active_.Size() != 0)
    {
        const LoadRequestPtr request = active_[active_.Size() - 1];
        CompleteLocked(request, Status::Make(ErrorCode::Cancelled, "the loader was destroyed"));
    }
}

u64 AsyncLoader::Now() const
{
    return options_.clock != nullptr ? options_.clock(options_.clock_user) : os::MonotonicTimeNanos();
}

void AsyncLoader::CompleteLocked(const LoadRequestPtr& request, const Status& status)
{
    for (usize i = 0; i < active_.Size(); ++i)
    {
        if (active_[i].Get() == request.Get())
        {
            active_[i] = active_[active_.Size() - 1];
            active_.Pop();
            break;
        }
    }
    request->prefetched.Clear();
    request->result = status;
    if (status.Ok())
    {
        stats_.completed += 1;
    }
    else if (status.Is(ErrorCode::Cancelled))
    {
        stats_.cancelled += 1;
    }
    else
    {
        stats_.failed += 1;
    }
    request->state.Store(kDone);
    request->done.Store(true, MemoryOrder::Release);
}

bool AsyncLoader::FullLocked() const
{
    return queued_.Size() + waiting_.Size() >= options_.max_queued;
}

void AsyncLoader::DropCancelledLocked()
{
    // Cancelled requests that wait do not count against the limit.
    for (int pass = 0; pass < 2; ++pass)
    {
        Vector<LoadRequestPtr>& list = pass == 0 ? queued_ : waiting_;
        for (usize i = 0; i < list.Size();)
        {
            if (list[i]->IsCancelled())
            {
                LoadRequestPtr request = Take(&list, i);
                CompleteLocked(request, Status::Make(ErrorCode::Cancelled, "the load was cancelled"));
            }
            else
            {
                ++i;
            }
        }
    }
}

LoadTicket AsyncLoader::Submit(const LoadRequestPtr& request)
{
    request->sequence = next_sequence_++;
    LoadTicket ticket(request);
    // Loaded already: a cache hit is answered at once.
    if (manager_->IsLoaded(request->id))
    {
        const Status status = manager_->LoadPrefetchedRaw(request->id, request->type, request->key, request->prefetched,
                                                          &request->handle, nullptr);
        LockGuard<Mutex> lock(mutex_);
        active_.Push(request);
        CompleteLocked(request, status);
        return ticket;
    }
    LockGuard<Mutex> lock(mutex_);
    DropCancelledLocked();
    const LoadRequestPtr other = FindActiveLocked(request->id, request->key, request->cancel, request.Get());
    if (other)
    {
        // One load for one resource: the new ticket waits on the one in progress.
        if (static_cast<u8>(request->priority) < static_cast<u8>(other->priority))
        {
            other->priority = request->priority;
        }
        stats_.shared += 1;
        ticket.request_ = other;
        return ticket;
    }
    if (reading_ >= options_.max_inflight && FullLocked())
    {
        active_.Push(request);
        stats_.rejected += 1;
        CompleteLocked(request, Status::Make(ErrorCode::LimitExceeded, "assets.max_queued loads are waiting"));
        return ticket;
    }
    active_.Push(request);
    queued_.Push(request);
    PumpLocked();
    return ticket;
}

LoadRequestPtr AsyncLoader::FindActiveLocked(ResourceId id, u32 key, const storage::CancelToken* cancel,
                                             const LoadRequest* except)
{
    for (usize i = 0; i < active_.Size(); ++i)
    {
        LoadRequest& other = *active_[i];
        if (&other == except || other.by_path || other.leader || other.id != id || other.key != key ||
            other.cancel != cancel)
        {
            continue;  // a request with another cancel token is another load: its token must not cancel this one
        }
        // Join only a load somebody still waits for: the count never goes from 0 back up.
        u32 waiting = other.waiters.Load();
        while (waiting != 0 && !other.waiters.CompareExchangeWeak(waiting, waiting + 1))
        {
        }
        if (waiting != 0 && !other.IsCancelled())
        {
            return active_[i];
        }
        if (waiting != 0)
        {
            other.waiters.FetchSub(1);
        }
    }
    return LoadRequestPtr();
}

LoadTicket AsyncLoader::LoadAsyncRaw(ResourceId id, ResourceType type, u32 key, LoadPriority priority,
                                     const storage::CancelToken* cancel)
{
    LoadRequestPtr request = MakeShared<LoadRequest>();
    request->id = id;
    request->type = type;
    request->key = key;
    request->priority = priority;
    request->cancel = cancel;
    return Submit(request);
}

LoadTicket AsyncLoader::LoadPathAsyncRaw(StringView path, ResourceType type, u32 key, LoadPriority priority,
                                         const storage::CancelToken* cancel)
{
    ResourceId id;
    const Status resolved = manager_->Resolve(path, &id);
    if (resolved.Ok())
    {
        return LoadAsyncRaw(id, type, key, priority, cancel);
    }
    LoadRequestPtr request = MakeShared<LoadRequest>();
    request->type = type;
    request->key = key;
    request->priority = priority;
    request->cancel = cancel;
    request->sequence = next_sequence_++;
    LoadTicket ticket(request);
    LockGuard<Mutex> lock(mutex_);
    active_.Push(request);
    if (resolved.Is(ErrorCode::NotReady))
    {
        DropCancelledLocked();
        if (FullLocked())
        {
            stats_.rejected += 1;
            CompleteLocked(request, Status::Make(ErrorCode::LimitExceeded, "assets.max_queued loads are waiting"));
            return ticket;
        }
        request->by_path = true;
        request->path = String<HeapAllocator>(HeapAllocator::Default(), path);
        request->state.Store(kWaitingTable);
        waiting_.Push(request);
        return ticket;
    }
    CompleteLocked(request, resolved);
    return ticket;
}

LoadTicket AsyncLoader::LoadLevelAsync(ResourceId id, LoadPriority priority, const storage::CancelToken* cancel)
{
    const u32 key = manager_->LoaderKeyOf(types::kLevel);
    if (key == 0)
    {
        LoadRequestPtr request = MakeShared<LoadRequest>();
        request->id = id;
        LoadTicket ticket(request);
        LockGuard<Mutex> lock(mutex_);
        active_.Push(request);
        CompleteLocked(request, Status::Make(ErrorCode::TypeMismatch, "no loader is registered for levels"));
        return ticket;
    }
    return LoadAsyncRaw(id, types::kLevel, key, priority, cancel);
}

void AsyncLoader::StartLocked(const LoadRequestPtr& request)
{
    reading_ += 1;
    request->state.Store(kReading);
    jobs::Spawn(outstanding_, ReadJob(this, request));
}

void AsyncLoader::PumpLocked()
{
    // Requests by path whose table arrived.
    for (usize i = 0; i < waiting_.Size();)
    {
        LoadRequestPtr request = waiting_[i];
        if (request->IsCancelled())
        {
            Take(&waiting_, i);
            CompleteLocked(request, Status::Make(ErrorCode::Cancelled, "the load was cancelled"));
            continue;
        }
        if (request->leader)
        {
            if (request->leader->done.Load(MemoryOrder::Acquire))
            {
                Take(&waiting_, i);
                request->handle = request->leader->handle;
                const Status status = request->leader->result;
                request->leader = LoadRequestPtr();
                CompleteLocked(request, status);
            }
            else
            {
                ++i;
            }
            continue;
        }
        ResourceId id;
        const Status resolved = manager_->Resolve(request->path.View(), &id);
        if (resolved.Is(ErrorCode::NotReady))
        {
            ++i;
            continue;
        }
        Take(&waiting_, i);
        if (resolved.Failed())
        {
            CompleteLocked(request, resolved);
            continue;
        }
        request->id = id;
        request->by_path = false;
        if (manager_->IsLoaded(id))
        {
            const Status status = manager_->LoadPrefetchedRaw(id, request->type, request->key, request->prefetched,
                                                              &request->handle, nullptr);
            CompleteLocked(request, status);
            continue;
        }
        const LoadRequestPtr leader = FindActiveLocked(id, request->key, nullptr, request.Get());
        if (leader)
        {
            // The resource is being loaded by another request: this one ends with it.
            leader->waiters.FetchSub(1);  // a follower does not keep the leader alive: it takes the result if it ends
            request->leader = leader;
            request->state.Store(kWaitingTable);
            waiting_.Push(request);
            continue;
        }
        request->state.Store(kQueued);
        queued_.Push(request);
    }
    // The queue, most urgent first, while there are reading slots.
    while (reading_ < options_.max_inflight && queued_.Size() != 0)
    {
        LoadRequestPtr request = Take(&queued_, BestOf(queued_));
        if (request->IsCancelled())
        {
            CompleteLocked(request, Status::Make(ErrorCode::Cancelled, "the load was cancelled"));
            continue;
        }
        StartLocked(request);
    }
}

void AsyncLoader::RunRead(LoadRequest& request)
{
    // Reads the blob and the blobs of its closure that are not loaded. A failure to read one is not the loader's to
    // report: the build reads it again and says why it failed (with the chain, or with a fallback).
    const TypeRegistry& types = manager_->Types();
    Vector<ResourceId> pending;
    HashMap<u64, u8, HeapAllocator> seen(HeapAllocator::Default());  // the ids of `pending`
    usize head = 0;                                                  // pending[head...] is what is left to read
    pending.Push(request.id);
    seen.Insert(request.id.Value(), 1);
    request.total.Store(1);
    while (head < pending.Size() && !request.IsCancelled())
    {
        const ResourceId id = pending[head++];
        BlobBufferPtr buffer;
        if (!manager_->ReadBlob(id, &buffer).Ok())
        {
            request.read.FetchAdd(1);
            continue;
        }
        request.prefetched.Add(id, buffer);
        request.read.FetchAdd(1);
        BlobReader reader;
        if (!reader.Open(buffer->Bytes(), types).Ok())
        {
            continue;
        }
        // The levels a level includes are needed before what it merely references.
        const bool level = reader.Type() == types::kLevel;
        for (int pass = 0; pass < (level ? 2 : 1); ++pass)
        {
            for (usize d = 0; d < reader.DependencyCount(); ++d)
            {
                const Dependency dep = reader.DependencyAt(d);
                const bool included = (dep.role & kLevelRoleInclude) != 0;
                if (level && (pass == 0) != included)
                {
                    continue;
                }
                if (seen.Find(dep.id.Value()) == nullptr && !manager_->MayBeLoaded(dep.id))
                {
                    seen.Insert(dep.id.Value(), 1);
                    pending.Push(dep.id);
                    request.total.FetchAdd(1);
                }
            }
        }
    }
    LockGuard<Mutex> lock(mutex_);
    for (usize i = 0; i < active_.Size(); ++i)
    {
        if (active_[i].Get() == &request)
        {
            reading_ -= 1;
            request.state.Store(kReady);
            ready_.Push(active_[i]);
            return;
        }
    }
    reading_ -= 1;
}

LoadUpdate AsyncLoader::Update(u64 max_microseconds, u64 max_bytes)
{
    LoadUpdate update;
    const u64 started = Now();
    {
        LockGuard<Mutex> lock(mutex_);
        PumpLocked();
    }
    bool built = false;
    u64 spent = 0;
    for (;;)
    {
        LoadRequestPtr request;
        {
            LockGuard<Mutex> lock(mutex_);
            if (ready_.Size() == 0)
            {
                break;
            }
            if (built && ((max_microseconds != 0 && Now() - started >= max_microseconds * 1000) ||
                          (max_bytes != 0 && spent >= max_bytes)))
            {
                break;
            }
            request = Take(&ready_, BestOf(ready_));
            if (request->IsCancelled())
            {
                CompleteLocked(request, Status::Make(ErrorCode::Cancelled, "the load was cancelled"));
                update.completed += 1;
                continue;
            }
        }
        // The build runs the loaders, GPU uploads among them: not under this lock.
        const u64 bytes = request->prefetched.Bytes();
        const Status status = manager_->LoadPrefetchedRaw(request->id, request->type, request->key, request->prefetched,
                                                          &request->handle, nullptr);
        built = true;
        spent += bytes;
        update.bytes += bytes;
        update.completed += 1;
        LockGuard<Mutex> lock(mutex_);
        CompleteLocked(request, status);
    }
    LockGuard<Mutex> lock(mutex_);
    PumpLocked();
    update.backlog = static_cast<u32>(ready_.Size());
    stats_.updates += 1;
    return update;
}

Status AsyncLoader::Wait(const LoadTicket& ticket)
{
    if (!ticket.Valid())
    {
        return Status::Make(ErrorCode::Invalid, "the ticket is empty");
    }
    while (!ticket.IsDone())
    {
        Update(0, 0);
        if (ticket.IsDone())
        {
            break;
        }
        if (scheduler_->ParticipantCount() == 1 && !outstanding_.IsZero())
        {
            scheduler_->HelpUntil(outstanding_);  // nobody else runs the reading jobs: this thread does
        }
        else
        {
            ThisThread::Yield();
        }
    }
    return ticket.Result();
}

AsyncLoadStats AsyncLoader::Stats() const
{
    LockGuard<Mutex> lock(mutex_);
    AsyncLoadStats copy = stats_;
    copy.queued = static_cast<u32>(queued_.Size());
    copy.waiting_table = static_cast<u32>(waiting_.Size());
    copy.reading = reading_;
    copy.ready = static_cast<u32>(ready_.Size());
    return copy;
}

}  // namespace engine_placeholder::asset
