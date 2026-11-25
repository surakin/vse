// Name resolution and the connect helpers (Phase 20 Day 270): numeric hosts without the resolver thread, the system
// resolver for `localhost`, a hostile result list, a slow resolver that is abandoned, a full queue, shutdown, and
// ConnectAny.

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "jobs/scheduler.h"
#include "net/async.h"
#include "net/listener.h"
#include "net/resolve.h"
#include "os/os.h"
#include "sync/atomic.h"
#include "sync/thread.h"

namespace vse::net
{
namespace
{

using jobs::Job;
using jobs::Scheduler;

UniquePtr<Scheduler> ResolveTestScheduler(usize participants)
{
    Scheduler::Config cfg;
    cfg.participants = participants;
    return MakeUnique<Scheduler>(cfg);
}

Atomic<int> g_resolver_calls{0};

// Resolver functions of the tests. A name resolves to a fixed endpoint, or to a flood, or after a delay.
Status ResolveTestCounting(StringView, u16 port, AddressFamily, EndpointSet* out)
{
    g_resolver_calls.FetchAdd(1);
    out->Add(Endpoint(Address::V4(10, 0, 0, 1), port));
    return Status::Success();
}

Status ResolveTestFlood(StringView, u16 port, AddressFamily, EndpointSet* out)
{
    for (u32 i = 0; i < 1000; ++i)
    {
        out->Add(Endpoint(Address::V4(10, 1, static_cast<u8>(i >> 8), static_cast<u8>(i)),
                          port));  // adds past the capacity are refused
    }
    return Status::Success();
}

Status ResolveTestSlow(StringView, u16 port, AddressFamily, EndpointSet* out)
{
    ThisThread::SleepForMilliseconds(200);
    out->Add(Endpoint(Address::V4(10, 0, 0, 2), port));
    return Status::Success();
}

Status ResolveTestEmpty(StringView, u16, AddressFamily, EndpointSet*)
{
    return Status::Success();  // success with nothing found
}

struct ResolveOutcome
{
    ResolveResult result;
    u64 elapsed_nanos = 0;
    Atomic<int> done{0};
};

Job ResolveTestJob(Reactor* reactor, Resolver* resolver, const char* host, u16 port, AddressFamily family,
                   WaitOptions options, ResolveOutcome* out)
{
    const u64 start = reactor->GetClock().NowNanos();
    out->result = co_await Resolve(*reactor, *resolver, StringView(host), port, family, options);
    out->elapsed_nanos = reactor->GetClock().NowNanos() - start;
    out->done.FetchAdd(1, MemoryOrder::Release);
}

void ResolveTestWait(Scheduler& scheduler, ResolveOutcome& out)
{
    struct Probe
    {
        ResolveOutcome* out;
    } probe{&out};
    scheduler.HelpUntil([](void* u) { return static_cast<Probe*>(u)->out->done.Load(MemoryOrder::Acquire) != 0; },
                        &probe);
}

}  // namespace

TEST_CASE("A numeric host resolves without the resolver thread", "[network][resolve]")
{
    auto scheduler = ResolveTestScheduler(2);
    Reactor reactor(*scheduler);
    Resolver resolver(&ResolveTestCounting);
    g_resolver_calls.Store(0);
    struct Case
    {
        const char* host;
        AddressFamily family;
        bool ok;
        const char* text;
    } cases[] = {
        {"127.0.0.1", AddressFamily::None, true, "127.0.0.1:80"},
        {"::1", AddressFamily::None, true, "[::1]:80"},
        {"[::1]", AddressFamily::None, true, "[::1]:80"},
        {"10.20.30.40", AddressFamily::V4, true, "10.20.30.40:80"},
        {"127.0.0.1", AddressFamily::V6, false, ""},  // an address of the other family than asked for
        {"::1", AddressFamily::V4, false, ""},
    };
    for (const Case& c : cases)
    {
        ResolveOutcome out;
        scheduler->Submit(ResolveTestJob(&reactor, &resolver, c.host, 80, c.family, {}, &out));
        ResolveTestWait(*scheduler, out);
        CAPTURE(c.host);
        CHECK(out.result.status.Ok() == c.ok);
        if (c.ok)
        {
            REQUIRE(out.result.endpoints.Count() == 1);
            CHECK(out.result.endpoints[0].Format().View() == StringView(c.text));
        }
        else
        {
            CHECK(out.result.status.Is(ErrorCode::NotFound));
            CHECK(out.result.endpoints.Empty());
        }
    }
    CHECK(g_resolver_calls.Load() == 0);  // none of them went to the resolver
    // An empty name, or one with a zero byte in it (the name checked must be the name looked up), is refused.
    ResolveOutcome empty;
    scheduler->Submit(ResolveTestJob(&reactor, &resolver, "", 80, AddressFamily::None, {}, &empty));
    ResolveTestWait(*scheduler, empty);
    CHECK(empty.result.status.Is(ErrorCode::Invalid));
    CHECK(g_resolver_calls.Load() == 0);
}

TEST_CASE("A name goes to the resolver thread and its endpoints come back", "[network][resolve]")
{
    auto scheduler = ResolveTestScheduler(3);
    Reactor reactor(*scheduler);
    Resolver resolver(&ResolveTestCounting);
    g_resolver_calls.Store(0);
    ResolveOutcome out;
    scheduler->Submit(ResolveTestJob(&reactor, &resolver, "game.example", 4433, AddressFamily::None, {}, &out));
    ResolveTestWait(*scheduler, out);
    CHECK(out.result.status.Ok());
    REQUIRE(out.result.endpoints.Count() == 1);
    CHECK(out.result.endpoints[0].Format().View() == StringView("10.0.0.1:4433"));
    CHECK(g_resolver_calls.Load() == 1);
}

TEST_CASE("The system resolver finds localhost, and refuses a name that does not exist", "[network][resolve]")
{
    auto scheduler = ResolveTestScheduler(2);
    Reactor reactor(*scheduler);
    Resolver resolver;  // the system's
    ResolveOutcome local;
    scheduler->Submit(ResolveTestJob(&reactor, &resolver, "localhost", 7777, AddressFamily::None, {}, &local));
    ResolveTestWait(*scheduler, local);
    if (local.result.status.Failed())
    {
        SKIP("this machine does not resolve localhost: " << ErrorCodeName(local.result.status.Code()));
    }
    REQUIRE(local.result.endpoints.Count() >= 1);
    for (usize i = 0; i < local.result.endpoints.Count(); ++i)
    {
        CHECK(local.result.endpoints[i].GetAddress().IsLoopback());
        CHECK(local.result.endpoints[i].Port() == 7777);
    }
    // One family only.
    ResolveOutcome v4;
    scheduler->Submit(ResolveTestJob(&reactor, &resolver, "localhost", 1, AddressFamily::V4, {}, &v4));
    ResolveTestWait(*scheduler, v4);
    if (v4.result.status.Ok())
    {
        for (usize i = 0; i < v4.result.endpoints.Count(); ++i)
        {
            CHECK(v4.result.endpoints[i].GetAddress().IsV4());
        }
    }
    // `.invalid` never resolves (RFC 6761); a sandbox with no name server may answer with a timeout or another failure
    // instead.
    WaitOptions limit;
    limit.timeout_nanos = 3'000'000'000;
    ResolveOutcome missing;
    scheduler->Submit(
        ResolveTestJob(&reactor, &resolver, "no-such-host.invalid", 80, AddressFamily::None, limit, &missing));
    ResolveTestWait(*scheduler, missing);
    CHECK(missing.result.status.Failed());
    CHECK(missing.result.endpoints.Empty());
}

TEST_CASE("A flood of results is cut to the capacity of the set, and a success with nothing is NotFound",
          "[network][resolve]")
{
    auto scheduler = ResolveTestScheduler(2);
    Reactor reactor(*scheduler);
    {
        Resolver resolver(&ResolveTestFlood);
        ResolveOutcome out;
        scheduler->Submit(ResolveTestJob(&reactor, &resolver, "flood.example", 9, AddressFamily::None, {}, &out));
        ResolveTestWait(*scheduler, out);
        CHECK(out.result.status.Ok());
        CHECK(out.result.endpoints.Count() == EndpointSet::kCapacity);
    }
    {
        Resolver resolver(&ResolveTestEmpty);
        ResolveOutcome out;
        scheduler->Submit(ResolveTestJob(&reactor, &resolver, "nothing.example", 9, AddressFamily::None, {}, &out));
        ResolveTestWait(*scheduler, out);
        CHECK(out.result.status.Is(ErrorCode::NotFound));
    }
}

TEST_CASE("A slow resolution is abandoned on timeout or cancel, and its answer is dropped safely", "[network][resolve]")
{
    auto scheduler = ResolveTestScheduler(2);
    Reactor reactor(*scheduler);
    Resolver resolver(&ResolveTestSlow);
    WaitOptions limit;
    limit.timeout_nanos = 30'000'000;
    ResolveOutcome timed;
    scheduler->Submit(ResolveTestJob(&reactor, &resolver, "slow.example", 1, AddressFamily::None, limit, &timed));
    ResolveTestWait(*scheduler, timed);
    CHECK(timed.result.status.Is(ErrorCode::TimedOut));
    CHECK(timed.elapsed_nanos < 150'000'000);  // it did not wait for the 200 ms resolution

    CancelToken token;
    WaitOptions cancellable;
    cancellable.token = &token;
    ResolveOutcome cancelled;
    scheduler->Submit(
        ResolveTestJob(&reactor, &resolver, "slow.example", 1, AddressFamily::None, cancellable, &cancelled));
    ThisThread::SleepForMilliseconds(20);
    token.Cancel();
    ResolveTestWait(*scheduler, cancelled);
    CHECK(cancelled.result.status.Is(ErrorCode::Cancelled));
    // The abandoned resolutions finish on the thread and fire signals nobody waits for; shutting down waits for them.
    resolver.Shutdown();
}

namespace
{

Job ResolveTestEmbeddedZero(Reactor* reactor, Resolver* resolver, ResolveOutcome* out)
{
    out->result = co_await Resolve(*reactor, *resolver, StringView("internal\0.example.com", 21), 80);
    out->done.FetchAdd(1, MemoryOrder::Release);
}

Atomic<int> g_slow_calls{0};

Status ResolveTestSlowCounting(StringView, u16 port, AddressFamily, EndpointSet* out)
{
    g_slow_calls.FetchAdd(1);
    ThisThread::SleepForMilliseconds(
        400);  // long enough that a stalled hosted runner still has it running when the others time out
    out->Add(Endpoint(Address::V4(10, 0, 0, 3), port));
    return Status::Success();
}

}  // namespace

TEST_CASE("A name with a zero byte is refused, and abandoned requests that have not started are skipped",
          "[network][resolve]")
{
    auto scheduler = ResolveTestScheduler(2);
    Reactor reactor(*scheduler);
    {
        Resolver resolver(&ResolveTestCounting);
        g_resolver_calls.Store(0);
        ResolveOutcome out;
        scheduler->Submit(ResolveTestEmbeddedZero(&reactor, &resolver, &out));
        ResolveTestWait(*scheduler, out);
        CHECK(out.result.status.Is(ErrorCode::Invalid));
        CHECK(g_resolver_calls.Load() == 0);
    }
    // Four lookups are queued behind a slow one; the last three time out and are abandoned, so the thread does not run
    // them.
    Resolver resolver(&ResolveTestSlowCounting);
    g_slow_calls.Store(0);
    ResolveOutcome first;
    scheduler->Submit(ResolveTestJob(&reactor, &resolver, "first.example", 1, AddressFamily::None, {}, &first));
    ThisThread::SleepForMilliseconds(30);
    WaitOptions limit;
    limit.timeout_nanos = 20'000'000;
    ResolveOutcome abandoned[3];
    for (ResolveOutcome& a : abandoned)
    {
        scheduler->Submit(ResolveTestJob(&reactor, &resolver, "later.example", 1, AddressFamily::None, limit, &a));
    }
    for (ResolveOutcome& a : abandoned)
    {
        ResolveTestWait(*scheduler, a);
        CHECK(a.result.status.Is(ErrorCode::TimedOut));
    }
    ResolveTestWait(*scheduler, first);
    CHECK(first.result.status.Ok());
    ThisThread::SleepForMilliseconds(
        1300);  // time enough for the thread to have run all three, had it not skipped them
    resolver.Shutdown();
    CHECK(g_slow_calls.Load() == 1);
}

TEST_CASE("The resolver's queue is bounded, and shutdown ends the queued requests", "[network][resolve]")
{
    auto scheduler = ResolveTestScheduler(2);
    Reactor reactor(*scheduler);
    Resolver resolver(&ResolveTestSlow, 1);  // one waiting request besides the one in progress
    ResolveOutcome first;
    ResolveOutcome second;
    ResolveOutcome third;
    scheduler->Submit(ResolveTestJob(&reactor, &resolver, "a.example", 1, AddressFamily::None, {}, &first));
    ThisThread::SleepForMilliseconds(40);  // the thread has taken the first
    scheduler->Submit(ResolveTestJob(&reactor, &resolver, "b.example", 1, AddressFamily::None, {}, &second));
    ThisThread::SleepForMilliseconds(20);  // queued
    scheduler->Submit(ResolveTestJob(&reactor, &resolver, "c.example", 1, AddressFamily::None, {}, &third));
    ResolveTestWait(*scheduler, third);
    CHECK(third.result.status.Is(ErrorCode::LimitExceeded));
    resolver.Shutdown();  // waits for the first, ends the second
    ResolveTestWait(*scheduler, first);
    ResolveTestWait(*scheduler, second);
    CHECK(first.result.status.Ok());
    CHECK(second.result.status.Is(ErrorCode::Cancelled));
    ResolveOutcome late;
    scheduler->Submit(ResolveTestJob(&reactor, &resolver, "d.example", 1, AddressFamily::None, {}, &late));
    ResolveTestWait(*scheduler, late);
    CHECK(late.result.status.Is(ErrorCode::Cancelled));
}

namespace
{

struct ConnectOutcome
{
    IoResult result;
    Atomic<int> done{0};
};

Job ResolveTestConnectAny(Reactor* reactor, const EndpointSet* endpoints, Socket* socket, ConnectOptions options,
                          ConnectOutcome* out)
{
    out->result = co_await ConnectAny(*reactor, *endpoints, socket, options);
    out->done.FetchAdd(1, MemoryOrder::Release);
}

Job ResolveTestConnectTo(Reactor* reactor, Resolver* resolver, const char* host, u16 port, Socket* socket,
                         ConnectOutcome* out)
{
    out->result = co_await ConnectTo(*reactor, *resolver, StringView(host), port, socket);
    out->done.FetchAdd(1, MemoryOrder::Release);
}

void ResolveTestWaitConnect(Scheduler& scheduler, ConnectOutcome& out)
{
    struct Probe
    {
        ConnectOutcome* out;
    } probe{&out};
    scheduler.HelpUntil([](void* u) { return static_cast<Probe*>(u)->out->done.Load(MemoryOrder::Acquire) != 0; },
                        &probe);
}

}  // namespace

TEST_CASE("ConnectAny skips a refusing endpoint and connects to the next; ConnectTo resolves first",
          "[network][resolve]")
{
    auto scheduler = ResolveTestScheduler(2);
    Reactor reactor(*scheduler);
    Resolver resolver;
    Listener listener;
    if (listener.Open(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Endpoint closed_port;
    {
        Socket temp;
        REQUIRE(Socket::OpenTcp(AddressFamily::V4, &temp).Ok());
        REQUIRE(temp.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
        REQUIRE(temp.LocalEndpoint(&closed_port).Ok());
    }
    EndpointSet candidates;
    candidates.Add(closed_port);
    candidates.Add(listener.LocalEndpoint());
    Socket client;
    ConnectOutcome out;
    scheduler->Submit(ResolveTestConnectAny(&reactor, &candidates, &client, {}, &out));
    ResolveTestWaitConnect(*scheduler, out);
    CHECK(out.result.status.Ok());
    CHECK(client.IsOpen());
    Endpoint peer;
    REQUIRE(client.PeerEndpoint(&peer).Ok());
    CHECK(peer == listener.LocalEndpoint());

    // Only refusing endpoints: the last failure comes back, and nothing is left open.
    EndpointSet refusing;
    refusing.Add(closed_port);
    Socket none;
    ConnectOutcome refused;
    scheduler->Submit(ResolveTestConnectAny(&reactor, &refusing, &none, {}, &refused));
    ResolveTestWaitConnect(*scheduler, refused);
    CHECK(refused.result.status.Is(ErrorCode::Refused));
    CHECK_FALSE(none.IsOpen());

    // An empty set is refused; a cancelled token ends the loop.
    EndpointSet empty;
    ConnectOutcome invalid;
    scheduler->Submit(ResolveTestConnectAny(&reactor, &empty, &none, {}, &invalid));
    ResolveTestWaitConnect(*scheduler, invalid);
    CHECK(invalid.result.status.Is(ErrorCode::Invalid));
    CancelToken token;
    token.Cancel();
    ConnectOptions cancelled_options;
    cancelled_options.token = &token;
    ConnectOutcome cancelled;
    scheduler->Submit(ResolveTestConnectAny(&reactor, &candidates, &none, cancelled_options, &cancelled));
    ResolveTestWaitConnect(*scheduler, cancelled);
    CHECK(cancelled.result.status.Is(ErrorCode::Cancelled));

    // ConnectTo with a numeric host and with `localhost`.
    Socket via_address;
    ConnectOutcome by_address;
    scheduler->Submit(ResolveTestConnectTo(&reactor, &resolver, "127.0.0.1", listener.LocalEndpoint().Port(),
                                           &via_address, &by_address));
    ResolveTestWaitConnect(*scheduler, by_address);
    CHECK(by_address.result.status.Ok());
    Socket via_name;
    ConnectOutcome by_name;
    scheduler->Submit(
        ResolveTestConnectTo(&reactor, &resolver, "localhost", listener.LocalEndpoint().Port(), &via_name, &by_name));
    ResolveTestWaitConnect(*scheduler, by_name);
    // `localhost` may list ::1 first, where this listener is not: the next endpoint is tried, so it connects either
    // way.
    if (by_name.result.status.Failed())
    {
        SKIP("this machine does not resolve localhost: " << ErrorCodeName(by_name.result.status.Code()));
    }
    CHECK(via_name.IsOpen());
}

}  // namespace vse::net
