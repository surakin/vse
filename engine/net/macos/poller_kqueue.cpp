// net::Poller on macOS (Phase 21 Day 310): kqueue, with one-shot read and write filters and an EVFILT_USER event to
// wake the wait.
//
// kqueue keeps reading and writing as two filters of a socket, where epoll has one registration: `Arm` adds the
// filters of the interest (each EV_ONESHOT, so a report disarms it, and checked by level when added, so a socket that
// is already ready is reported at once) and deletes the filter of a direction no longer asked for. When one direction
// fires, the other stays armed until it fires or the socket is armed again; the reactor arms what is still waited for
// after every report and ignores a report nobody waits for, so this is the interface's one-shot behavior. A wait's two
// filters of one socket are merged into one event. Closing a descriptor removes its filters.

#include <fcntl.h>
#include <sys/event.h>
#include <time.h>

#include "containers/array.h"
#include "net/poller.h"
#include "net/posix/posix_net.h"
#include "os/last_error.h"
#include "os/os.h"

namespace vse::net
{

namespace
{

constexpr usize kWakeIdent = 1;  // the EVFILT_USER event's identifier (its own name space, apart from descriptors)

// One kevent change; `flags` EV_ADD, EV_DELETE, ...
struct kevent Change(i64 handle, short filter, u16 flags)
{
    struct kevent k;
    EV_SET(&k, static_cast<usize>(handle), filter, flags, 0, 0, nullptr);
    return k;
}

}  // namespace

Poller::Poller()
{
    const int queue = ::kqueue();
    if (queue < 0)
    {
        return;
    }
    // A kqueue is not inherited by fork; close-on-exec keeps it from an exec'd process as well.
    struct kevent wake;
    EV_SET(&wake, kWakeIdent, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (::fcntl(queue, F_SETFD, FD_CLOEXEC) != 0 || ::kevent(queue, &wake, 1, nullptr, 0, nullptr) != 0)
    {
        os::Close(queue);
        return;
    }
    poller_ = queue;
    wake_ = static_cast<i64>(kWakeIdent);  // not a descriptor: the user event of the same kqueue
}

Poller::~Poller()
{
    if (poller_ >= 0)
    {
        os::Close(static_cast<int>(poller_));
    }
}

Status Poller::Arm(i64 handle, u32 interest)
{
    // EV_RECEIPT reports each change's result separately: a delete of a filter that was never added (ENOENT) is not a
    // failure, an add the system refuses (a closed descriptor: EBADF) is.
    Array<struct kevent, 2> changes;
    changes[0] = Change(handle, EVFILT_READ,
                        (interest & kPollRead) != 0 ? (EV_ADD | EV_ENABLE | EV_ONESHOT | EV_RECEIPT)
                                                    : (EV_DELETE | EV_RECEIPT));
    changes[1] = Change(handle, EVFILT_WRITE,
                        (interest & kPollWrite) != 0 ? (EV_ADD | EV_ENABLE | EV_ONESHOT | EV_RECEIPT)
                                                     : (EV_DELETE | EV_RECEIPT));
    Array<struct kevent, 2> results;
    const int n = ::kevent(static_cast<int>(poller_), changes.Data(), 2, results.Data(), 2, nullptr);
    if (n < 0)
    {
        return posix::StatusFromErrno(os::LastError());
    }
    for (int i = 0; i < n; ++i)
    {
        const struct kevent& r = results[static_cast<usize>(i)];
        const int error = (r.flags & EV_ERROR) != 0 ? static_cast<int>(r.data) : 0;
        const bool deleting = r.filter == EVFILT_READ ? (interest & kPollRead) == 0 : (interest & kPollWrite) == 0;
        if (error != 0 && !(deleting && error == ENOENT))
        {
            return posix::StatusFromErrno(error);
        }
    }
    return Status::Success();
}

void Poller::Disarm(i64 handle)
{
    Array<struct kevent, 2> changes;
    changes[0] = Change(handle, EVFILT_READ, EV_DELETE | EV_RECEIPT);
    changes[1] = Change(handle, EVFILT_WRITE, EV_DELETE | EV_RECEIPT);
    Array<struct kevent, 2> results;
    // A filter that is not there, or a descriptor already closed (which removed its filters), is what Disarm wants.
    [[maybe_unused]] const int ignored = ::kevent(static_cast<int>(poller_), changes.Data(), 2, results.Data(), 2,
                                                  nullptr);
}

Status Poller::Wait(PollEvent* events, usize capacity, i64 timeout_nanos, usize* count)
{
    *count = 0;
    if (capacity == 0)
    {
        return Status::Of(ErrorCode::Invalid);  // kevent would return at once; epoll_wait refuses it (EINVAL)
    }
    Array<struct kevent, kMaxBatch> raw;
    const usize limit = capacity < kMaxBatch ? capacity : kMaxBatch;
    timespec timeout{};
    timespec* wait_for = nullptr;  // for ever
    if (timeout_nanos >= 0)
    {
        // kevent refuses a timeout of centuries (EINVAL); at most a million seconds per wait, as the epoll poller
        // caps its milliseconds, and a longer sleep waits again.
        const i64 seconds = timeout_nanos / 1'000'000'000;
        timeout.tv_sec = static_cast<time_t>(seconds > 1'000'000 ? 1'000'000 : seconds);
        timeout.tv_nsec = static_cast<long>(timeout_nanos % 1'000'000'000);  // nanoseconds: never early
        wait_for = &timeout;
    }
    const int n = ::kevent(static_cast<int>(poller_), nullptr, 0, raw.Data(), static_cast<int>(limit), wait_for);
    if (n < 0)
    {
        // An interrupted wait is not an error; anything else (a bad descriptor) means the poller is broken.
        return os::LastError() == EINTR ? Status::Success() : posix::StatusFromErrno(os::LastError());
    }
    usize reported = 0;
    for (int i = 0; i < n; ++i)
    {
        const struct kevent& k = raw[static_cast<usize>(i)];
        if (k.filter == EVFILT_USER)
        {
            continue;  // the wake-up (EV_CLEAR resets it)
        }
        u32 ready = 0;
        if (k.filter == EVFILT_READ)
        {
            ready |= kPollRead;  // data, a connection to accept, or the end of the stream (EV_EOF): a read side event
        }
        else if (k.filter == EVFILT_WRITE)
        {
            ready |= kPollWrite;
            if ((k.flags & EV_EOF) != 0)
            {
                ready |= kPollHangup;  // nothing more can be written
            }
        }
        if ((k.flags & EV_EOF) != 0 && k.fflags != 0)
        {
            ready |= kPollError;  // the socket's pending error (fflags holds it)
        }
        if ((k.flags & EV_ERROR) != 0)
        {
            ready |= kPollError;
        }
        const i64 handle = static_cast<i64>(k.ident);
        usize at = reported;
        for (usize j = 0; j < reported; ++j)
        {
            if (events[j].handle == handle)
            {
                at = j;  // the socket's other filter in the same wait: one event
                break;
            }
        }
        if (at == reported)
        {
            events[reported].handle = handle;
            events[reported].ready = 0;
            ++reported;
        }
        events[at].ready |= ready;
    }
    *count = reported;
    return Status::Success();
}

void Poller::Wake()
{
    struct kevent wake;
    EV_SET(&wake, kWakeIdent, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
    // The event exists from the constructor; a trigger that fails leaves the wait to its timeout, which callers allow.
    [[maybe_unused]] const int ignored = ::kevent(static_cast<int>(poller_), &wake, 1, nullptr, 0, nullptr);
}

}  // namespace vse::net
