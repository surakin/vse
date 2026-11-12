#include "net/reactor.h"

#include "containers/array.h"
#include "log/assert.h"

namespace engine_placeholder::net
{

namespace
{

constexpr usize kNoHeapIndex = ~usize{0};
constexpr u64 kMaxNanos = ~u64{0} >> 1;  // the largest duration and time the reactor handles; sums saturate here

u64 SaturatingAdd(u64 a, u64 b)
{
    return a > kMaxNanos - b ? kMaxNanos : a + b;
}

}  // namespace

void CancelToken::Cancel()
{
    cancelled_.Store(true, MemoryOrder::SeqCst);
    // A wait that registers after this load sees the flag (it stores the reactor before it checks the flag).
    Reactor* reactor = reactor_.Load(MemoryOrder::SeqCst);
    if (reactor != nullptr)
    {
        reactor->CancelWaitsOf(this);
    }
}

WaitAwaitable::WaitAwaitable(Reactor* reactor, WaitKind kind, i64 socket, i64 timeout_nanos, CancelToken* token)
    : timeout_nanos_(timeout_nanos)
{
    wait_.reactor = reactor;
    wait_.kind = kind;
    wait_.socket = socket;
    wait_.token = token;
}

bool WaitAwaitable::await_suspend(CoroutineHandle<> handle) noexcept
{
    if (invalid_)
    {
        wait_.result = Status::Of(ErrorCode::Invalid);
        return false;
    }
    wait_.handle = handle;
    if (timeout_nanos_ >= 0)
    {
        wait_.has_deadline = true;
        wait_.deadline = SaturatingAdd(wait_.reactor->GetClock().NowNanos(), static_cast<u64>(timeout_nanos_));
    }
    // Nothing of this object may be touched after Begin returns true: the coroutine can already be running elsewhere.
    return wait_.reactor->Begin(&wait_);
}

Reactor::Reactor(jobs::Scheduler& scheduler, const Clock* clock)
    : scheduler_(&scheduler), clock_(clock != nullptr ? clock : &real_clock_), fds_(HeapAllocator::Default())
{
    running_.Store(poller_.IsValid(), MemoryOrder::Release);
    if (IsRunning())
    {
        thread_ = Thread([this] { Loop(); });
        thread_id_ = thread_.Id();
    }
}

Reactor::~Reactor()
{
    Shutdown();
}

WaitAwaitable Reactor::WaitReadable(const Socket& socket, WaitOptions options)
{
    WaitAwaitable awaitable(this, WaitKind::Readable, socket.NativeHandle(), options.timeout_nanos, options.token);
    awaitable.invalid_ = !socket.IsOpen();
    return awaitable;
}

WaitAwaitable Reactor::WaitWritable(const Socket& socket, WaitOptions options)
{
    WaitAwaitable awaitable(this, WaitKind::Writable, socket.NativeHandle(), options.timeout_nanos, options.token);
    awaitable.invalid_ = !socket.IsOpen();
    return awaitable;
}

WaitAwaitable Reactor::Sleep(u64 nanos, WaitOptions options)
{
    // A duration of 2^63 ns or more is clamped (it would read as a negative timeout, which means none).
    return WaitAwaitable(this, WaitKind::Sleep, -1, static_cast<i64>(nanos > kMaxNanos ? kMaxNanos : nanos),
                         options.token);
}

u32 Reactor::InterestOf(const FdRecord& record)
{
    return (record.reader != nullptr ? kPollRead : 0u) | (record.writer != nullptr ? kPollWrite : 0u);
}

void Reactor::LinkAll(Wait* wait)
{
    wait->prev = nullptr;
    wait->next = all_;
    if (all_ != nullptr)
    {
        all_->prev = wait;
    }
    all_ = wait;
}

void Reactor::UnlinkAll(Wait* wait)
{
    if (wait->prev != nullptr)
    {
        wait->prev->next = wait->next;
    }
    else
    {
        all_ = wait->next;
    }
    if (wait->next != nullptr)
    {
        wait->next->prev = wait->prev;
    }
    wait->prev = nullptr;
    wait->next = nullptr;
}

void Reactor::LinkToken(Wait* wait)
{
    CancelToken* token = wait->token;
    ENGINE_ASSERT(token->reactor_.Load(MemoryOrder::Relaxed) == nullptr ||
                      token->reactor_.Load(MemoryOrder::Relaxed) == this,
                  "CancelToken: used with two reactors at once");
    wait->token_prev = nullptr;
    wait->token_next = token->waits_;
    if (token->waits_ != nullptr)
    {
        token->waits_->token_prev = wait;
    }
    token->waits_ = wait;
    token->reactor_.Store(this, MemoryOrder::SeqCst);
}

void Reactor::UnlinkToken(Wait* wait)
{
    CancelToken* token = wait->token;
    if (token == nullptr)
    {
        return;
    }
    if (wait->token_prev != nullptr)
    {
        wait->token_prev->token_next = wait->token_next;
    }
    else
    {
        token->waits_ = wait->token_next;
    }
    if (wait->token_next != nullptr)
    {
        wait->token_next->token_prev = wait->token_prev;
    }
    wait->token_prev = nullptr;
    wait->token_next = nullptr;
    if (token->waits_ == nullptr)
    {
        token->reactor_.Store(nullptr,
                              MemoryOrder::SeqCst);  // the token holds no pointer to a reactor that may go away
    }
}

void Reactor::HeapSiftUp(usize index)
{
    while (index > 0)
    {
        const usize parent = (index - 1) / 2;
        if (heap_[parent]->deadline <= heap_[index]->deadline)
        {
            break;
        }
        Wait* a = heap_[parent];
        Wait* b = heap_[index];
        heap_[parent] = b;
        heap_[index] = a;
        b->heap_index = parent;
        a->heap_index = index;
        index = parent;
    }
}

void Reactor::HeapSiftDown(usize index)
{
    for (;;)
    {
        usize smallest = index;
        const usize left = 2 * index + 1;
        const usize right = left + 1;
        if (left < heap_.Size() && heap_[left]->deadline < heap_[smallest]->deadline)
        {
            smallest = left;
        }
        if (right < heap_.Size() && heap_[right]->deadline < heap_[smallest]->deadline)
        {
            smallest = right;
        }
        if (smallest == index)
        {
            return;
        }
        Wait* a = heap_[smallest];
        Wait* b = heap_[index];
        heap_[smallest] = b;
        heap_[index] = a;
        b->heap_index = smallest;
        a->heap_index = index;
        index = smallest;
    }
}

void Reactor::HeapPush(Wait* wait)
{
    wait->heap_index = heap_.Size();
    heap_.Push(wait);
    HeapSiftUp(wait->heap_index);
}

void Reactor::HeapRemove(Wait* wait)
{
    const usize index = wait->heap_index;
    if (index == kNoHeapIndex)
    {
        return;
    }
    const usize last = heap_.Size() - 1;
    wait->heap_index = kNoHeapIndex;
    if (index != last)
    {
        heap_[index] = heap_[last];
        heap_[index]->heap_index = index;
        heap_.Pop();
        HeapSiftDown(index);
        HeapSiftUp(index);
    }
    else
    {
        heap_.Pop();
    }
}

bool Reactor::Begin(Wait* wait)
{
    LockGuard<Mutex> guard(lock_);
    wait->reactor = this;
    if (stopping_ || !running_.Load(MemoryOrder::Acquire))
    {
        wait->result = failure_.Failed() ? failure_ : Status::Of(ErrorCode::Cancelled);
        return false;
    }
    if (wait->token != nullptr && wait->token->IsCancelled())
    {
        wait->result = Status::Of(ErrorCode::Cancelled);
        return false;
    }
    if (wait->kind != WaitKind::Sleep)
    {
        FdRecord* record = fds_.Find(wait->socket);
        if (record == nullptr)
        {
            fds_.Insert(wait->socket, FdRecord{});
            record = fds_.Find(wait->socket);
        }
        Wait** slot = wait->kind == WaitKind::Readable ? &record->reader : &record->writer;
        if (*slot != nullptr)
        {
            wait->result = Status::Of(ErrorCode::Invalid);  // another coroutine already waits in this direction
            return false;
        }
        *slot = wait;
        const Status armed = poller_.Arm(wait->socket, InterestOf(*record));
        if (armed.Failed())
        {
            *slot = nullptr;
            if (record->reader == nullptr && record->writer == nullptr)
            {
                fds_.Remove(wait->socket);
            }
            wait->result = armed;
            return false;
        }
    }
    wait->done = false;
    LinkAll(wait);
    if (wait->has_deadline)
    {
        HeapPush(wait);
        if (wait->heap_index == 0)
        {
            poller_.Wake();  // the reactor thread sleeps until an older deadline: make it look again
        }
    }
    if (wait->token != nullptr)
    {
        // Register with the token first, then look at its flag again: CancelToken::Cancel sets the flag first and then
        // looks for the reactor, so one of the two sees the other and a cancel cannot slip between the early check
        // above and the registration.
        LinkToken(wait);
        if (wait->token->IsCancelled())
        {
            Ready discarded;  // the coroutine is not suspended yet: it goes on at once with Cancelled
            CompleteLocked(wait, Status::Of(ErrorCode::Cancelled), &discarded);
            return false;
        }
    }
    return true;
}

void Reactor::CompleteLocked(Wait* wait, Status result, Ready* ready)
{
    if (wait->done)
    {
        return;
    }
    wait->done = true;
    wait->result = result;
    UnlinkAll(wait);
    UnlinkToken(wait);
    HeapRemove(wait);
    if (wait->kind != WaitKind::Sleep)
    {
        FdRecord* record = fds_.Find(wait->socket);
        if (record != nullptr)
        {
            if (record->reader == wait)
            {
                record->reader = nullptr;
            }
            if (record->writer == wait)
            {
                record->writer = nullptr;
            }
            if (record->reader == nullptr && record->writer == nullptr)
            {
                poller_.Disarm(wait->socket);
                fds_.Remove(wait->socket);
            }
            else
            {
                // A one-shot registration is spent when it fires; the other direction needs its own arming again.
                RearmOrFail(wait->socket, ready);
            }
        }
    }
    ready->Push(
        wait->handle);  // the wait may be gone as soon as the coroutine runs: nothing of it is touched after this
}

void Reactor::Dispatch(const PollEvent& event, Ready* ready)
{
    FdRecord* record = fds_.Find(event.handle);
    if (record == nullptr)
    {
        poller_.Disarm(event.handle);
        return;
    }
    const bool problem = (event.ready & (kPollError | kPollHangup)) != 0;
    Wait* reader = record->reader;
    Wait* writer = record->writer;
    const bool read_ready = reader != nullptr && ((event.ready & kPollRead) != 0 || problem);
    const bool write_ready = writer != nullptr && ((event.ready & kPollWrite) != 0 || problem);
    if (read_ready)
    {
        CompleteLocked(reader, Status::Success(), ready);
    }
    if (write_ready)
    {
        CompleteLocked(writer, Status::Success(), ready);
    }
    if (!read_ready && !write_ready)
    {
        // The registration fired for something nobody waits for any more: arm what is still waited for.
        RearmOrFail(event.handle, ready);
    }
}

// Arms `handle` for whoever still waits on it; if the system refuses, those waits end with the error instead of waiting
// for ever.
void Reactor::RearmOrFail(i64 handle, Ready* ready)
{
    FdRecord* record = fds_.Find(handle);
    if (record == nullptr)
    {
        return;
    }
    const Status armed = poller_.Arm(handle, InterestOf(*record));
    if (armed.Ok())
    {
        return;
    }
    Wait* reader = record->reader;
    Wait* writer = record->writer;
    if (reader != nullptr)
    {
        CompleteLocked(reader, armed, ready);
    }
    if (writer != nullptr)
    {
        CompleteLocked(writer, armed, ready);
    }
}

void Reactor::FireTimers(Ready* ready)
{
    const u64 now = clock_->NowNanos();
    while (heap_.Size() > 0 && heap_[0]->deadline <= now)
    {
        Wait* top = heap_[0];
        CompleteLocked(top, top->kind == WaitKind::Sleep ? Status::Success() : Status::Of(ErrorCode::TimedOut), ready);
    }
}

void Reactor::Release(Ready* ready)
{
    for (usize i = 0; i < ready->Size(); ++i)
    {
        scheduler_->Schedule((*ready)[i]);
    }
    ready->Clear();
}

void Reactor::Loop()
{
    Array<PollEvent, Poller::kMaxBatch> events;
    Ready ready;
    for (;;)
    {
        i64 timeout = -1;
        {
            LockGuard<Mutex> guard(lock_);
            if (stopping_)
            {
                return;
            }
            if (heap_.Size() > 0)
            {
                const u64 now = clock_->NowNanos();
                const u64 deadline = heap_[0]->deadline;
                timeout = deadline > now ? static_cast<i64>(deadline - now) : 0;
            }
        }
        usize count = 0;
        const Status waited = poller_.Wait(events.Data(), events.Size(), timeout, &count);
        {
            LockGuard<Mutex> guard(lock_);
            if (stopping_)
            {
                return;
            }
            if (waited.Failed())
            {
                // The poller is broken: nothing can wake a waiter any more. End them all with the error instead of
                // spinning.
                failure_ = waited;
                stopping_ = true;
                while (all_ != nullptr)
                {
                    CompleteLocked(all_, waited, &ready);
                }
                running_.Store(false, MemoryOrder::Release);
                Release(&ready);
                return;
            }
            for (usize i = 0; i < count; ++i)
            {
                Dispatch(events[i], &ready);
            }
            FireTimers(&ready);
        }
        Release(&ready);
    }
}

void Reactor::CancelWaitsOf(CancelToken* token)
{
    Ready ready;
    {
        LockGuard<Mutex> guard(lock_);
        while (token->waits_ != nullptr)  // CompleteLocked takes each wait off the token's list
        {
            CompleteLocked(token->waits_, Status::Of(ErrorCode::Cancelled), &ready);
        }
    }
    Release(&ready);
}

void Reactor::ForgetLocked(i64 handle, Ready* ready)
{
    FdRecord* record = fds_.Find(handle);
    if (record != nullptr)
    {
        Wait* reader = record->reader;
        Wait* writer = record->writer;
        if (reader != nullptr)
        {
            CompleteLocked(reader, Status::Of(ErrorCode::Closed), ready);
        }
        if (writer != nullptr)
        {
            CompleteLocked(writer, Status::Of(ErrorCode::Closed), ready);
        }
    }
    poller_.Disarm(handle);
}

void Reactor::Forget(const Socket& socket)
{
    Ready ready;
    {
        LockGuard<Mutex> guard(lock_);
        ForgetLocked(socket.NativeHandle(), &ready);
    }
    Release(&ready);
}

Status Reactor::Close(Socket& socket)
{
    Ready ready;
    Status closed;
    {
        // Under the lock, so that a wait registering at this moment either comes before (and ends with Closed) or after
        // the close (and finds a descriptor the system refuses): it can never be armed on a descriptor that is closed
        // behind its back.
        LockGuard<Mutex> guard(lock_);
        ForgetLocked(socket.NativeHandle(), &ready);
        closed = socket.Close();
    }
    Release(&ready);
    return closed;
}

void Reactor::Shutdown()
{
    LockGuard<Mutex> shutdown_guard(shutdown_lock_);
    if (!running_.Load(MemoryOrder::Acquire))
    {
        return;
    }
    {
        LockGuard<Mutex> guard(lock_);
        stopping_ = true;
    }
    poller_.Wake();
    thread_.Join();
    Ready ready;
    {
        LockGuard<Mutex> guard(lock_);
        while (all_ != nullptr)
        {
            CompleteLocked(all_, Status::Of(ErrorCode::Cancelled), &ready);
        }
        running_.Store(false, MemoryOrder::Release);
    }
    Release(&ready);
}

}  // namespace engine_placeholder::net
