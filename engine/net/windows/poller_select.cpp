// net::Poller on Windows (Phase 20 Day 278): select over the armed sockets, with a pair of loopback UDP sockets to wake
// the wait (Windows has no eventfd).
//
// The Day 266 plan named WSAPoll (docs/superpowers/specs/2026-11-11-networking-design.md). Day 278 measured it and
// changed to select: WSAPoll reports POLLHUP whatever was asked for, so a socket whose peer has shut down its writing
// half, waited on only for writing while its send buffer is full, makes every poll return at once (the reactor thread
// would spin until the buffer drains). select reports a socket for writing only when it can be written (or the
// connection is reset), as epoll does with EPOLLOUT; the end of the stream and a reset make it readable. Both cost the
// same, O(n) per wait. IOCP stays the replacement if the measurement of Day 281 says this is the bottleneck.
//
// select keeps nothing between calls, so the poller keeps the armed set itself: each Wait copies it under the lock,
// waits on the copy without the lock (Arm and Disarm run meanwhile, and an Arm wakes the wait so that the next copy
// has it), and then reports only what is still armed in the same registration. One-shot: a reported socket stays known
// but disarmed until it is armed again.

#include "containers/array.h"
#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "mem/heap_allocator.h"
#include "net/poller.h"
#include "net/socket.h"
#include "net/windows/windows_net.h"
#include "sync/mutex.h"

namespace engine_placeholder::net
{

namespace
{

struct ArmedSocket
{
    i64 handle = -1;
    u32 interest = 0;    // kPollRead | kPollWrite; 0 while disarmed after a report
    u32 generation = 0;  // which registration: a closed handle's value can come back as a new socket
};

constexpr usize kNotFound = ~usize{0};

// Winsock's fd_set of any size. fd_set is `{ u_int fd_count; SOCKET fd_array[FD_SETSIZE]; }` and select reads
// `fd_count` entries, so the same layout with a longer array is a valid set (FD_SETSIZE, 64, is only the declared
// size; programs that need more define it larger). Slot 0 holds the count, the sockets follow. The FD_ macros are not
// used: they expand to `FAR`, which the unity wrappers take away again after the Winsock headers.
static_assert(sizeof(u_int) <= sizeof(SOCKET) && alignof(SOCKET) == sizeof(SOCKET),
              "fd_set's array must start one SOCKET after the count");

class SocketSet
{
public:
    // Empties the set and makes room for `capacity` sockets; false when out of memory.
    bool Prepare(usize capacity)
    {
        if (!slots_.TryResize(capacity + 1))
        {
            return false;
        }
        slots_[0] = 0;
        return true;
    }
    void Add(SOCKET socket)
    {
        const u_int count = Count();
        slots_[1 + count] = socket;
        const u_int grown = count + 1;
        MemCopy(slots_.Data(), &grown, sizeof(grown));
    }
    u_int Count() const
    {
        u_int count = 0;
        MemCopy(&count, slots_.Data(), sizeof(count));
        return count;
    }
    // After select: the socket at `i` (below Count) of the ready ones.
    SOCKET At(u_int i) const { return slots_[1 + i]; }
    // The set for select; null when empty (select takes no empty set).
    fd_set* Native() { return Count() == 0 ? nullptr : reinterpret_cast<fd_set*>(slots_.Data()); }

private:
    Vector<SOCKET> slots_;
};

}  // namespace

struct Poller::ArmedSet
{
    Mutex lock;
    Vector<ArmedSocket> sockets;  // guarded by `lock`
    u32 next_generation = 1;      // guarded by `lock`
    bool waiting = false;         // guarded by `lock`: a Wait waits on a copy taken before the latest Arm
    Socket wake_reader;           // bound to loopback; the wait watches it for reading
    Socket wake_writer;           // connected to the reader; Wake sends one byte
    // The waiting thread's copy and sets (Wait is called by one thread at a time).
    Vector<ArmedSocket> polled;
    SocketSet readable;
    SocketSet writable;
    SocketSet failed;  // exceptfds: a connect that failed
    // Container audit Day 12: where each handle is in `sockets` (guarded by `lock`), and what select reported for each
    // ready socket (the waiting thread's), so a wait is linear in the armed sockets instead of quadratic.
    HashMap<i64, usize, HeapAllocator> index{HeapAllocator::Default()};
    HashMap<SOCKET, u32, HeapAllocator> ready{HeapAllocator::Default()};

    usize IndexOf(i64 handle) const
    {
        const usize* at = index.Find(handle);
        return at == nullptr ? kNotFound : *at;
    }

    // Gathers the three sets select left into `ready`, one lookup per socket afterwards.
    void CollectReady()
    {
        ready.Clear();
        Gather(readable, kPollRead);
        Gather(writable, kPollWrite);
        Gather(failed, kPollError);
    }

    void Gather(const SocketSet& set, u32 bit)
    {
        const u_int count = set.Count();
        for (u_int i = 0; i < count; ++i)
        {
            if (u32* bits = ready.Find(set.At(i)))
            {
                *bits |= bit;
            }
            else
            {
                ready.Insert(set.At(i), bit);
            }
        }
    }

    u32 ReadyBits(SOCKET socket) const
    {
        const u32* bits = ready.Find(socket);
        return bits == nullptr ? 0 : *bits;
    }
};

namespace
{

// Whether `handle` is still an open socket: the check epoll_ctl makes on a descriptor.
bool HandleIsSocket(i64 handle)
{
    int type = 0;
    int size = static_cast<int>(sizeof(type));
    return ::getsockopt(windows::SocketOf(handle), SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &size) == 0;
}

// Opens the wake-up pair: a UDP socket bound to an ephemeral loopback port and a second one connected to it.
bool OpenWakePair(Socket* reader, Socket* writer)
{
    const Endpoint any_port(Address::Loopback(AddressFamily::V4), 0);
    Endpoint bound;
    return Socket::OpenUdp(AddressFamily::V4, reader).Ok() && reader->Bind(any_port).Ok() &&
           reader->LocalEndpoint(&bound).Ok() && Socket::OpenUdp(AddressFamily::V4, writer).Ok() &&
           writer->Connect(bound).Ok();
}

}  // namespace

Poller::Poller()
{
    if (windows::EnsureWinsock().Failed())
    {
        return;
    }
    UniquePtr<ArmedSet> armed = MakeUnique<ArmedSet>();
    if (!OpenWakePair(&armed->wake_reader, &armed->wake_writer))
    {
        return;
    }
    poller_ = armed->wake_reader.NativeHandle();
    wake_ = armed->wake_writer.NativeHandle();
    armed_ = Move(armed);
}

Poller::~Poller() = default;  // the armed set closes the wake-up pair

Status Poller::Arm(i64 handle, u32 interest)
{
    if (!armed_)
    {
        return Status::Of(ErrorCode::Invalid);  // the poller could not be made (IsValid is false)
    }
    // The system's own check of the handle, as epoll_ctl makes one: a closed or foreign handle is refused here, not at
    // the next wait.
    int type = 0;
    int size = static_cast<int>(sizeof(type));
    if (::getsockopt(windows::SocketOf(handle), SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &size) != 0)
    {
        return windows::LastWsaStatus();
    }
    bool wake = false;
    {
        LockGuard<Mutex> guard(armed_->lock);
        const usize at = armed_->IndexOf(handle);
        if (at == kNotFound)
        {
            if (!armed_->sockets.TryReserve(armed_->sockets.Size() + 1))
            {
                return Status::Of(ErrorCode::LimitExceeded);
            }
            ArmedSocket entry;
            entry.handle = handle;
            entry.interest = interest;
            entry.generation = armed_->next_generation++;
            armed_->index.Insert(handle, armed_->sockets.Size());
            armed_->sockets.Push(entry);
        }
        else
        {
            armed_->sockets[at].interest = interest;
        }
        wake = armed_->waiting;
    }
    if (wake)
    {
        Wake();  // the wait in progress does not have this registration: make it take a new copy
    }
    return Status::Success();
}

void Poller::Disarm(i64 handle)
{
    if (!armed_)
    {
        return;
    }
    // No wake-up: the wait in progress may still report the handle, and the report is dropped because it is no longer
    // armed.
    LockGuard<Mutex> guard(armed_->lock);
    const usize at = armed_->IndexOf(handle);
    if (at != kNotFound)
    {
        const usize last = armed_->sockets.Size() - 1;
        armed_->index.Remove(handle);
        if (at != last)
        {
            armed_->sockets[at] = armed_->sockets[last];
            armed_->index.Insert(armed_->sockets[at].handle, at);  // the last entry moved into the hole
        }
        armed_->sockets.Pop();
    }
}

Status Poller::Wait(PollEvent* events, usize capacity, i64 timeout_nanos, usize* count)
{
    *count = 0;
    if (!armed_ || capacity == 0)
    {
        // No room: select would report sockets nobody can be told of, so a waiting loop would spin; epoll_wait refuses
        // it (EINVAL) and so does the kqueue poller.
        return Status::Of(ErrorCode::Invalid);
    }
    ArmedSet& a = *armed_;
    {
        LockGuard<Mutex> guard(a.lock);
        const usize n = a.sockets.Size();
        if (!a.polled.TryReserve(n) || !a.readable.Prepare(n + 1) || !a.writable.Prepare(n) || !a.failed.Prepare(n))
        {
            return Status::Of(ErrorCode::LimitExceeded);
        }
        a.polled.Clear();
        a.readable.Add(windows::SocketOf(poller_));
        for (usize i = 0; i < n; ++i)
        {
            const ArmedSocket& s = a.sockets[i];
            if (s.interest == 0)
            {
                continue;
            }
            a.polled.Push(s);
            if ((s.interest & kPollRead) != 0)
            {
                a.readable.Add(windows::SocketOf(s.handle));
            }
            if ((s.interest & kPollWrite) != 0)
            {
                a.writable.Add(windows::SocketOf(s.handle));
                a.failed.Add(windows::SocketOf(s.handle));
            }
        }
        a.waiting = true;
    }
    timeval timeout{};
    timeval* timeout_pointer = nullptr;
    if (timeout_nanos >= 0)
    {
        const i64 micros = (timeout_nanos + 999) / 1000;  // rounded up: never early
        const i64 seconds = micros / 1'000'000 > 100'000'000 ? 100'000'000 : micros / 1'000'000;
        timeout.tv_sec = static_cast<long>(seconds);
        timeout.tv_usec = static_cast<long>(micros % 1'000'000);
        timeout_pointer = &timeout;
    }
    // The first argument is ignored by Winsock.
    const int ready = ::select(0, a.readable.Native(), a.writable.Native(), a.failed.Native(), timeout_pointer);
    const int select_error = ready == SOCKET_ERROR ? ::WSAGetLastError() : 0;
    LockGuard<Mutex> guard(a.lock);
    a.waiting = false;
    if (select_error == WSAENOTSOCK)
    {
        // The copy names a handle that was closed after it was taken (Reactor::Close disarms and closes while this
        // thread waits), or one closed without being disarmed. Neither breaks the poller: a closed handle that is still
        // armed in the same registration is reported once as an error (the next operation on it says what happened),
        // one that was disarmed is dropped, and the next wait takes a new copy.
        const usize limit = capacity < kMaxBatch ? capacity : kMaxBatch;
        usize reported = 0;
        for (usize i = 0; i < a.polled.Size() && reported < limit; ++i)
        {
            if (HandleIsSocket(a.polled[i].handle))
            {
                continue;
            }
            const usize at = a.IndexOf(a.polled[i].handle);
            if (at == kNotFound || a.sockets[at].generation != a.polled[i].generation || a.sockets[at].interest == 0)
            {
                continue;
            }
            a.sockets[at].interest = 0;
            events[reported].handle = a.sockets[at].handle;
            events[reported].ready = kPollError;
            ++reported;
        }
        *count = reported;
        return Status::Success();
    }
    if (select_error != 0)
    {
        return windows::StatusFromWsa(select_error);  // the poller is broken
    }
    if (ready == 0)
    {
        return Status::Success();  // the timeout
    }
    a.CollectReady();
    if ((a.ReadyBits(windows::SocketOf(poller_)) & kPollRead) != 0)
    {
        // Drain the wake-ups: every byte sent since the last wait. The reader is non-blocking, so the loop ends with
        // WouldBlock (or an error, which leaves nothing to drain).
        Array<byte, 64> drained;
        usize n = 0;
        while (a.wake_reader.Receive(MutableByteSpan(drained.Data(), drained.Size()), &n).Ok())
        {
        }
    }
    const usize limit = capacity < kMaxBatch ? capacity : kMaxBatch;
    usize reported = 0;
    for (usize i = 0; i < a.polled.Size() && reported < limit; ++i)
    {
        // kPollError from exceptfds: the connect failed, and FinishConnect reads the error.
        u32 bits = a.ReadyBits(windows::SocketOf(a.polled[i].handle));
        if (bits == 0)
        {
            continue;
        }
        const usize at = a.IndexOf(a.polled[i].handle);
        if (at == kNotFound)
        {
            continue;  // disarmed during the wait
        }
        ArmedSocket& s = a.sockets[at];
        if (s.generation != a.polled[i].generation || s.interest == 0)
        {
            continue;  // a new socket with the same handle value, or already reported
        }
        bits &= s.interest | kPollError;
        if (bits == 0)
        {
            continue;  // re-armed for another direction during the wait: wait again
        }
        s.interest = 0;  // one notification per arming
        events[reported].handle = s.handle;
        events[reported].ready = bits;
        ++reported;
    }
    *count = reported;
    return Status::Success();
}

void Poller::Wake()
{
    if (!armed_)
    {
        return;
    }
    const byte one{1};
    usize sent = 0;
    // A full socket buffer means wake-ups are already pending, so the wait returns at once either way.
    [[maybe_unused]] const Status ignored = armed_->wake_writer.Send(ByteSpan(&one, 1), &sent);
}

}  // namespace engine_placeholder::net
