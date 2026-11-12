// net::Poller on Linux: epoll with one-shot level-triggered registrations and an eventfd to wake the wait.

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "containers/array.h"
#include "net/poller.h"
#include "net/posix/posix_net.h"
#include "os/last_error.h"
#include "os/os.h"

namespace engine_placeholder::net
{

namespace
{

constexpr u64 kWakeTag = ~u64{0};  // epoll_event.data of the wake-up descriptor; never a socket handle

u32 ToEpoll(u32 interest)
{
    u32 events = EPOLLONESHOT;
    if ((interest & kPollRead) != 0)
    {
        events |= EPOLLIN | EPOLLRDHUP;
    }
    if ((interest & kPollWrite) != 0)
    {
        events |= EPOLLOUT;
    }
    return events;
}

}  // namespace

Poller::Poller()
{
    const int epoll = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll < 0)
    {
        return;
    }
    const int wake = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake < 0)
    {
        os::Close(epoll);
        return;
    }
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = kWakeTag;
    if (::epoll_ctl(epoll, EPOLL_CTL_ADD, wake, &event) != 0)
    {
        os::Close(wake);
        os::Close(epoll);
        return;
    }
    poller_ = epoll;
    wake_ = wake;
}

Poller::~Poller()
{
    if (wake_ >= 0)
    {
        os::Close(static_cast<int>(wake_));
    }
    if (poller_ >= 0)
    {
        os::Close(static_cast<int>(poller_));
    }
}

Status Poller::Arm(i64 handle, u32 interest)
{
    epoll_event event{};
    event.events = ToEpoll(interest);
    event.data.u64 = static_cast<u64>(handle);
    if (::epoll_ctl(static_cast<int>(poller_), EPOLL_CTL_MOD, static_cast<int>(handle), &event) == 0)
    {
        return Status::Success();
    }
    if (os::LastError() != ENOENT)
    {
        return posix::StatusFromErrno(os::LastError());
    }
    if (::epoll_ctl(static_cast<int>(poller_), EPOLL_CTL_ADD, static_cast<int>(handle), &event) != 0)
    {
        return posix::StatusFromErrno(os::LastError());
    }
    return Status::Success();
}

void Poller::Disarm(i64 handle)
{
    epoll_event ignored{};
    ::epoll_ctl(static_cast<int>(poller_), EPOLL_CTL_DEL, static_cast<int>(handle), &ignored);
}

Status Poller::Wait(PollEvent* events, usize capacity, i64 timeout_nanos, usize* count)
{
    *count = 0;
    Array<epoll_event, kMaxBatch> raw;
    const int max_events = capacity < kMaxBatch ? static_cast<int>(capacity) : static_cast<int>(kMaxBatch);
    int timeout_ms = -1;
    if (timeout_nanos >= 0)
    {
        const i64 ms = (timeout_nanos + 999'999) / 1'000'000;  // rounded up: never early
        timeout_ms = ms > 1'000'000'000 ? 1'000'000'000 : static_cast<int>(ms);
    }
    const int n = ::epoll_wait(static_cast<int>(poller_), raw.Data(), max_events, timeout_ms);
    if (n < 0)
    {
        // An interrupted wait is not an error; anything else (a bad descriptor) means the poller is broken.
        return os::LastError() == EINTR ? Status::Success() : posix::StatusFromErrno(os::LastError());
    }
    usize reported = 0;
    for (int i = 0; i < n; ++i)
    {
        if (raw[static_cast<usize>(i)].data.u64 == kWakeTag)
        {
            u64 drained = 0;
            // Reading resets the counter; there is nothing to do if it fails (the descriptor is non-blocking).
            [[maybe_unused]] const long ignored = os::Read(static_cast<int>(wake_), &drained, sizeof(drained));
            continue;
        }
        u32 ready = 0;
        if ((raw[static_cast<usize>(i)].events & (EPOLLIN | EPOLLRDHUP)) != 0)
        {
            ready |= kPollRead;  // the peer's shutdown of its writing half is read as the end of the stream: a read
                                 // side event only
        }
        if ((raw[static_cast<usize>(i)].events & EPOLLOUT) != 0)
        {
            ready |= kPollWrite;
        }
        if ((raw[static_cast<usize>(i)].events & EPOLLERR) != 0)
        {
            ready |= kPollError;
        }
        if ((raw[static_cast<usize>(i)].events & EPOLLHUP) != 0)
        {
            ready |= kPollHangup;
        }
        events[reported].handle = static_cast<i64>(raw[static_cast<usize>(i)].data.u64);
        events[reported].ready = ready;
        ++reported;
    }
    *count = reported;
    return Status::Success();
}

void Poller::Wake()
{
    const u64 one = 1;
    // A full counter (2^64 wake-ups unread) still means the wait returns at once, so a failed write is harmless.
    [[maybe_unused]] const long ignored = os::Write(static_cast<int>(wake_), &one, sizeof(one));
}

}  // namespace engine_placeholder::net
