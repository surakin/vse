// The reactor and the asynchronous operations (Phase 20 Day 269): readiness, timers, cancellation, close and shutdown
// races, and a thousand loopback connections driven by coroutines. Real sockets: the executable is labelled `network`.

#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "net/async.h"
#include "net/clock.h"
#include "net/reactor.h"
#include "os/os.h"
#include "sync/atomic.h"
#include "sync/thread.h"

namespace vse::net
{
namespace
{

using jobs::Job;
using jobs::Scheduler;

UniquePtr<Scheduler> ReactorTestScheduler(usize participants)
{
    Scheduler::Config cfg;
    cfg.participants = participants;
    return MakeUnique<Scheduler>(cfg);
}

// Runs jobs on the calling thread (participant 0) until `done` reaches `target`.
struct DoneCounter
{
    Atomic<int> done{0};
};

void HelpUntilDone(Scheduler& scheduler, DoneCounter& counter, int target)
{
    struct Probe
    {
        DoneCounter* counter;
        int target;
    } probe{&counter, target};
    scheduler.HelpUntil(
        [](void* u)
        { return static_cast<Probe*>(u)->counter->done.Load(MemoryOrder::Acquire) >= static_cast<Probe*>(u)->target; },
        &probe);
}

// A connected loopback TCP pair, set up synchronously (no reactor yet). Returns false where loopback is unavailable.
struct TcpPair
{
    Socket listener;
    Socket client;
    Socket server;
};

bool MakePair(TcpPair* pair)
{
    if (Socket::OpenTcp(AddressFamily::V4, &pair->listener).Failed() ||
        pair->listener.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed() ||
        pair->listener.Listen(1024).Failed())
    {
        return false;
    }
    Endpoint at;
    if (pair->listener.LocalEndpoint(&at).Failed() || Socket::OpenTcp(AddressFamily::V4, &pair->client).Failed())
    {
        return false;
    }
    const Status c = pair->client.Connect(at);
    if (c.Failed() && !c.Is(ErrorCode::WouldBlock))
    {
        return false;
    }
    Endpoint peer;
    for (int i = 0; i < 20000; ++i)
    {
        const Status a = pair->listener.Accept(&pair->server, &peer);
        if (a.Ok())
        {
            return true;
        }
        if (!a.Is(ErrorCode::WouldBlock))
        {
            return false;
        }
        ThisThread::SleepForMicroseconds(100);
    }
    return false;
}

#define REQUIRE_REACTOR(reactor)                                                                                       \
    if (!(reactor).IsRunning())                                                                                        \
    {                                                                                                                  \
        SKIP("the reactor could not start here (no poller)");                                                          \
    }                                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (false)

ByteSpan ReactorTestBytes(const char* text)
{
    return ByteSpan(reinterpret_cast<const byte*>(text), StringView(text).Length());
}

// ---- jobs used by the tests; they take everything by pointer, as jobs must ----

struct WaitOutcome
{
    Status status;
    bool on_participant = false;
    bool on_reactor_thread = true;
    u64 elapsed_nanos = 0;
    Atomic<int> done{0};
};

Job ReactorTestWaitReadable(Reactor* reactor, Socket* socket, WaitOptions options, WaitOutcome* out)
{
    const u64 start = reactor->GetClock().NowNanos();
    out->status = co_await reactor->WaitReadable(*socket, options);
    out->elapsed_nanos = reactor->GetClock().NowNanos() - start;
    out->on_participant = jobs::WorkerPool::IsParticipantThread();
    out->on_reactor_thread = ThisThread::Id() == reactor->GetThreadId();
    out->done.FetchAdd(1, MemoryOrder::Release);
}

Job ReactorTestWaitWritable(Reactor* reactor, Socket* socket, WaitOutcome* out)
{
    out->status = co_await reactor->WaitWritable(*socket, {});
    out->on_participant = jobs::WorkerPool::IsParticipantThread();
    out->done.FetchAdd(1, MemoryOrder::Release);
}

Job ReactorTestSleep(Reactor* reactor, u64 nanos, WaitOptions options, WaitOutcome* out)
{
    const u64 start = reactor->GetClock().NowNanos();
    out->status = co_await reactor->Sleep(nanos, options);
    out->elapsed_nanos = reactor->GetClock().NowNanos() - start;
    out->on_participant = jobs::WorkerPool::IsParticipantThread();
    out->on_reactor_thread = ThisThread::Id() == reactor->GetThreadId();
    out->done.FetchAdd(1, MemoryOrder::Release);
}

bool WaitDone(Scheduler& scheduler, WaitOutcome& out)
{
    struct Probe
    {
        WaitOutcome* out;
    } probe{&out};
    scheduler.HelpUntil([](void* u) { return static_cast<Probe*>(u)->out->done.Load(MemoryOrder::Acquire) != 0; },
                        &probe);
    return true;
}

bool NotDoneYet(const WaitOutcome& out)
{
    return out.done.Load(MemoryOrder::Acquire) == 0;
}

}  // namespace

TEST_CASE("A wait for readable ends when data arrives, and the coroutine resumes on a participant",
          "[network][reactor]")
{
    for (usize participants : {usize{1}, usize{3}})
    {
        auto scheduler = ReactorTestScheduler(participants);
        Reactor reactor(*scheduler);
        REQUIRE_REACTOR(reactor);
        TcpPair pair;
        if (!MakePair(&pair))
        {
            SKIP("loopback sockets are unavailable here");
        }
        WaitOutcome out;
        scheduler->Submit(ReactorTestWaitReadable(&reactor, &pair.server, {}, &out));
        ThisThread::SleepForMilliseconds(30);
        CHECK(NotDoneYet(out));  // nothing to read: the coroutine stays suspended
        usize sent = 0;
        REQUIRE(pair.client.Send(ReactorTestBytes("x"), &sent).Ok());
        WaitDone(*scheduler, out);
        CHECK(out.status.Ok());
        CHECK(out.on_participant);
        CHECK_FALSE(out.on_reactor_thread);
    }
}

TEST_CASE("A socket that is already ready ends the wait at once (the check is by level)", "[network][reactor]")
{
    auto scheduler = ReactorTestScheduler(2);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    TcpPair pair;
    if (!MakePair(&pair))
    {
        SKIP("loopback sockets are unavailable here");
    }
    usize sent = 0;
    REQUIRE(pair.client.Send(ReactorTestBytes("already here"), &sent).Ok());
    ThisThread::SleepForMilliseconds(20);  // the data is in the server's buffer before anyone waits
    WaitOutcome readable;
    scheduler->Submit(ReactorTestWaitReadable(&reactor, &pair.server, {}, &readable));
    WaitDone(*scheduler, readable);
    CHECK(readable.status.Ok());
    WaitOutcome writable;
    scheduler->Submit(ReactorTestWaitWritable(&reactor, &pair.client, &writable));
    WaitDone(*scheduler, writable);
    CHECK(writable.status.Ok());
    CHECK(writable.on_participant);
}

TEST_CASE("Sleep and timeouts end after the time and never before", "[network][reactor]")
{
    auto scheduler = ReactorTestScheduler(2);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    WaitOutcome slept;
    scheduler->Submit(ReactorTestSleep(&reactor, 30'000'000, {}, &slept));
    WaitDone(*scheduler, slept);
    CHECK(slept.status.Ok());
    CHECK(slept.elapsed_nanos >= 30'000'000);
    CHECK(slept.elapsed_nanos < 2'000'000'000);
    CHECK(slept.on_participant);
    CHECK_FALSE(slept.on_reactor_thread);

    TcpPair pair;
    if (!MakePair(&pair))
    {
        SKIP("loopback sockets are unavailable here");
    }
    WaitOptions options;
    options.timeout_nanos = 25'000'000;
    WaitOutcome timed;
    scheduler->Submit(ReactorTestWaitReadable(&reactor, &pair.server, options, &timed));
    WaitDone(*scheduler, timed);
    CHECK(timed.status.Is(ErrorCode::TimedOut));
    CHECK(timed.elapsed_nanos >= 25'000'000);
    CHECK(timed.elapsed_nanos < 2'000'000'000);
    // Timeouts of many waits fire in order of their deadlines.
    WaitOutcome order[5];
    for (int i = 4; i >= 0; --i)
    {
        scheduler->Submit(ReactorTestSleep(&reactor, static_cast<u64>(10 + 15 * i) * 1'000'000ull, {}, &order[i]));
    }
    for (int i = 0; i < 5; ++i)
    {
        WaitDone(*scheduler, order[i]);
        CHECK(order[i].status.Ok());
        CHECK(order[i].elapsed_nanos >= static_cast<u64>(10 + 15 * i) * 1'000'000ull);
    }
}

TEST_CASE("A manual clock decides when a sleep ends", "[network][reactor]")
{
    auto scheduler = ReactorTestScheduler(2);
    ManualClock clock;
    Reactor reactor(*scheduler, &clock);
    REQUIRE_REACTOR(reactor);
    WaitOutcome out;
    scheduler->Submit(ReactorTestSleep(&reactor, 1'000'000'000, {}, &out));
    ThisThread::SleepForMilliseconds(30);
    CHECK(NotDoneYet(out));
    clock.Advance(999'000'000);
    reactor.Poke();
    ThisThread::SleepForMilliseconds(30);
    CHECK(NotDoneYet(out));  // one millisecond short
    clock.Advance(1'000'000);
    reactor.Poke();
    WaitDone(*scheduler, out);
    CHECK(out.status.Ok());
    CHECK(out.elapsed_nanos == 1'000'000'000);
}

TEST_CASE("Cancel ends a wait once, before it starts or while it waits", "[network][reactor]")
{
    auto scheduler = ReactorTestScheduler(2);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    TcpPair pair;
    if (!MakePair(&pair))
    {
        SKIP("loopback sockets are unavailable here");
    }
    CancelToken during;
    WaitOptions options;
    options.token = &during;
    WaitOutcome waiting;
    scheduler->Submit(ReactorTestWaitReadable(&reactor, &pair.server, options, &waiting));
    ThisThread::SleepForMilliseconds(20);
    CHECK(NotDoneYet(waiting));
    during.Cancel();
    WaitDone(*scheduler, waiting);
    CHECK(waiting.status.Is(ErrorCode::Cancelled));
    during.Cancel();  // idempotent

    CancelToken before;
    before.Cancel();
    options.token = &before;
    WaitOutcome immediate;
    scheduler->Submit(ReactorTestWaitReadable(&reactor, &pair.server, options, &immediate));
    WaitDone(*scheduler, immediate);
    CHECK(immediate.status.Is(ErrorCode::Cancelled));

    // A token that is never cancelled changes nothing.
    CancelToken idle;
    options.token = &idle;
    options.timeout_nanos = 20'000'000;
    WaitOutcome timed;
    scheduler->Submit(ReactorTestWaitReadable(&reactor, &pair.server, options, &timed));
    WaitDone(*scheduler, timed);
    CHECK(timed.status.Is(ErrorCode::TimedOut));
}

TEST_CASE("A cancel storm ends 500 waits, each once", "[network][reactor]")
{
    auto scheduler = ReactorTestScheduler(4);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    constexpr int kWaits = 500;
    Vector<Socket> sockets;
    sockets.Resize(kWaits);
    for (int i = 0; i < kWaits; ++i)
    {
        if (Socket::OpenUdp(AddressFamily::V4, &sockets[static_cast<usize>(i)]).Failed())
        {
            SKIP("cannot open that many sockets here");
        }
    }
    CancelToken token;
    WaitOptions options;
    options.token = &token;
    Vector<WaitOutcome> outcomes;
    outcomes.Resize(kWaits);
    for (int i = 0; i < kWaits; ++i)
    {
        scheduler->Submit(ReactorTestWaitReadable(&reactor, &sockets[static_cast<usize>(i)], options,
                                                  &outcomes[static_cast<usize>(i)]));
    }
    ThisThread::SleepForMilliseconds(50);
    token.Cancel();
    for (int i = 0; i < kWaits; ++i)
    {
        WaitDone(*scheduler, outcomes[static_cast<usize>(i)]);
        CHECK(outcomes[static_cast<usize>(i)].status.Is(ErrorCode::Cancelled));
        CHECK(outcomes[static_cast<usize>(i)].done.Load(MemoryOrder::Acquire) == 1);
    }
}

TEST_CASE("Closing a socket through the reactor ends the waits on it with Closed", "[network][reactor]")
{
    auto scheduler = ReactorTestScheduler(2);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    TcpPair pair;
    if (!MakePair(&pair))
    {
        SKIP("loopback sockets are unavailable here");
    }
    WaitOutcome out;
    scheduler->Submit(ReactorTestWaitReadable(&reactor, &pair.server, {}, &out));
    ThisThread::SleepForMilliseconds(20);
    CHECK(NotDoneYet(out));
    CHECK(reactor.Close(pair.server).Ok());
    WaitDone(*scheduler, out);
    CHECK(out.status.Is(ErrorCode::Closed));
    CHECK_FALSE(pair.server.IsOpen());
    // A wait on a socket that is not open ends at once.
    WaitOutcome closed;
    scheduler->Submit(ReactorTestWaitReadable(&reactor, &pair.server, {}, &closed));
    WaitDone(*scheduler, closed);
    CHECK(closed.status.Is(ErrorCode::Invalid));
}

TEST_CASE("A second coroutine waiting in the same direction on a socket is refused", "[network][reactor]")
{
    auto scheduler = ReactorTestScheduler(2);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    TcpPair pair;
    if (!MakePair(&pair))
    {
        SKIP("loopback sockets are unavailable here");
    }
    WaitOutcome first;
    WaitOutcome second;
    scheduler->Submit(ReactorTestWaitReadable(&reactor, &pair.server, {}, &first));
    ThisThread::SleepForMilliseconds(20);
    scheduler->Submit(ReactorTestWaitReadable(&reactor, &pair.server, {}, &second));
    WaitDone(*scheduler, second);
    CHECK(second.status.Is(ErrorCode::Invalid));
    CHECK(NotDoneYet(first));  // the first wait is untouched
    // A reader and a writer on the same socket are fine together.
    WaitOutcome writer;
    scheduler->Submit(ReactorTestWaitWritable(&reactor, &pair.server, &writer));
    WaitDone(*scheduler, writer);
    CHECK(writer.status.Ok());
    CHECK(NotDoneYet(first));
    usize sent = 0;
    REQUIRE(pair.client.Send(ReactorTestBytes("go"), &sent).Ok());
    WaitDone(*scheduler, first);
    CHECK(first.status.Ok());
}

TEST_CASE("Shutdown ends 500 waits with Cancelled, and later waits end at once", "[network][reactor]")
{
    auto scheduler = ReactorTestScheduler(4);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    constexpr int kWaits = 500;
    Vector<WaitOutcome> outcomes;
    outcomes.Resize(kWaits);
    for (int i = 0; i < kWaits; ++i)
    {
        scheduler->Submit(ReactorTestSleep(&reactor, 3'600'000'000'000ull, {}, &outcomes[static_cast<usize>(i)]));
    }
    ThisThread::SleepForMilliseconds(50);
    reactor.Shutdown();
    CHECK_FALSE(reactor.IsRunning());
    for (int i = 0; i < kWaits; ++i)
    {
        WaitDone(*scheduler, outcomes[static_cast<usize>(i)]);
        CHECK(outcomes[static_cast<usize>(i)].status.Is(ErrorCode::Cancelled));
    }
    WaitOutcome late;
    scheduler->Submit(ReactorTestSleep(&reactor, 1'000'000, {}, &late));
    WaitDone(*scheduler, late);
    CHECK(late.status.Is(ErrorCode::Cancelled));
    reactor.Shutdown();  // idempotent
}

TEST_CASE("A token cancelled after its reactor is gone is harmless, and a token cancels only its own waits",
          "[network][reactor]")
{
    auto scheduler = ReactorTestScheduler(2);
    CancelToken outlives;
    {
        Reactor reactor(*scheduler);
        REQUIRE_REACTOR(reactor);
        WaitOptions options;
        options.token = &outlives;
        WaitOutcome out;
        scheduler->Submit(ReactorTestSleep(&reactor, 10'000'000, options, &out));
        WaitDone(*scheduler, out);
        CHECK(out.status.Ok());
        // Two tokens, three waits: cancelling one leaves the other's wait alone.
        CancelToken a;
        CancelToken b;
        WaitOptions oa;
        oa.token = &a;
        WaitOptions ob;
        ob.token = &b;
        WaitOutcome a1;
        WaitOutcome a2;
        WaitOutcome b1;
        scheduler->Submit(ReactorTestSleep(&reactor, 3'600'000'000'000ull, oa, &a1));
        scheduler->Submit(ReactorTestSleep(&reactor, 3'600'000'000'000ull, ob, &b1));
        scheduler->Submit(ReactorTestSleep(&reactor, 3'600'000'000'000ull, oa, &a2));
        ThisThread::SleepForMilliseconds(30);
        a.Cancel();
        WaitDone(*scheduler, a1);
        WaitDone(*scheduler, a2);
        CHECK(a1.status.Is(ErrorCode::Cancelled));
        CHECK(a2.status.Is(ErrorCode::Cancelled));
        ThisThread::SleepForMilliseconds(20);
        CHECK(NotDoneYet(b1));
        b.Cancel();
        WaitDone(*scheduler, b1);
        CHECK(b1.status.Is(ErrorCode::Cancelled));
    }  // the reactor is destroyed here
    outlives.Cancel();  // its waits ended long ago: the token holds no pointer to the destroyed reactor
    CHECK(outlives.IsCancelled());
}

namespace
{

Job ReactorTestWaitSignal(Reactor* reactor, Signal* signal, WaitOptions options, WaitOutcome* out)
{
    out->status = co_await reactor->WaitSignal(*signal, options);
    out->on_participant = jobs::WorkerPool::IsParticipantThread();
    out->done.FetchAdd(1, MemoryOrder::Release);
}

}  // namespace

TEST_CASE(
    "A signal fired from another thread ends the wait; before the wait it ends at once; a timeout and a cancel still apply",
    "[network][reactor][signal]")
{
    auto scheduler = ReactorTestScheduler(2);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    Signal later;
    WaitOutcome waiting;
    scheduler->Submit(ReactorTestWaitSignal(&reactor, &later, {}, &waiting));
    ThisThread::SleepForMilliseconds(30);
    CHECK(NotDoneYet(waiting));
    Thread firer([&later] { later.Fire(); });
    WaitDone(*scheduler, waiting);
    firer.Join();
    CHECK(waiting.status.Ok());
    CHECK(waiting.on_participant);
    later.Fire();  // idempotent, and harmless after the wait ended

    Signal early;
    early.Fire();
    WaitOutcome immediate;
    scheduler->Submit(ReactorTestWaitSignal(&reactor, &early, {}, &immediate));
    WaitDone(*scheduler, immediate);
    CHECK(immediate.status.Ok());

    Signal never;
    WaitOptions limit;
    limit.timeout_nanos = 20'000'000;
    WaitOutcome timed;
    scheduler->Submit(ReactorTestWaitSignal(&reactor, &never, limit, &timed));
    WaitDone(*scheduler, timed);
    CHECK(timed.status.Is(ErrorCode::TimedOut));
    never.Fire();  // after the timeout: no wait, no pointer to the reactor, nothing happens

    CancelToken token;
    WaitOptions cancellable;
    cancellable.token = &token;
    Signal other;
    WaitOutcome cancelled;
    scheduler->Submit(ReactorTestWaitSignal(&reactor, &other, cancellable, &cancelled));
    ThisThread::SleepForMilliseconds(20);
    token.Cancel();
    WaitDone(*scheduler, cancelled);
    CHECK(cancelled.status.Is(ErrorCode::Cancelled));

    // One waiter per signal.
    Signal shared;
    WaitOutcome first;
    WaitOutcome second;
    scheduler->Submit(ReactorTestWaitSignal(&reactor, &shared, {}, &first));
    ThisThread::SleepForMilliseconds(20);
    scheduler->Submit(ReactorTestWaitSignal(&reactor, &shared, {}, &second));
    WaitDone(*scheduler, second);
    CHECK(second.status.Is(ErrorCode::Invalid));
    shared.Fire();
    WaitDone(*scheduler, first);
    CHECK(first.status.Ok());
}

TEST_CASE("Firing a signal races with the wait starting without losing the wake-up",
          "[network][reactor][signal][stress]")
{
    auto scheduler = ReactorTestScheduler(4);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    constexpr int kRounds = 2000;
    for (int round = 0; round < kRounds; ++round)
    {
        Signal signal;
        WaitOutcome out;
        Thread firer(
            [&signal, round]
            {
                ThisThread::SleepForMicroseconds(static_cast<u64>(round % 40));
                signal.Fire();
            });
        scheduler->Submit(ReactorTestWaitSignal(&reactor, &signal, {}, &out));
        WaitDone(*scheduler, out);
        firer.Join();
        REQUIRE(out.status.Ok());
    }
}

TEST_CASE("An enormous sleep does not end at once", "[network][reactor]")
{
    auto scheduler = ReactorTestScheduler(2);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    WaitOutcome out;
    scheduler->Submit(
        ReactorTestSleep(&reactor, ~u64{0}, {}, &out));  // 2^64 - 1 ns would read as a negative timeout if cast
    ThisThread::SleepForMilliseconds(50);
    CHECK(NotDoneYet(out));
    reactor.Shutdown();
    WaitDone(*scheduler, out);
    CHECK(out.status.Is(ErrorCode::Cancelled));
}

TEST_CASE("The peer's half-close does not wake a coroutine that waits to write", "[network][reactor]")
{
    auto scheduler = ReactorTestScheduler(2);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    TcpPair pair;
    if (!MakePair(&pair))
    {
        SKIP("loopback sockets are unavailable here");
    }
    // Fill the client's send path until the socket says it is full, and stays full: macOS's loopback moves the bytes
    // to the peer after the send returns, so room appears a moment later (a real readiness, reported by kqueue), and
    // the path is full only when a pause adds no room (Phase 21 Day 310).
    Vector<byte> chunk;
    chunk.Resize(usize{1} << 16);
    auto fill = [&]
    {
        for (int round = 0; round < 50; ++round)
        {
            usize took = 0;
            Status s = Status::Success();
            for (int i = 0; i < 4096 && s.Ok(); ++i)
            {
                usize sent = 0;
                s = pair.client.Send(ByteSpan(chunk.Data(), chunk.Size()), &sent);
                took += sent;
            }
            REQUIRE(s.Is(ErrorCode::WouldBlock));
            if (took == 0 && round > 0)
            {
                return;
            }
            ThisThread::SleepForMilliseconds(20);
        }
    };
    // A writer that waits on the full socket. One that ends at once found room the fill did not see: it has done
    // nothing wrong, so fill again and wait again, until one waits (the test then tests something).
    Array<WaitOutcome, 10> attempts;
    WaitOutcome* writer = nullptr;
    for (usize a = 0; a < attempts.Size(); ++a)
    {
        fill();
        writer = &attempts[a];
        scheduler->Submit(ReactorTestWaitWritable(&reactor, &pair.client, writer));
        ThisThread::SleepForMilliseconds(30);
        if (NotDoneYet(*writer))
        {
            break;
        }
        WaitDone(*scheduler, *writer);
    }
    REQUIRE(NotDoneYet(*writer));  // a writer waits on a full socket
    WaitOutcome reader;
    scheduler->Submit(ReactorTestWaitReadable(&reactor, &pair.client, {},
                                              &reader));  // a reader too: the registration asks for read-side hangup
    // After the half-close a writer that ended must have had room (late room from the fill is a real readiness); one
    // woken by the half-close itself finds the socket full.
    auto writer_waits_or_had_room = [&]
    {
        if (NotDoneYet(*writer))
        {
            return true;
        }
        usize sent = 0;
        const byte one[1] = {byte{1}};
        return pair.client.Send(ByteSpan(one, 1), &sent).Ok() && sent == 1;
    };
    ThisThread::SleepForMilliseconds(30);
    CHECK(NotDoneYet(reader));
    // The server shuts down its writing half: the client's socket gets the end of the stream (a read-side event). The
    // reader ends (it can read the end); the writer, whose buffer is still full, must not.
    REQUIRE(pair.server.Shutdown(ShutdownMode::Write).Ok());
    WaitDone(*scheduler, reader);
    CHECK(reader.status.Ok());
    ThisThread::SleepForMilliseconds(60);
    CHECK(writer_waits_or_had_room());
    // Draining the server's receive side makes room: now the writer ends.
    for (int i = 0; i < 20000 && NotDoneYet(*writer); ++i)
    {
        usize got = 0;
        while (pair.server.Receive(MutableByteSpan(chunk.Data(), chunk.Size()), &got).Ok() && got != 0)
        {
        }
        ThisThread::SleepForMicroseconds(200);
    }
    WaitDone(*scheduler, *writer);
    CHECK(writer->status.Ok());
}

namespace
{

// One cancel-versus-data race: the wait ends exactly once, with Ok or Cancelled, whichever the reactor decides.
Job ReactorTestRaceJob(Reactor* reactor, Socket* socket, CancelToken* token, Atomic<int>* ok, Atomic<int>* cancelled,
                       Atomic<int>* other, DoneCounter* done)
{
    WaitOptions options;
    options.token = token;
    const Status s = co_await reactor->WaitReadable(*socket, options);
    if (s.Ok())
    {
        ok->FetchAdd(1, MemoryOrder::Relaxed);
    }
    else if (s.Is(ErrorCode::Cancelled))
    {
        cancelled->FetchAdd(1, MemoryOrder::Relaxed);
    }
    else
    {
        other->FetchAdd(1, MemoryOrder::Relaxed);
    }
    done->done.FetchAdd(1, MemoryOrder::Release);
}

}  // namespace

TEST_CASE("Register, cancel and complete race without losing or doubling a wake-up", "[network][reactor][stress]")
{
    auto scheduler = ReactorTestScheduler(4);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    constexpr int kRounds = 1500;
    Atomic<int> ok{0};
    Atomic<int> cancelled{0};
    Atomic<int> other{0};
    DoneCounter done;
    TcpPair pair;
    if (!MakePair(&pair))
    {
        SKIP("loopback sockets are unavailable here");
    }
    for (int round = 0; round < kRounds; ++round)
    {
        CancelToken token;
        scheduler->Submit(ReactorTestRaceJob(&reactor, &pair.server, &token, &ok, &cancelled, &other, &done));
        // The two events race with the registration and with each other; the order varies with the round.
        usize sent = 0;
        if (round % 3 == 0)
        {
            token.Cancel();
            [[maybe_unused]] const Status sent_status = pair.client.Send(ReactorTestBytes("d"), &sent);
        }
        else if (round % 3 == 1)
        {
            [[maybe_unused]] const Status sent_status = pair.client.Send(ReactorTestBytes("d"), &sent);
            token.Cancel();
        }
        else
        {
            ThisThread::SleepForMicroseconds(static_cast<u64>(round % 50));
            token.Cancel();
        }
        HelpUntilDone(*scheduler, done, round + 1);
        // Take the byte away, if one arrived, so the next round starts with an idle socket.
        byte sink[16];
        usize got = 0;
        while (pair.server.Receive(MutableByteSpan(sink, sizeof sink), &got).Ok() && got != 0)
        {
        }
    }
    CHECK(ok.Load() + cancelled.Load() == kRounds);
    CHECK(other.Load() == 0);
    CHECK(cancelled.Load() > 0);
}

namespace
{

// The server side of one connection: echoes `rounds` messages of kMessage bytes.
constexpr usize kReactorMessage = 16;

Job ReactorTestEchoServer(Reactor* reactor, Socket* socket, int rounds, Atomic<int>* failures, DoneCounter* done)
{
    byte message[kReactorMessage];
    for (int round = 0; round < rounds; ++round)
    {
        usize have = 0;
        while (have < kReactorMessage)
        {
            const IoResult r = co_await Receive(*reactor, *socket,
                                                MutableByteSpan(message + have, kReactorMessage - have));
            if (r.status.Failed())
            {
                failures->FetchAdd(1);
                done->done.FetchAdd(1, MemoryOrder::Release);
                co_return;
            }
            have += r.count;
        }
        const IoResult sent = co_await Send(*reactor, *socket, ByteSpan(message, kReactorMessage));
        if (sent.status.Failed() || sent.count != kReactorMessage)
        {
            failures->FetchAdd(1);
            break;
        }
    }
    done->done.FetchAdd(1, MemoryOrder::Release);
}

Job ReactorTestEchoClient(Reactor* reactor, Socket* socket, int id, int rounds, Atomic<int>* failures,
                          Atomic<u64>* trips, DoneCounter* done)
{
    for (int round = 0; round < rounds; ++round)
    {
        byte message[kReactorMessage];
        byte reply[kReactorMessage];
        for (usize i = 0; i < kReactorMessage; ++i)
        {
            message[i] = static_cast<byte>(id * 31 + round * 7 + static_cast<int>(i));
        }
        const IoResult sent = co_await Send(*reactor, *socket, ByteSpan(message, kReactorMessage));
        if (sent.status.Failed())
        {
            failures->FetchAdd(1);
            break;
        }
        usize have = 0;
        while (have < kReactorMessage)
        {
            const IoResult r = co_await Receive(*reactor, *socket,
                                                MutableByteSpan(reply + have, kReactorMessage - have));
            if (r.status.Failed())
            {
                failures->FetchAdd(1);
                done->done.FetchAdd(1, MemoryOrder::Release);
                co_return;
            }
            have += r.count;
        }
        bool same = true;
        for (usize i = 0; i < kReactorMessage; ++i)
        {
            same = same && reply[i] == message[i];
        }
        if (!same)
        {
            failures->FetchAdd(1);
            break;
        }
        trips->FetchAdd(1, MemoryOrder::Relaxed);
    }
    done->done.FetchAdd(1, MemoryOrder::Release);
}

}  // namespace

TEST_CASE("A thousand loopback connections echo a hundred messages each, on 1 to 8 participants",
          "[network][reactor][echo]")
{
    constexpr int kConnections = 1000;
    constexpr int kRounds = 100;
    for (usize participants : {usize{1}, usize{4}, usize{8}})
    {
        auto scheduler = ReactorTestScheduler(participants);
        Reactor reactor(*scheduler);
        REQUIRE_REACTOR(reactor);
        Socket listener;
        Endpoint at;
        if (Socket::OpenTcp(AddressFamily::V4, &listener).Failed() ||
            listener.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed() ||
            listener.Listen(2048).Failed() || listener.LocalEndpoint(&at).Failed())
        {
            SKIP("loopback sockets are unavailable here");
        }
        Vector<Socket> clients;
        Vector<Socket> servers;
        clients.Resize(kConnections);
        servers.Resize(kConnections);
        for (int i = 0; i < kConnections; ++i)
        {
            Socket& client = clients[static_cast<usize>(i)];
            if (Socket::OpenTcp(AddressFamily::V4, &client).Failed())
            {
                SKIP("cannot open that many sockets here");
            }
            const Status c = client.Connect(at);
            REQUIRE((c.Ok() || c.Is(ErrorCode::WouldBlock)));
            Endpoint peer;
            Status a = listener.Accept(&servers[static_cast<usize>(i)], &peer);
            for (int spin = 0; spin < 20000 && a.Is(ErrorCode::WouldBlock); ++spin)
            {
                ThisThread::SleepForMicroseconds(50);
                a = listener.Accept(&servers[static_cast<usize>(i)], &peer);
            }
            REQUIRE(a.Ok());
        }
        Atomic<int> failures{0};
        Atomic<u64> trips{0};
        DoneCounter done;
        for (int i = 0; i < kConnections; ++i)
        {
            scheduler->Submit(
                ReactorTestEchoServer(&reactor, &servers[static_cast<usize>(i)], kRounds, &failures, &done));
            scheduler->Submit(
                ReactorTestEchoClient(&reactor, &clients[static_cast<usize>(i)], i, kRounds, &failures, &trips, &done));
        }
        HelpUntilDone(*scheduler, done, 2 * kConnections);
        CHECK(failures.Load() == 0);
        CHECK(trips.Load() == static_cast<u64>(kConnections) * kRounds);
    }
}

namespace
{

struct StreamOutcome
{
    IoResult sent;
    IoResult received_end;
    usize received = 0;
    bool content_ok = true;
    IoResult accepted;
    IoResult connected;
    Atomic<int> done{0};
};

constexpr usize kReactorStreamBytes = usize{4} << 20;

byte StreamByte(usize i)
{
    return static_cast<byte>((i * 131 + (i >> 9)) & 0xff);
}

Job ReactorTestStreamServer(Reactor* reactor, Socket* listener, Socket* connection, StreamOutcome* out)
{
    Endpoint peer;
    out->accepted = co_await Accept(*reactor, *listener, connection, &peer);
    if (out->accepted.status.Failed())
    {
        out->done.FetchAdd(1, MemoryOrder::Release);
        co_return;
    }
    Vector<byte> buffer;
    buffer.Resize(usize{1} << 16);
    for (;;)
    {
        const IoResult r = co_await Receive(*reactor, *connection, MutableByteSpan(buffer.Data(), buffer.Size()));
        if (r.status.Failed())
        {
            out->received_end = r;
            break;
        }
        for (usize i = 0; i < r.count; ++i)
        {
            out->content_ok = out->content_ok && buffer[i] == StreamByte(out->received + i);
        }
        out->received += r.count;
    }
    out->done.FetchAdd(1, MemoryOrder::Release);
}

Job ReactorTestStreamClient(Reactor* reactor, Socket* socket, Endpoint at, StreamOutcome* out)
{
    out->connected = co_await Connect(*reactor, *socket, at);
    if (out->connected.status.Ok())
    {
        Vector<byte> data;
        data.Resize(kReactorStreamBytes);
        for (usize i = 0; i < data.Size(); ++i)
        {
            data[i] = StreamByte(i);
        }
        out->sent = co_await Send(*reactor, *socket, ByteSpan(data.Data(), data.Size()));
        [[maybe_unused]] const Status shutdown = socket->Shutdown(ShutdownMode::Write);
    }
    out->done.FetchAdd(1, MemoryOrder::Release);
}

}  // namespace

TEST_CASE("Connect, Accept, a large Send and Receive run to the end of the stream", "[network][reactor][stream]")
{
    auto scheduler = ReactorTestScheduler(3);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    Socket listener;
    Endpoint at;
    if (Socket::OpenTcp(AddressFamily::V4, &listener).Failed() ||
        listener.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed() || listener.Listen().Failed() ||
        listener.LocalEndpoint(&at).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Socket client;
    Socket connection;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &client).Ok());
    StreamOutcome server_out;
    StreamOutcome client_out;
    scheduler->Submit(ReactorTestStreamServer(&reactor, &listener, &connection, &server_out));
    scheduler->Submit(ReactorTestStreamClient(&reactor, &client, at, &client_out));
    struct Probe
    {
        StreamOutcome* a;
        StreamOutcome* b;
    } probe{&server_out, &client_out};
    scheduler->HelpUntil(
        [](void* u)
        {
            Probe* p = static_cast<Probe*>(u);
            return p->a->done.Load(MemoryOrder::Acquire) != 0 && p->b->done.Load(MemoryOrder::Acquire) != 0;
        },
        &probe);
    CHECK(client_out.connected.status.Ok());
    CHECK(client_out.sent.status.Ok());
    CHECK(client_out.sent.count == kReactorStreamBytes);
    CHECK(server_out.accepted.status.Ok());
    CHECK(server_out.received == kReactorStreamBytes);
    CHECK(server_out.content_ok);
    CHECK(server_out.received_end.status.Is(ErrorCode::Closed));  // the end of the stream, after all the data
}

namespace
{

Job ReactorTestRefusedConnect(Reactor* reactor, Socket* socket, Endpoint at, IoResult* out, Atomic<int>* done)
{
    *out = co_await Connect(*reactor, *socket, at);
    done->FetchAdd(1, MemoryOrder::Release);
}

Job ReactorTestIdleReceive(Reactor* reactor, Socket* socket, WaitOptions options, IoResult* out, Atomic<int>* done)
{
    byte buffer[8];
    *out = co_await Receive(*reactor, *socket, MutableByteSpan(buffer, sizeof buffer), options);
    done->FetchAdd(1, MemoryOrder::Release);
}

Job ReactorTestDatagrams(Reactor* reactor, Socket* a, Socket* b, Endpoint b_at, IoResult* sent, IoResult* got,
                         Endpoint* from, Atomic<int>* done)
{
    byte buffer[64];
    *sent = co_await SendTo(*reactor, *a, ReactorTestBytes("datagram"), b_at);
    *got = co_await ReceiveFrom(*reactor, *b, MutableByteSpan(buffer, sizeof buffer), from);
    done->FetchAdd(1, MemoryOrder::Release);
}

}  // namespace

TEST_CASE("The asynchronous operations report refusal, timeout and cancellation, and move datagrams",
          "[network][reactor][stream]")
{
    auto scheduler = ReactorTestScheduler(2);
    Reactor reactor(*scheduler);
    REQUIRE_REACTOR(reactor);
    Endpoint closed_port;
    {
        Socket temp;
        if (Socket::OpenTcp(AddressFamily::V4, &temp).Failed() ||
            temp.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed() ||
            temp.LocalEndpoint(&closed_port).Failed())
        {
            SKIP("loopback sockets are unavailable here");
        }
    }
    Atomic<int> done{0};
    Socket refused_client;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &refused_client).Ok());
    IoResult refused;
    scheduler->Submit(ReactorTestRefusedConnect(&reactor, &refused_client, closed_port, &refused, &done));

    TcpPair pair;
    REQUIRE(MakePair(&pair));
    WaitOptions short_timeout;
    short_timeout.timeout_nanos = 30'000'000;
    IoResult timed_out;
    scheduler->Submit(ReactorTestIdleReceive(&reactor, &pair.server, short_timeout, &timed_out, &done));
    CancelToken token;
    WaitOptions cancellable;
    cancellable.token = &token;
    IoResult cancelled;
    scheduler->Submit(ReactorTestIdleReceive(&reactor, &pair.client, cancellable, &cancelled, &done));

    Socket udp_a;
    Socket udp_b;
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &udp_a).Ok());
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &udp_b).Ok());
    REQUIRE(udp_b.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
    Endpoint b_at;
    REQUIRE(udp_b.LocalEndpoint(&b_at).Ok());
    IoResult datagram_sent;
    IoResult datagram_got;
    Endpoint from;
    scheduler->Submit(
        ReactorTestDatagrams(&reactor, &udp_a, &udp_b, b_at, &datagram_sent, &datagram_got, &from, &done));

    ThisThread::SleepForMilliseconds(80);
    token.Cancel();
    struct Probe
    {
        Atomic<int>* done;
    } probe{&done};
    scheduler->HelpUntil([](void* u) { return static_cast<Probe*>(u)->done->Load(MemoryOrder::Acquire) >= 4; }, &probe);
    CHECK(refused.status.Is(ErrorCode::Refused));
    CHECK(timed_out.status.Is(ErrorCode::TimedOut));
    CHECK(cancelled.status.Is(ErrorCode::Cancelled));
    CHECK(datagram_sent.status.Ok());
    CHECK(datagram_sent.count == 8);
    CHECK(datagram_got.status.Ok());
    CHECK(datagram_got.count == 8);
    Endpoint a_at;
    REQUIRE(udp_a.LocalEndpoint(&a_at).Ok());
    CHECK(from.Port() == a_at.Port());
}

}  // namespace vse::net
