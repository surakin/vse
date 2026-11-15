// net::Poller directly (Phase 20 Day 278): a socket closed while a wait is in progress, the way Reactor::Close does it
// (Disarm, then close, from another thread), must not break the wait. On Windows the poller waits on a copy of its
// armed set, so the copy can name a handle that is closed before or during select; the day's review found that this
// failed the wait (WSAENOTSOCK), which ends the reactor for good. Real sockets: label `network`.

#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "containers/vector.h"
#include "net/poller.h"
#include "net/socket.h"
#include "sync/atomic.h"
#include "os/console.h"
#include "os/os.h"
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
    // kevent with no room returns at once, which would make a waiting loop spin; epoll_wait refuses it (Day 310).
    Poller poller;
    REQUIRE(poller.IsValid());
    Array<PollEvent, 1> events;
    usize count = 7;
    CHECK(poller.Wait(events.Data(), 0, 1'000'000, &count).Is(ErrorCode::Invalid));
    CHECK(count == 0);
}

// The poller's cost at scale (written on the Windows PC on 2026-11-15, for the Phase 20 entry "not measured at 1,000
// sockets"): 1,000 loopback UDP sockets armed for reading, and in each round 0, 1, 16 or all 1,000 of them made ready
// by one datagram each; the wait and the re-arm are timed. Hidden: run from a Release build with
// `net_tests_sockets "[poller-bench]"`; it prints one line per case.
TEST_CASE("Benchmark: the poller with 1,000 armed sockets", "[.][net-bench][poller-bench]")
{
    constexpr usize kSockets = 1000;
    Poller poller;
    REQUIRE(poller.IsValid());
    Vector<Socket> receivers;
    Vector<Endpoint> addresses;
    receivers.Resize(kSockets);
    addresses.Resize(kSockets);
    for (usize i = 0; i < kSockets; ++i)
    {
        REQUIRE(Socket::OpenUdp(AddressFamily::V4, &receivers[i]).Ok());
        REQUIRE(receivers[i].Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
        REQUIRE(receivers[i].LocalEndpoint(&addresses[i]).Ok());
        REQUIRE(poller.Arm(receivers[i].NativeHandle(), kPollRead).Ok());
    }
    Socket sender;
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &sender).Ok());
    Array<PollEvent, Poller::kMaxBatch> events{};
    const byte one = 1;
    for (const usize ready : {usize{0}, usize{1}, usize{16}, kSockets})
    {
        const usize rounds = ready == kSockets ? 50 : 500;
        u64 wait_nanos = 0;
        u64 arm_nanos = 0;
        usize waits = 0;
        usize arms = 0;
        usize reported = 0;
        for (usize round = 0; round < rounds; ++round)
        {
            const usize first = (round * 37) % kSockets;
            for (usize k = 0; k < ready; ++k)
            {
                usize sent = 0;
                Status status = sender.SendTo(ByteSpan(&one, 1), addresses[(first + k) % kSockets], &sent);
                while (status.Is(ErrorCode::WouldBlock))  // a full send buffer (1,000 datagrams in a row)
                {
                    ThisThread::Yield();
                    status = sender.SendTo(ByteSpan(&one, 1), addresses[(first + k) % kSockets], &sent);
                }
                REQUIRE(status.Ok());
            }
            usize collected = 0;
            do
            {
                usize count = 0;
                const u64 start = os::MonotonicTimeNanos();
                REQUIRE(poller.Wait(events.Data(), events.Size(), ready == 0 ? 0 : 1'000'000'000, &count).Ok());
                wait_nanos += os::MonotonicTimeNanos() - start;
                ++waits;
                if (ready != 0)
                {
                    REQUIRE(count > 0);  // a second without the datagrams that were sent
                }
                collected += count;
            } while (collected < ready);
            reported += collected;
            for (usize k = 0; k < ready; ++k)
            {
                Socket& s = receivers[(first + k) % kSockets];
                Array<byte, 16> buffer;
                usize received = 0;
                Endpoint from;
                while (s.ReceiveFrom(MutableByteSpan(buffer.Data(), buffer.Size()), &received, &from).Ok())
                {
                }
                const u64 start = os::MonotonicTimeNanos();
                REQUIRE(poller.Arm(s.NativeHandle(), kPollRead).Ok());
                arm_nanos += os::MonotonicTimeNanos() - start;
                ++arms;
            }
        }
        CHECK(reported == ready * rounds);
        os::Print("net-bench: poller, %zu armed sockets, %zu ready: %.1f us a wait (%zu waits, %.1f events each), "
                  "%.2f us a re-arm\n",
                  kSockets, ready, static_cast<f64>(wait_nanos) / 1000.0 / static_cast<f64>(waits), waits,
                  static_cast<f64>(reported) / static_cast<f64>(waits),
                  arms == 0 ? 0.0 : static_cast<f64>(arm_nanos) / 1000.0 / static_cast<f64>(arms));
    }
    for (usize i = 0; i < kSockets; ++i)
    {
        poller.Disarm(receivers[i].NativeHandle());
    }
}

}  // namespace
}  // namespace engine_placeholder::net
