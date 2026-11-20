// The listener (Phase 20 Day 270): thousands of accepts, running out of descriptors and recovering, the
// unhandled-connection cap, and dual stack.

#include "build_defs.h"
#if !ENGINE_PLATFORM_WINDOWS
#include <sys/resource.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "containers/unique_ptr.h"
#include "jobs/scheduler.h"
#include "net/async.h"
#include "net/listener.h"
#include "os/os.h"
#include "sync/atomic.h"
#include "sync/thread.h"

namespace vse::net
{
namespace
{

using jobs::Job;
using jobs::Scheduler;

UniquePtr<Scheduler> ListenerTestScheduler(usize participants)
{
    Scheduler::Config cfg;
    cfg.participants = participants;
    return MakeUnique<Scheduler>(cfg);
}

struct ListenerCounts
{
    Atomic<int> accepted{0};
    Atomic<int> connected{0};
    Atomic<int> failures{0};
    Atomic<int> done{0};
};

// How long one accept or one wait for the server's close may take before the loops give up: a client that failed (out
// of ephemeral ports, say) must end the test with a failure, not leave the other loop waiting for ever.
constexpr i64 kListenerTestStepTimeoutNanos = 20'000'000'000;

// Accepts `count` connections one after the other, closing each (which is "handling" it).
Job ListenerTestAcceptLoop(Reactor* reactor, Listener* listener, int count, ListenerCounts* counts)
{
    WaitOptions options;
    options.timeout_nanos = kListenerTestStepTimeoutNanos;
    for (int i = 0; i < count; ++i)
    {
        Socket connection;
        Endpoint peer;
        const IoResult r = co_await listener->Accept(*reactor, &connection, &peer, options);
        if (r.status.Failed() || !peer.GetAddress().IsLoopback())
        {
            counts->failures.FetchAdd(1);
            break;
        }
        counts->accepted.FetchAdd(1, MemoryOrder::Relaxed);
        [[maybe_unused]] const Status closed = reactor->Close(connection);
        listener->Handled();
    }
    counts->done.FetchAdd(1, MemoryOrder::Release);
}

// Connects `count` times, closing each connection once the server has closed it. The server closes first, so the
// connection's TIME_WAIT stays on the listener's side and does not hold the client's ephemeral port (both closing at
// once leaves both sides in TIME_WAIT; on Windows, with 16,384 ephemeral ports kept for two minutes, five thousand
// connections a run beside the other socket tests ran out of ports: Phase 20 Day 278).
Job ListenerTestConnectLoop(Reactor* reactor, Endpoint at, int count, ListenerCounts* counts)
{
    WaitOptions options;
    options.timeout_nanos = kListenerTestStepTimeoutNanos;
    for (int i = 0; i < count; ++i)
    {
        Socket client;
        if (Socket::OpenTcp(at.GetAddress().Family(), &client).Failed())
        {
            counts->failures.FetchAdd(1);
            break;
        }
        const IoResult r = co_await Connect(*reactor, client, at);
        if (r.status.Failed())
        {
            counts->failures.FetchAdd(1);
            break;
        }
        counts->connected.FetchAdd(1, MemoryOrder::Relaxed);
        Array<byte, 16> buffer{};
        const IoResult end = co_await Receive(*reactor, client, MutableByteSpan(buffer.Data(), buffer.Size()), options);
        if (!end.status.Is(ErrorCode::Closed))
        {
            counts->failures.FetchAdd(1);  // the server sends nothing and closes: the end of the stream, nothing else
            break;
        }
        [[maybe_unused]] const Status closed = reactor->Close(client);
    }
    counts->done.FetchAdd(1, MemoryOrder::Release);
}

void ListenerTestWaitDone(Scheduler& scheduler, ListenerCounts& counts, int target)
{
    struct Probe
    {
        ListenerCounts* counts;
        int target;
    } probe{&counts, target};
    scheduler.HelpUntil(
        [](void* u)
        { return static_cast<Probe*>(u)->counts->done.Load(MemoryOrder::Acquire) >= static_cast<Probe*>(u)->target; },
        &probe);
}

}  // namespace

TEST_CASE("A listener accepts five thousand connections from a looping client", "[network][listener]")
{
    auto scheduler = ListenerTestScheduler(3);
    Reactor reactor(*scheduler);
    Listener listener;
    ListenerOptions options;
    options.backlog = 1024;
    if (listener.Open(Endpoint(Address::Loopback(AddressFamily::V4), 0), options).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    CHECK(listener.LocalEndpoint().Port() != 0);
    CHECK_FALSE(listener.DualStack());
    ListenerCounts counts;
    constexpr int kConnections = 5000;
    scheduler->Submit(ListenerTestAcceptLoop(&reactor, &listener, kConnections, &counts));
    scheduler->Submit(ListenerTestConnectLoop(&reactor, listener.LocalEndpoint(), kConnections, &counts));
    ListenerTestWaitDone(*scheduler, counts, 2);
    CHECK(counts.failures.Load() == 0);
    CHECK(counts.accepted.Load() == kConnections);
    CHECK(counts.connected.Load() == kConnections);
    CHECK(listener.Unhandled() == 0);
    // Binding the same port again fails.
    Listener second;
    ListenerOptions strict;
    strict.reuse_address = false;
    CHECK(second.Open(listener.LocalEndpoint(), strict).Is(ErrorCode::AddressInUse));
    CHECK(listener.Open(listener.LocalEndpoint()).Is(ErrorCode::Invalid));  // already open
}

namespace
{

struct ExhaustionOutcome
{
    IoResult accepted;
    u64 elapsed_nanos = 0;
    Atomic<int> done{0};
};

#if !ENGINE_PLATFORM_WINDOWS  // the descriptor-limit tests below, which Windows skips
Job ListenerTestAcceptOne(Reactor* reactor, Listener* listener, Socket* connection, ExhaustionOutcome* out)
{
    Endpoint peer;
    const u64 start = reactor->GetClock().NowNanos();
    out->accepted = co_await listener->Accept(*reactor, connection, &peer);
    out->elapsed_nanos = reactor->GetClock().NowNanos() - start;
    out->done.FetchAdd(1, MemoryOrder::Release);
}
#endif

}  // namespace

TEST_CASE("Running out of descriptors is survived: the accept backs off and recovers when room appears",
          "[network][listener]")
{
    auto scheduler = ListenerTestScheduler(2);
    Reactor reactor(*scheduler);
    Listener listener;
    if (listener.Open(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Socket client;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &client).Ok());
    const Status c = client.Connect(
        listener.LocalEndpoint());  // completes in the kernel: the connection waits in the listener's queue
    REQUIRE((c.Ok() || c.Is(ErrorCode::WouldBlock)));

#if ENGINE_PLATFORM_WINDOWS
    SKIP("Windows has no per-process descriptor limit to lower");
#else
    rlimit original{};
    if (::getrlimit(RLIMIT_NOFILE, &original) != 0)
    {
        SKIP("cannot read the descriptor limit here");
    }
    // Leave no room: the limit is the number of descriptors in use.
    rlimit lowered = original;
    lowered.rlim_cur = static_cast<rlim_t>(os::OpenHandleCount()) + 3;
    if (lowered.rlim_cur > original.rlim_max || ::setrlimit(RLIMIT_NOFILE, &lowered) != 0)
    {
        SKIP("cannot lower the descriptor limit here");
    }
    // Take up the free descriptors so that accept has none.
    Vector<Socket> filler;
    for (int i = 0; i < 8; ++i)
    {
        Socket s;
        if (Socket::OpenUdp(AddressFamily::V4, &s).Failed())
        {
            break;
        }
        filler.Push(static_cast<Socket&&>(s));
    }
    Socket connection;
    ExhaustionOutcome out;
    scheduler->Submit(ListenerTestAcceptOne(&reactor, &listener, &connection, &out));
    ThisThread::SleepForMilliseconds(120);
    const bool still_waiting = out.done.Load(MemoryOrder::Acquire) == 0;  // backing off, not failed and not spinning
    ::setrlimit(RLIMIT_NOFILE, &original);                                // room again
#if ENGINE_PLATFORM_MACOS
    // macOS takes the waiting connection off the queue when accept fails for lack of a descriptor, and drops it (Linux
    // leaves it queued; seen on Phase 21 Day 310): what recovers is the next connection, accepted after the back-off.
    Socket second;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &second).Ok());
    const Status c2 = second.Connect(listener.LocalEndpoint());
    REQUIRE((c2.Ok() || c2.Is(ErrorCode::WouldBlock)));
#endif
    struct Probe
    {
        ExhaustionOutcome* out;
    } probe{&out};
    scheduler->HelpUntil([](void* u) { return static_cast<Probe*>(u)->out->done.Load(MemoryOrder::Acquire) != 0; },
                         &probe);
    CHECK(still_waiting);
    CHECK(out.accepted.status.Ok());
    CHECK(connection.IsOpen());
    CHECK(out.elapsed_nanos >= 100'000'000);
    CHECK(listener.Unhandled() == 1);
#endif
}

TEST_CASE("An accept that is out of descriptors still honours its timeout", "[network][listener]")
{
    auto scheduler = ListenerTestScheduler(2);
    Reactor reactor(*scheduler);
    Listener listener;
    if (listener.Open(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Socket client;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &client).Ok());
    const Status c = client.Connect(listener.LocalEndpoint());
    REQUIRE((c.Ok() || c.Is(ErrorCode::WouldBlock)));
#if ENGINE_PLATFORM_WINDOWS
    SKIP("Windows has no per-process descriptor limit to lower");
#else
    rlimit original{};
    if (::getrlimit(RLIMIT_NOFILE, &original) != 0)
    {
        SKIP("cannot read the descriptor limit here");
    }
    rlimit lowered = original;
    lowered.rlim_cur = static_cast<rlim_t>(os::OpenHandleCount()) + 3;
    if (lowered.rlim_cur > original.rlim_max || ::setrlimit(RLIMIT_NOFILE, &lowered) != 0)
    {
        SKIP("cannot lower the descriptor limit here");
    }
    Vector<Socket> filler;
    for (int i = 0; i < 8; ++i)
    {
        Socket s;
        if (Socket::OpenUdp(AddressFamily::V4, &s).Failed())
        {
            break;
        }
        filler.Push(static_cast<Socket&&>(s));
    }
    struct Local
    {
        static Job Attempt(Reactor* reactor, Listener* listener, ExhaustionOutcome* out)
        {
            Socket connection;
            Endpoint peer;
            WaitOptions options;
            options.timeout_nanos = 40'000'000;
            const u64 start = reactor->GetClock().NowNanos();
            out->accepted = co_await listener->Accept(*reactor, &connection, &peer, options);
            out->elapsed_nanos = reactor->GetClock().NowNanos() - start;
            out->done.FetchAdd(1, MemoryOrder::Release);
        }
    };
    ExhaustionOutcome out;
    scheduler->Submit(Local::Attempt(&reactor, &listener, &out));
    struct Probe
    {
        ExhaustionOutcome* out;
    } probe{&out};
    scheduler->HelpUntil([](void* u) { return static_cast<Probe*>(u)->out->done.Load(MemoryOrder::Acquire) != 0; },
                         &probe);
    ::setrlimit(RLIMIT_NOFILE, &original);
    CHECK(out.accepted.status.Is(ErrorCode::TimedOut));
    CHECK(out.elapsed_nanos >= 40'000'000);
    CHECK(out.elapsed_nanos < 150'000'000);  // not a whole 200 ms back-off late
    CHECK(listener.Unhandled() == 0);
#endif
}

namespace
{

Job ListenerTestAcceptCounted(Reactor* reactor, Listener* listener, int count, Atomic<int>* accepted,
                              Vector<Socket>* held, Atomic<int>* done)
{
    for (int i = 0; i < count; ++i)
    {
        Endpoint peer;
        const IoResult r = co_await listener->Accept(*reactor, &(*held)[static_cast<usize>(i)], &peer);
        if (r.status.Failed())
        {
            break;
        }
        accepted->FetchAdd(1, MemoryOrder::Release);
    }
    done->FetchAdd(1, MemoryOrder::Release);
}

}  // namespace

TEST_CASE("Handled never takes the count below zero, even called from two threads at the same instant",
          "[network][listener]")
{
    Listener listener;
    ListenerOptions options;
    options.max_unhandled = 1;
    if (listener.Open(Endpoint(Address::Loopback(AddressFamily::V4), 0), options).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    int wrapped = 0;
    for (int round = 0; round < 20000 && wrapped == 0; ++round)
    {
        REQUIRE(listener.Reserve());      // the count is exactly 1
        CHECK_FALSE(listener.Reserve());  // the cap
        // Two threads report it handled at the same moment: one takes the place, the other must find none.
        Atomic<int> go{0};
        Thread a(
            [&listener, &go]
            {
                while (go.Load(MemoryOrder::Acquire) == 0)
                {
                }
                listener.Handled();
            });
        Thread b(
            [&listener, &go]
            {
                while (go.Load(MemoryOrder::Acquire) == 0)
                {
                }
                listener.Handled();
            });
        go.Store(1, MemoryOrder::Release);
        a.Join();
        b.Join();
        wrapped = listener.Unhandled() != 0 ? 1 : 0;
    }
    CHECK(wrapped == 0);
    CHECK(listener.Unhandled() == 0);
}

TEST_CASE("Unhandled connections are capped: the accept loop waits until some are handled", "[network][listener]")
{
    auto scheduler = ListenerTestScheduler(2);
    Reactor reactor(*scheduler);
    Listener listener;
    ListenerOptions options;
    options.max_unhandled = 2;
    if (listener.Open(Endpoint(Address::Loopback(AddressFamily::V4), 0), options).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Vector<Socket> clients;
    clients.Resize(5);
    for (usize i = 0; i < clients.Size(); ++i)
    {
        REQUIRE(Socket::OpenTcp(AddressFamily::V4, &clients[i]).Ok());
        const Status c = clients[i].Connect(listener.LocalEndpoint());
        REQUIRE((c.Ok() || c.Is(ErrorCode::WouldBlock)));
    }
    Vector<Socket> held;
    held.Resize(5);
    Atomic<int> accepted{0};
    Atomic<int> done{0};
    scheduler->Submit(ListenerTestAcceptCounted(&reactor, &listener, 5, &accepted, &held, &done));
    ThisThread::SleepForMilliseconds(100);
    CHECK(accepted.Load() == 2);  // the cap
    CHECK(listener.Unhandled() == 2);
    listener.Handled();
    ThisThread::SleepForMilliseconds(60);
    CHECK(accepted.Load() == 3);
    listener.Handled();
    listener.Handled();
    struct Probe
    {
        Atomic<int>* done;
        Listener* listener;
    } probe{&done, &listener};
    scheduler->HelpUntil(
        [](void* u)
        {
            Probe* p = static_cast<Probe*>(u);
            if (p->done->Load(MemoryOrder::Acquire) != 0)
            {
                return true;
            }
            p->listener->Handled();  // keep making room until the loop has taken all five
            return false;
        },
        &probe);
    CHECK(accepted.Load() == 5);
}

TEST_CASE(
    "A listener on the IPv6 any-address accepts IPv4 clients where the system allows it, and says whether it does",
    "[network][listener]")
{
    auto scheduler = ListenerTestScheduler(2);
    Reactor reactor(*scheduler);
    Listener dual;
    if (dual.Open(Endpoint(Address::Any(AddressFamily::V6), 0)).Failed())
    {
        SKIP("IPv6 sockets are unavailable here");
    }
    if (!dual.DualStack())
    {
        SKIP("this system does not allow a dual-stack listener");
    }
    ListenerCounts counts;
    scheduler->Submit(ListenerTestAcceptLoop(&reactor, &dual, 2, &counts));
    // One client over IPv4 (to the loopback of the same port), one over IPv6.
    scheduler->Submit(ListenerTestConnectLoop(
        &reactor, Endpoint(Address::Loopback(AddressFamily::V4), dual.LocalEndpoint().Port()), 1, &counts));
    scheduler->Submit(ListenerTestConnectLoop(
        &reactor, Endpoint(Address::Loopback(AddressFamily::V6), dual.LocalEndpoint().Port()), 1, &counts));
    ListenerTestWaitDone(*scheduler, counts, 3);
    CHECK(counts.failures.Load() == 0);
    CHECK(counts.accepted.Load() == 2);

    // Asked for IPv6 only: an IPv4 client is refused.
    Listener only6;
    ListenerOptions six;
    six.dual_stack = false;
    REQUIRE(only6.Open(Endpoint(Address::Any(AddressFamily::V6), 0), six).Ok());
    CHECK_FALSE(only6.DualStack());
    Socket v4_client;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &v4_client).Ok());
    struct RefuseProbe
    {
        Atomic<int> done{0};
        IoResult result;
    } probe;
    struct Local
    {
        static Job Attempt(Reactor* reactor, Socket* socket, Endpoint at, RefuseProbe* out)
        {
            out->result = co_await Connect(*reactor, *socket, at);
            out->done.FetchAdd(1, MemoryOrder::Release);
        }
    };
    scheduler->Submit(Local::Attempt(
        &reactor, &v4_client, Endpoint(Address::Loopback(AddressFamily::V4), only6.LocalEndpoint().Port()), &probe));
    scheduler->HelpUntil([](void* u) { return static_cast<RefuseProbe*>(u)->done.Load(MemoryOrder::Acquire) != 0; },
                         &probe);
    CHECK(probe.result.status.Is(ErrorCode::Refused));
}

}  // namespace vse::net
