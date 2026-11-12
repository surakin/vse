// TcpStream (Phase 20 Day 271): framed echo with every read size, bounded send queue and backpressure, oversized and
// zero-length frames, reset and end of stream mid-frame, idle timeout, graceful close and the linger limit.

#include "build_defs.h"
#if !ENGINE_PLATFORM_WINDOWS
#include <sys/resource.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "jobs/scheduler.h"
#include "net/async.h"
#include "net/listener.h"
#include "net/tcp_stream.h"
#include "os/os.h"
#include "sync/atomic.h"

namespace engine_placeholder::net
{
namespace
{

using jobs::Job;
using jobs::Scheduler;

u64 TcpTestNext(u64* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

usize TcpTestSize(usize index)
{
    u64 state = 0xabcdef12ull + index * 31;
    return 1 + TcpTestNext(&state) % 300;
}

void TcpTestPayload(usize index, Vector<byte>* out)
{
    out->Clear();
    u64 state = 0x9e3779b97f4a7c15ull ^ (index * 0x100000001b3ull);
    const usize size = TcpTestSize(index);
    for (usize i = 0; i < size; ++i)
    {
        out->Push(static_cast<byte>(TcpTestNext(&state) & 0xff));
    }
}

void TcpTestWait(Scheduler& scheduler, Atomic<int>& done, int target)
{
    struct Probe
    {
        Atomic<int>* done;
        int target;
    } probe{&done, target};
    scheduler.HelpUntil(
        [](void* u)
        { return static_cast<Probe*>(u)->done->Load(MemoryOrder::Acquire) >= static_cast<Probe*>(u)->target; }, &probe);
}

// A reactor, a listening loopback socket, and a way to make a connected pair of streams.
class TcpHarness
{
public:
    explicit TcpHarness(usize participants = 4) : scheduler_(MakeConfig(participants)), reactor_(scheduler_) {}

    bool Open() { return listener_.Open(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok(); }

    // Connects `client` to the listener and accepts `server`.
    Status Pair(const TcpStreamOptions& client_options, const TcpStreamOptions& server_options, TcpStream* client,
                TcpStream* server)
    {
        Atomic<int> done{0};
        Status result;
        scheduler_.Submit(PairJob(this, client_options, server_options, client, server, &result, &done));
        TcpTestWait(scheduler_, done, 1);
        return result;
    }

    // A raw client socket connected to the listener, with `server` accepted around the other end.
    Status RawPair(const TcpStreamOptions& server_options, Socket* raw, TcpStream* server)
    {
        Atomic<int> done{0};
        Status result;
        scheduler_.Submit(RawPairJob(this, server_options, raw, server, &result, &done));
        TcpTestWait(scheduler_, done, 1);
        return result;
    }

    Scheduler& GetScheduler() { return scheduler_; }
    Reactor& GetReactor() { return reactor_; }
    Listener& GetListener() { return listener_; }

private:
    static Scheduler::Config MakeConfig(usize participants)
    {
        Scheduler::Config cfg;
        cfg.participants = participants;
        return cfg;
    }

    static Job PairJob(TcpHarness* self, TcpStreamOptions co, TcpStreamOptions so, TcpStream* client, TcpStream* server,
                       Status* result, Atomic<int>* done)
    {
        *result = co_await TcpStream::Connect(self->reactor_, self->listener_.LocalEndpoint(), co, client);
        if (result->Ok())
        {
            Socket accepted;
            Endpoint peer;
            const IoResult a = co_await self->listener_.Accept(self->reactor_, &accepted, &peer);
            *result = a.status;
            if (a.status.Ok())
            {
                *result = server->Adopt(self->reactor_, Move(accepted), so);
                self->listener_.Handled();
            }
        }
        done->FetchAdd(1, MemoryOrder::Release);
    }

    static Job RawPairJob(TcpHarness* self, TcpStreamOptions so, Socket* raw, TcpStream* server, Status* result,
                          Atomic<int>* done)
    {
        *result = Socket::OpenTcp(AddressFamily::V4, raw);
        if (result->Ok())
        {
            const IoResult c = co_await Connect(self->reactor_, *raw, self->listener_.LocalEndpoint());
            *result = c.status;
        }
        if (result->Ok())
        {
            Socket accepted;
            Endpoint peer;
            const IoResult a = co_await self->listener_.Accept(self->reactor_, &accepted, &peer);
            *result = a.status;
            if (a.status.Ok())
            {
                *result = server->Adopt(self->reactor_, Move(accepted), so);
                self->listener_.Handled();
            }
        }
        done->FetchAdd(1, MemoryOrder::Release);
    }

    Scheduler scheduler_;
    Reactor reactor_;
    Listener listener_;
};

struct EchoResult
{
    Atomic<int> done{0};
    usize frames = 0;
    Status status;
};

// Echoes every frame it receives until the stream ends.
Job TcpTestEchoServer(TcpStream* stream, EchoResult* result)
{
    for (;;)
    {
        Status s = co_await stream->ReceiveFrame();
        if (s.Failed())
        {
            result->status = s;
            break;
        }
        s = co_await stream->SendFrame(stream->Frame());
        if (s.Failed())
        {
            result->status = s;
            break;
        }
        ++result->frames;
    }
    result->done.FetchAdd(1, MemoryOrder::Release);
}

Job TcpTestSender(TcpStream* stream, usize frames, EchoResult* result)
{
    Vector<byte> payload;
    for (usize i = 0; i < frames; ++i)
    {
        TcpTestPayload(i, &payload);
        const Status s = co_await stream->SendFrame(ByteSpan(payload.Data(), payload.Size()));
        if (s.Failed())
        {
            result->status = s;
            break;
        }
        ++result->frames;
    }
    result->done.FetchAdd(1, MemoryOrder::Release);
}

Job TcpTestChecker(TcpStream* stream, usize frames, EchoResult* result)
{
    Vector<byte> expected;
    for (usize i = 0; i < frames; ++i)
    {
        const Status s = co_await stream->ReceiveFrame();
        if (s.Failed())
        {
            result->status = s;
            break;
        }
        TcpTestPayload(i, &expected);
        const ByteSpan got = stream->Frame();
        if (got.Size() != expected.Size() || MemCompare(got.Data(), expected.Data(), got.Size()) != 0)
        {
            result->status = Status::Of(ErrorCode::Other);
            break;
        }
        ++result->frames;
    }
    result->done.FetchAdd(1, MemoryOrder::Release);
}

TcpStreamOptions TcpTestOptions(usize max_read)
{
    TcpStreamOptions options;
    options.max_frame_bytes = 1000;
    options.max_read_bytes = max_read;
    return options;
}

}  // namespace

TEST_CASE("Frames echo through a stream whatever the size of each read", "[network][tcp]")
{
    struct Case
    {
        usize max_read;
        usize frames;
    };
    const Case cases[] = {{1, 1500}, {3, 4000}, {7, 10000}, {0, 10000}};
    for (const Case& c : cases)
    {
        TcpHarness harness;
        if (!harness.Open())
        {
            SKIP("loopback sockets are unavailable here");
        }
        TcpStream client;
        TcpStream server;
        REQUIRE(harness.Pair(TcpTestOptions(c.max_read), TcpTestOptions(c.max_read), &client, &server).Ok());
        EchoResult echo;
        EchoResult sent;
        EchoResult checked;
        harness.GetScheduler().Submit(TcpTestEchoServer(&server, &echo));
        harness.GetScheduler().Submit(TcpTestSender(&client, c.frames, &sent));
        harness.GetScheduler().Submit(TcpTestChecker(&client, c.frames, &checked));
        TcpTestWait(harness.GetScheduler(), sent.done, 1);
        TcpTestWait(harness.GetScheduler(), checked.done, 1);
        CHECK(sent.status.Ok());
        CHECK(checked.status.Ok());
        CHECK(checked.frames == c.frames);
        // End the echo: abort the client; the server sees the reset (or the end) and stops.
        CHECK(client.Abort().Ok());
        TcpTestWait(harness.GetScheduler(), echo.done, 1);
        CHECK(echo.frames == c.frames);
        CHECK(server.Reason() != CloseReason::None);
    }
}

namespace
{

struct BackpressureResult
{
    Atomic<int> done{0};
    usize queued_frames = 0;
    usize max_queue_seen = 0;
    bool full_seen = false;
    bool timed_out = false;
    usize capacity_before = 0;
    usize capacity_after = 0;
    Status status;
};

// Fills the stream's queue and the kernel's buffers while nobody reads.
Job TcpTestFiller(TcpStream* stream, BackpressureResult* r)
{
    Vector<byte> payload;
    for (usize i = 0; i < 100000; ++i)
    {
        TcpTestPayload(i, &payload);
        const ByteSpan frame(payload.Data(), payload.Size());
        Status s = stream->Send(frame);
        if (s.Is(ErrorCode::LimitExceeded))
        {
            r->full_seen = true;
            // Drain with a short timeout; when the peer stops taking data the flush times out.
            WaitOptions wait;
            wait.timeout_nanos = 100'000'000;
            s = co_await stream->Flush(stream->SendQueueCapacity() / 2, wait);
            if (s.Is(ErrorCode::TimedOut))
            {
                r->timed_out = true;
                break;
            }
            if (s.Failed())
            {
                r->status = s;
                break;
            }
            s = stream->Send(frame);
        }
        if (s.Failed())
        {
            r->status = s;
            break;
        }
        ++r->queued_frames;
        const usize queued = stream->SendQueueBytes();
        r->max_queue_seen = queued > r->max_queue_seen ? queued : r->max_queue_seen;
    }
    r->done.FetchAdd(1, MemoryOrder::Release);
}

}  // namespace

TEST_CASE("A slow reader stops the writer at the queue's capacity, and nothing is lost", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    TcpStreamOptions small = TcpTestOptions(0);
    small.send_queue_bytes = 8192;
    small.socket_send_buffer_bytes = 4096;
    small.socket_receive_buffer_bytes = 4096;
    TcpStream client;
    TcpStream server;
    REQUIRE(harness.Pair(small, small, &client, &server).Ok());
    const usize capacity = client.SendQueueCapacity();
    CHECK(capacity == 8192);

    BackpressureResult filled;
    harness.GetScheduler().Submit(TcpTestFiller(&client, &filled));
    TcpTestWait(harness.GetScheduler(), filled.done, 1);
    CHECK(filled.status.Ok());
    CHECK(filled.full_seen);
    CHECK(filled.timed_out);
    CHECK(filled.max_queue_seen <= capacity);
    CHECK(client.SendQueueBytes() <= capacity);
    CHECK(client.SendQueueCapacity() == capacity);
    const usize queued = filled.queued_frames;
    CHECK(queued > 10);

    // The reader wakes up: every frame that was queued arrives, in order, and the writer's queue empties.
    EchoResult sent;
    EchoResult checked;
    harness.GetScheduler().Submit(TcpTestChecker(&server, queued, &checked));
    struct FlushJob
    {
        static Job Run(TcpStream* s, EchoResult* r)
        {
            r->status = co_await s->Flush(0);
            r->done.FetchAdd(1, MemoryOrder::Release);
        }
    };
    harness.GetScheduler().Submit(FlushJob::Run(&client, &sent));
    TcpTestWait(harness.GetScheduler(), sent.done, 1);
    TcpTestWait(harness.GetScheduler(), checked.done, 1);
    CHECK(sent.status.Ok());
    CHECK(checked.status.Ok());
    CHECK(checked.frames == queued);
    CHECK(client.SendQueueBytes() == 0);
}

TEST_CASE("A frame that does not fit is refused, an empty one is invalid", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    TcpStreamOptions options = TcpTestOptions(0);
    options.send_queue_bytes = 64;
    TcpStream client;
    TcpStream server;
    REQUIRE(harness.Pair(options, options, &client, &server).Ok());
    byte big[1001] = {};
    CHECK(client.Send(ByteSpan(big, 1001)).Is(ErrorCode::LimitExceeded));  // above max_frame_bytes
    CHECK(client.Send(ByteSpan()).Is(ErrorCode::Invalid));
    CHECK(client.Send(ByteSpan(big, 59)).Ok());
    CHECK(client.SendQueueBytes() == 63);
    CHECK(client.Send(ByteSpan(big, 1)).Is(ErrorCode::LimitExceeded));  // 4 + 1 > 1 byte of room
    CHECK(client.SendQueueBytes() == 63);
}

TEST_CASE("An idle stream holds a few KiB; the queue grows with the frames and never passes its capacity",
          "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    TcpStreamOptions options = TcpTestOptions(0);
    options.send_queue_bytes = 256 * 1024;
    options.max_frame_bytes = 64 * 1024;
    TcpStream client;
    TcpStream server;
    REQUIRE(harness.Pair(options, options, &client, &server).Ok());
    CHECK(client.SendQueueAllocatedBytes() <= 4096);
    Vector<byte> payload;
    payload.Resize(30000);
    usize queued = 0;
    for (u32 i = 0; i < 20; ++i)
    {
        if (client.Send(ByteSpan(payload.Data(), payload.Size())).Failed())
        {
            break;
        }
        queued += payload.Size() + 4;
        CHECK(client.SendQueueAllocatedBytes() >= client.SendQueueBytes());
        CHECK(client.SendQueueAllocatedBytes() <= client.SendQueueCapacity());
    }
    CHECK(queued > 200000);
    CHECK(client.SendQueueAllocatedBytes() == 256 * 1024);
}

namespace
{

struct ReceiveOutcome
{
    Atomic<int> done{0};
    Status status;
    CloseReason reason = CloseReason::None;
    usize frames = 0;
};

Job TcpTestReceiveAll(TcpStream* stream, WaitOptions options, ReceiveOutcome* out)
{
    for (;;)
    {
        const Status s = co_await stream->ReceiveFrame(options);
        if (s.Failed())
        {
            out->status = s;
            break;
        }
        ++out->frames;
    }
    out->reason = stream->Reason();
    out->done.FetchAdd(1, MemoryOrder::Release);
}

enum class RawEnd : u8
{
    Keep,   // leave the socket open
    Close,  // orderly close
    Reset,  // abortive close
};

Job TcpTestRawSend(Reactor* reactor, Socket* raw, Vector<byte>* bytes, RawEnd end, Atomic<int>* done)
{
    if (!bytes->Empty())
    {
        const IoResult r = co_await Send(*reactor, *raw, ByteSpan(bytes->Data(), bytes->Size()));
        if (r.status.Failed())
        {
            done->FetchAdd(100, MemoryOrder::Release);
        }
    }
    if (end == RawEnd::Reset)
    {
        [[maybe_unused]] const Status linger = raw->SetLinger(true, 0);
    }
    if (end != RawEnd::Keep)
    {
        [[maybe_unused]] const Status closed = reactor->Close(*raw);
    }
    done->FetchAdd(1, MemoryOrder::Release);
}

void TcpTestHeader(u32 length, Vector<byte>* out)
{
    out->Push(static_cast<byte>((length >> 24) & 0xff));
    out->Push(static_cast<byte>((length >> 16) & 0xff));
    out->Push(static_cast<byte>((length >> 8) & 0xff));
    out->Push(static_cast<byte>(length & 0xff));
}

// Runs one raw client against a fresh server stream and returns what the server's receive loop saw.
struct Hostile
{
    explicit Hostile(TcpHarness& h) : harness(&h) {}

    ReceiveOutcome& Run(const Vector<byte>& to_send, RawEnd end, WaitOptions options = {},
                        TcpStreamOptions server_options = TcpTestOptions(0))
    {
        ReceiveOutcome& out = outcome;
        REQUIRE(harness->RawPair(server_options, &raw, &server).Ok());
        bytes.Clear();
        for (usize i = 0; i < to_send.Size(); ++i)
        {
            bytes.Push(to_send[i]);
        }
        Atomic<int> sent{0};
        harness->GetScheduler().Submit(TcpTestReceiveAll(&server, options, &out));
        harness->GetScheduler().Submit(TcpTestRawSend(&harness->GetReactor(), &raw, &bytes, end, &sent));
        TcpTestWait(harness->GetScheduler(), out.done, 1);
        TcpTestWait(harness->GetScheduler(), sent, 1);
        return out;
    }

    TcpHarness* harness;
    ReceiveOutcome outcome;
    Socket raw;
    TcpStream server;
    Vector<byte> bytes;
};

}  // namespace

TEST_CASE("A frame above the limit closes the stream before its payload is read", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Hostile h(harness);
    Vector<byte> bytes;
    TcpTestHeader(1001, &bytes);  // the limit is 1000
    const ReceiveOutcome& out = h.Run(bytes, RawEnd::Keep);
    CHECK(out.status.Is(ErrorCode::LimitExceeded));
    CHECK(out.reason == CloseReason::ProtocolError);
    CHECK(out.frames == 0);
    // The stream stays failed, and sending on it is refused.
    byte one[1] = {};
    CHECK(h.server.Send(ByteSpan(one, 1)).Is(ErrorCode::Closed));
}

TEST_CASE("A zero-length frame closes the stream", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Hostile h(harness);
    Vector<byte> bytes;
    TcpTestHeader(0, &bytes);
    const ReceiveOutcome& out = h.Run(bytes, RawEnd::Keep);
    CHECK(out.status.Is(ErrorCode::Invalid));
    CHECK(out.reason == CloseReason::ProtocolError);
}

TEST_CASE("Good frames before a bad one are delivered", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Hostile h(harness);
    Vector<byte> bytes;
    for (int i = 0; i < 3; ++i)
    {
        TcpTestHeader(2, &bytes);
        bytes.Push(byte{7});
        bytes.Push(byte{8});
    }
    TcpTestHeader(5000, &bytes);
    const ReceiveOutcome& out = h.Run(bytes, RawEnd::Keep);
    CHECK(out.frames == 3);
    CHECK(out.status.Is(ErrorCode::LimitExceeded));
}

TEST_CASE("A reset in the middle of a frame is reported as a reset", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Hostile h(harness);
    Vector<byte> bytes;
    TcpTestHeader(100, &bytes);
    for (int i = 0; i < 10; ++i)
    {
        bytes.Push(byte{1});
    }
    const ReceiveOutcome& out = h.Run(bytes, RawEnd::Reset);
    CHECK(out.status.Is(ErrorCode::Reset));
    CHECK(out.reason == CloseReason::Reset);
    CHECK(out.frames == 0);
}

TEST_CASE("The end of the stream inside a frame is a protocol error, at a boundary an orderly close", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    {
        Hostile h(harness);
        Vector<byte> bytes;
        TcpTestHeader(100, &bytes);
        bytes.Push(byte{1});
        const ReceiveOutcome& out = h.Run(bytes, RawEnd::Close);
        CHECK(out.status.Is(ErrorCode::Closed));
        CHECK(out.reason == CloseReason::ProtocolError);
    }
    {
        Hostile h(harness);
        Vector<byte> bytes;
        TcpTestHeader(2, &bytes);
        bytes.Push(byte{1});
        bytes.Push(byte{2});
        const ReceiveOutcome& out = h.Run(bytes, RawEnd::Close);
        CHECK(out.frames == 1);
        CHECK(out.status.Is(ErrorCode::Closed));
        CHECK(out.reason == CloseReason::PeerClosed);
    }
    {
        // Half a header is also inside a frame.
        Hostile h(harness);
        Vector<byte> bytes;
        bytes.Push(byte{0});
        bytes.Push(byte{0});
        const ReceiveOutcome& out = h.Run(bytes, RawEnd::Close);
        CHECK(out.reason == CloseReason::ProtocolError);
    }
}

TEST_CASE("A silent peer ends the stream when the idle time passes", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Hostile h(harness);
    TcpStreamOptions options = TcpTestOptions(0);
    options.idle_timeout_nanos = 50'000'000;
    const u64 started = os::MonotonicTimeNanos();
    const ReceiveOutcome& out = h.Run(Vector<byte>(), RawEnd::Keep, WaitOptions{}, options);
    const u64 elapsed = os::MonotonicTimeNanos() - started;
    CHECK(out.status.Is(ErrorCode::TimedOut));
    CHECK(out.reason == CloseReason::TimedOut);
    CHECK(elapsed >= 40'000'000);
    CHECK(elapsed < 5'000'000'000ull);
}

TEST_CASE("The idle time restarts with every byte that arrives", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    TcpStreamOptions options = TcpTestOptions(0);
    options.idle_timeout_nanos = 150'000'000;
    Socket raw;
    TcpStream server;
    REQUIRE(harness.RawPair(options, &raw, &server).Ok());
    ReceiveOutcome out;
    harness.GetScheduler().Submit(TcpTestReceiveAll(&server, WaitOptions{}, &out));
    // One byte of a frame every 60 ms for 400 ms: slower than nothing, faster than the idle time.
    struct Dribble
    {
        static Job Run(Reactor* reactor, Socket* raw, Atomic<int>* done)
        {
            Vector<byte> header;
            TcpTestHeader(8, &header);
            for (usize i = 0; i < header.Size(); ++i)
            {
                [[maybe_unused]] const Status slept = co_await reactor->Sleep(60'000'000);
                const IoResult r = co_await Send(*reactor, *raw, ByteSpan(header.Data() + i, 1));
                (void)r;
            }
            for (int i = 0; i < 4; ++i)
            {
                [[maybe_unused]] const Status slept = co_await reactor->Sleep(60'000'000);
                const byte b[2] = {byte{1}, byte{2}};
                const IoResult r = co_await Send(*reactor, *raw, ByteSpan(b, 2));
                (void)r;
            }
            done->FetchAdd(1, MemoryOrder::Release);
        }
    };
    Atomic<int> dribbled{0};
    harness.GetScheduler().Submit(Dribble::Run(&harness.GetReactor(), &raw, &dribbled));
    TcpTestWait(harness.GetScheduler(), dribbled, 1);
    TcpTestWait(harness.GetScheduler(), out.done, 1);
    CHECK(out.frames == 1);                      // the frame arrived whole although it took longer than the idle time
    CHECK(out.reason == CloseReason::TimedOut);  // then the silence ended it
}

TEST_CASE("The time limit of one receive does not end the stream", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Socket raw;
    TcpStream server;
    REQUIRE(harness.RawPair(TcpTestOptions(0), &raw, &server).Ok());
    ReceiveOutcome out;
    WaitOptions limited;
    limited.timeout_nanos = 30'000'000;
    harness.GetScheduler().Submit(TcpTestReceiveAll(&server, limited, &out));
    TcpTestWait(harness.GetScheduler(), out.done, 1);
    CHECK(out.status.Is(ErrorCode::TimedOut));
    CHECK(out.reason == CloseReason::None);
    CHECK(server.IsOpen());
}

namespace
{

struct CloseResult
{
    Atomic<int> done{0};
    Status status;
    usize frames = 0;
    CloseReason reason = CloseReason::None;
};

Job TcpTestSendThenClose(TcpStream* stream, usize frames, CloseResult* r)
{
    Vector<byte> payload;
    for (usize i = 0; i < frames; ++i)
    {
        TcpTestPayload(i, &payload);
        // Queue without flushing when there is room, so that Close has something in flight to drain.
        Status s = stream->Send(ByteSpan(payload.Data(), payload.Size()));
        if (s.Is(ErrorCode::LimitExceeded))
        {
            s = co_await stream->SendFrame(ByteSpan(payload.Data(), payload.Size()));
        }
        if (s.Failed())
        {
            r->status = s;
            r->done.FetchAdd(1, MemoryOrder::Release);
            co_return;
        }
    }
    r->status = co_await stream->Close();
    r->reason = stream->Reason();
    r->done.FetchAdd(1, MemoryOrder::Release);
}

// Receives and verifies `frames` frames, then waits for the end and closes its side.
Job TcpTestReceiveThenClose(TcpStream* stream, usize frames, CloseResult* r)
{
    Vector<byte> expected;
    for (usize i = 0; i < frames; ++i)
    {
        const Status s = co_await stream->ReceiveFrame();
        TcpTestPayload(i, &expected);
        if (s.Failed() || stream->Frame().Size() != expected.Size() ||
            MemCompare(stream->Frame().Data(), expected.Data(), expected.Size()) != 0)
        {
            r->status = s.Failed() ? s : Status::Of(ErrorCode::Other);
            r->done.FetchAdd(1, MemoryOrder::Release);
            co_return;
        }
        ++r->frames;
    }
    const Status end = co_await stream->ReceiveFrame();
    r->status = end.Is(ErrorCode::Closed) ? Status::Success() : end;
    r->reason = stream->Reason();
    const Status closed = co_await stream->Close();
    if (r->status.Ok())
    {
        r->status = closed;
    }
    r->done.FetchAdd(1, MemoryOrder::Release);
}

}  // namespace

TEST_CASE("A graceful close delivers everything that was in flight", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    TcpStream client;
    TcpStream server;
    REQUIRE(harness.Pair(TcpTestOptions(0), TcpTestOptions(0), &client, &server).Ok());
    constexpr usize kFrames = 800;  // more than the queue holds at once: the close must drain it
    CloseResult closed;
    CloseResult received;
    harness.GetScheduler().Submit(TcpTestReceiveThenClose(&server, kFrames, &received));
    harness.GetScheduler().Submit(TcpTestSendThenClose(&client, kFrames, &closed));
    TcpTestWait(harness.GetScheduler(), closed.done, 1);
    TcpTestWait(harness.GetScheduler(), received.done, 1);
    CHECK(closed.status.Ok());
    CHECK(closed.reason == CloseReason::LocalClose);
    CHECK_FALSE(client.IsOpen());
    CHECK(received.status.Ok());
    CHECK(received.frames == kFrames);
    CHECK(received.reason == CloseReason::PeerClosed);
    CHECK_FALSE(server.IsOpen());
}

TEST_CASE("A close that the peer never answers ends at the linger time, abortively", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    TcpStreamOptions options = TcpTestOptions(0);
    options.linger_nanos = 60'000'000;
    TcpStream client;
    TcpStream server;
    REQUIRE(harness.Pair(options, TcpTestOptions(0), &client, &server).Ok());
    CloseResult closed;
    struct Closer
    {
        static Job Run(TcpStream* s, CloseResult* r)
        {
            r->status = co_await s->Close();
            r->reason = s->Reason();
            r->done.FetchAdd(1, MemoryOrder::Release);
        }
    };
    harness.GetScheduler().Submit(Closer::Run(&client, &closed));
    TcpTestWait(harness.GetScheduler(), closed.done, 1);  // the server never reads, so it never closes its side
    CHECK(closed.status.Is(ErrorCode::TimedOut));
    CHECK(closed.reason == CloseReason::LocalClose);
    CHECK_FALSE(client.IsOpen());
}

TEST_CASE("Aborting a stream resets the peer", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    TcpStream client;
    TcpStream server;
    REQUIRE(harness.Pair(TcpTestOptions(0), TcpTestOptions(0), &client, &server).Ok());
    ReceiveOutcome out;
    harness.GetScheduler().Submit(TcpTestReceiveAll(&server, WaitOptions{}, &out));
    byte one[1] = {byte{5}};
    REQUIRE(client.Send(ByteSpan(one, 1)).Ok());  // queued, never sent: dropped by the abort
    CHECK(client.Abort().Ok());
    CHECK(client.Reason() == CloseReason::LocalClose);
    CHECK(client.Send(ByteSpan(one, 1)).Is(ErrorCode::Closed));
    TcpTestWait(harness.GetScheduler(), out.done, 1);
    CHECK(out.status.Is(ErrorCode::Reset));
    CHECK(out.reason == CloseReason::Reset);
    CHECK(out.frames == 0);
}

TEST_CASE("Connecting to a port nobody listens on is refused, to a listener works", "[network][tcp]")
{
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    // Take a free port and release it again.
    Endpoint dead;
    {
        Listener other;
        REQUIRE(other.Open(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
        dead = other.LocalEndpoint();
    }
    struct Attempt
    {
        static Job Run(Reactor* reactor, Endpoint at, TcpStream* stream, Status* result, Atomic<int>* done)
        {
            *result = co_await TcpStream::Connect(*reactor, at, TcpTestOptions(0), stream);
            done->FetchAdd(1, MemoryOrder::Release);
        }
    };
    TcpStream stream;
    Status result;
    Atomic<int> done{0};
    harness.GetScheduler().Submit(Attempt::Run(&harness.GetReactor(), dead, &stream, &result, &done));
    TcpTestWait(harness.GetScheduler(), done, 1);
    CHECK(result.Is(ErrorCode::Refused));
    CHECK_FALSE(stream.IsOpen());
}

TEST_CASE("Keep-alive timing and linger are accepted on a stream socket and refused on a datagram socket",
          "[network][tcp]")
{
    Socket tcp;
    Socket udp;
    if (Socket::OpenTcp(AddressFamily::V4, &tcp).Failed() || Socket::OpenUdp(AddressFamily::V4, &udp).Failed())
    {
        SKIP("sockets are unavailable here");
    }
    CHECK(tcp.SetKeepAlive(true).Ok());
    CHECK(tcp.SetKeepAliveTiming(30, 5, 3).Ok());
    CHECK(tcp.SetKeepAliveTiming(0, 0, 0).Ok());
    CHECK(tcp.SetLinger(true, 0).Ok());
    CHECK(tcp.SetLinger(false, 0).Ok());
    CHECK(udp.SetKeepAliveTiming(30, 5, 3).Is(ErrorCode::Invalid));
    CHECK(udp.SetLinger(true, 0).Is(ErrorCode::Invalid));
    Socket closed;
    CHECK(closed.SetLinger(true, 0).Is(ErrorCode::Invalid));
}

TEST_CASE("A stream that was never opened answers Closed", "[network][tcp]")
{
    TcpHarness harness;
    TcpStream stream;
    byte one[1] = {};
    CHECK(stream.Send(ByteSpan(one, 1)).Is(ErrorCode::Closed));
    CHECK_FALSE(stream.IsOpen());
    CHECK(stream.Reason() == CloseReason::None);
    CHECK(stream.Abort().Ok());
    struct Probe
    {
        static Job Run(TcpStream* s, Status* flushed, Status* received, Status* sent, Status* closed, Atomic<int>* done)
        {
            byte b[1] = {};
            *flushed = co_await s->Flush(0);
            *received = co_await s->ReceiveFrame();
            *sent = co_await s->SendFrame(ByteSpan(b, 1));
            *closed = co_await s->Close();
            done->FetchAdd(1, MemoryOrder::Release);
        }
    };
    Status flushed;
    Status received;
    Status sent;
    Status closed;
    Atomic<int> done{0};
    harness.GetScheduler().Submit(Probe::Run(&stream, &flushed, &received, &sent, &closed, &done));
    TcpTestWait(harness.GetScheduler(), done, 1);
    CHECK(flushed.Is(ErrorCode::Closed));
    CHECK(received.Is(ErrorCode::Closed));
    CHECK(sent.Is(ErrorCode::Closed));
    CHECK(closed.Ok());
}

TEST_CASE("A thousand streams carry a frame each", "[network][tcp]")
{
    constexpr usize kPairs = 500;
#if !ENGINE_PLATFORM_WINDOWS  // Windows has no per-process descriptor limit
    struct rlimit limit{};
    if (::getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur < 4096 && limit.rlim_cur < limit.rlim_max)
    {
        limit.rlim_cur = limit.rlim_max < 4096 ? limit.rlim_max : 4096;
        ::setrlimit(RLIMIT_NOFILE, &limit);
    }
    if (::getrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur < 2 * kPairs + 64)
    {
        SKIP("the descriptor limit is too low for a thousand streams");
    }
#endif
    TcpHarness harness;
    if (!harness.Open())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Vector<UniquePtr<TcpStream>> clients;
    Vector<UniquePtr<TcpStream>> servers;
    TcpStreamOptions options = TcpTestOptions(0);
    options.receive_buffer_bytes = 4096;
    options.send_queue_bytes = 4096;
    for (usize i = 0; i < kPairs; ++i)
    {
        clients.Push(MakeUnique<TcpStream>());
        servers.Push(MakeUnique<TcpStream>());
        REQUIRE(harness.Pair(options, options, clients[i].Get(), servers[i].Get()).Ok());
    }
    Vector<UniquePtr<EchoResult>> echoes;
    Vector<UniquePtr<EchoResult>> sent;
    Vector<UniquePtr<EchoResult>> checked;
    for (usize i = 0; i < kPairs; ++i)
    {
        echoes.Push(MakeUnique<EchoResult>());
        sent.Push(MakeUnique<EchoResult>());
        checked.Push(MakeUnique<EchoResult>());
        harness.GetScheduler().Submit(TcpTestEchoServer(servers[i].Get(), echoes[i].Get()));
        harness.GetScheduler().Submit(TcpTestSender(clients[i].Get(), 3, sent[i].Get()));
        harness.GetScheduler().Submit(TcpTestChecker(clients[i].Get(), 3, checked[i].Get()));
    }
    usize good = 0;
    for (usize i = 0; i < kPairs; ++i)
    {
        TcpTestWait(harness.GetScheduler(), checked[i]->done, 1);
        TcpTestWait(harness.GetScheduler(), sent[i]->done, 1);
        if (checked[i]->status.Ok() && checked[i]->frames == 3 && sent[i]->status.Ok())
        {
            ++good;
        }
    }
    CHECK(good == kPairs);
    for (usize i = 0; i < kPairs; ++i)
    {
        CHECK(clients[i]->Abort().Ok());
    }
    for (usize i = 0; i < kPairs; ++i)
    {
        TcpTestWait(harness.GetScheduler(), echoes[i]->done, 1);
    }
}

}  // namespace engine_placeholder::net
