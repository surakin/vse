// net::Poller directly (Phase 20 Day 278): a socket closed while a wait is in progress, the way Reactor::Close does it
// (Disarm, then close, from another thread), must not break the wait. On Windows the poller waits on a copy of its
// armed set, so the copy can name a handle that is closed before or during select; the day's review found that this
// failed the wait (WSAENOTSOCK), which ends the reactor for good. Real sockets: label `network`.

#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "net/poller.h"
#include "net/socket.h"
#include "sync/atomic.h"
#include "sync/thread.h"

namespace engine_placeholder::net
{
namespace
{

TEST_CASE("A socket disarmed and closed while the poller waits does not break the wait", "[network][poller]")
{
    Poller poller;
    REQUIRE(poller.IsValid());
    Socket probe;
    if (Socket::OpenUdp(AddressFamily::V4, &probe).Failed() ||
        probe.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Atomic<int> stop{0};
    Atomic<int> failed_waits{0};
    Atomic<int> waits{0};
    Thread waiter(
        [&]
        {
            Array<PollEvent, Poller::kMaxBatch> events{};
            while (stop.Load(MemoryOrder::Acquire) == 0)
            {
                usize count = 0;
                if (poller.Wait(events.Data(), events.Size(), 2'000'000, &count).Failed())
                {
                    failed_waits.FetchAdd(1, MemoryOrder::Relaxed);
                }
                waits.FetchAdd(1, MemoryOrder::Relaxed);
            }
        });
    // Each round arms a few sockets (which wakes the wait, so its next copy names them), lets the wait take that copy,
    // and closes them the way the reactor does: disarm, then close.
    for (int round = 0; round < 400; ++round)
    {
        Array<Socket, 4> sockets;
        for (Socket& s : sockets)
        {
            REQUIRE(Socket::OpenUdp(AddressFamily::V4, &s).Ok());
            REQUIRE(s.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
            REQUIRE(poller.Arm(s.NativeHandle(), kPollRead).Ok());
        }
        if (round % 4 == 0)
        {
            ThisThread::SleepForMicroseconds(300);  // sometimes close while the wait is inside the system call
        }
        for (Socket& s : sockets)
        {
            poller.Disarm(s.NativeHandle());
            REQUIRE(s.Close().Ok());
        }
    }
    stop.Store(1, MemoryOrder::Release);
    poller.Wake();
    waiter.Join();
    CHECK(waits.Load() > 0);
    CHECK(failed_waits.Load() == 0);
}

TEST_CASE("A socket closed without being disarmed is reported, not a broken wait", "[network][poller]")
{
    // The reactor always disarms first; a caller that does not still gets a working poller: the stale registration is
    // reported once (an error the next operation on the handle explains) and the wait goes on.
    Poller poller;
    REQUIRE(poller.IsValid());
    Socket s;
    if (Socket::OpenUdp(AddressFamily::V4, &s).Failed() ||
        s.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    const i64 handle = s.NativeHandle();
    REQUIRE(poller.Arm(handle, kPollRead).Ok());
    REQUIRE(s.Close().Ok());
    Array<PollEvent, Poller::kMaxBatch> events{};
    usize count = 0;
    CHECK(poller.Wait(events.Data(), events.Size(), 20'000'000, &count).Ok());
    usize again = 0;
    CHECK(poller.Wait(events.Data(), events.Size(), 20'000'000, &again).Ok());
    CHECK(again == 0);  // reported at most once
    poller.Disarm(handle);
}

TEST_CASE("A wait with no room for events is Invalid, not an instant return", "[network][poller]")
{
    // kevent with no room returns at once, which would make a waiting loop spin; epoll_wait refuses it (Day 326).
    Poller poller;
    REQUIRE(poller.IsValid());
    Array<PollEvent, 1> events;
    usize count = 7;
    CHECK(poller.Wait(events.Data(), 0, 1'000'000, &count).Is(ErrorCode::Invalid));
    CHECK(count == 0);
}

}  // namespace
}  // namespace engine_placeholder::net
