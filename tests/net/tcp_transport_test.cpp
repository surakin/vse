// The TCP transport (Phase 20 Day 277): the conformance suite over a simulated byte stream (whole reads and one-byte
// reads), a stream that breaks the protocol in each way, and a limit on half-open connections.

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "net/frame.h"
#include "net/sim_network.h"
#include "net/sim_stream.h"
#include "net/tcp_transport.h"
#include "transport_conformance.h"

namespace engine_placeholder::net
{
namespace
{

struct TcpNode
{
    TcpNode(SimStreamNetwork& net, const Endpoint& at, const Clock& clock, const TcpTransportConfig& cfg)
        : provider(net.MakeProvider(at)), transport(*provider, clock, cfg)
    {
    }
    UniquePtr<StreamProvider> provider;
    TcpTransport transport;
};

template <usize kMaxRead> class TcpFixture
{
public:
    TcpFixture() : time_(1), streams_(time_.GetClock(), MakeConfig())
    {
        server_endpoint_ = streams_.AllocateEndpoint();
        dead_endpoint_ = streams_.AllocateEndpoint();
    }
    ~TcpFixture()
    {
        for (usize i = 0; i < nodes_.Size(); ++i)
        {
            nodes_[i].Reset();
        }
    }
    static SimStreamConfig MakeConfig()
    {
        SimStreamConfig c;
        c.max_read_bytes = kMaxRead;
        return c;
    }
    Transport* MakeServer()
    {
        nodes_.Push(MakeUnique<TcpNode>(streams_, server_endpoint_, time_.GetClock(), Config()));
        REQUIRE(nodes_[nodes_.Size() - 1]->transport.Listen(server_endpoint_).Ok());
        return &nodes_[nodes_.Size() - 1]->transport;
    }
    Transport* MakeClient()
    {
        nodes_.Push(MakeUnique<TcpNode>(streams_, streams_.AllocateEndpoint(), time_.GetClock(), Config()));
        return &nodes_[nodes_.Size() - 1]->transport;
    }
    Endpoint ServerEndpoint() const { return server_endpoint_; }
    Endpoint DeadEndpoint() const { return dead_endpoint_; }
    void Destroy(Transport* t)
    {
        for (usize i = 0; i < nodes_.Size(); ++i)
        {
            if (nodes_[i] && &nodes_[i]->transport == t)
            {
                nodes_[i].Reset();
            }
        }
    }
    void Pump(u32 milliseconds) { time_.Advance(static_cast<u64>(milliseconds) * 1'000'000ull); }
    bool Lossy() const { return false; }

    static TcpTransportConfig Config()
    {
        TcpTransportConfig c;
        c.max_message_bytes = 8192;
        return c;
    }

private:
    SimNetwork time_;
    SimStreamNetwork streams_;
    Vector<UniquePtr<TcpNode>> nodes_;
    Endpoint server_endpoint_;
    Endpoint dead_endpoint_;
};

}  // namespace

#define TCP_CONFORMANCE(NAME)                                                                                          \
    TEST_CASE("TCP transport, whole reads: " #NAME, "[net][tcp]")                                                      \
    {                                                                                                                  \
        TcpFixture<0> f;                                                                                               \
        conformance::NAME(f);                                                                                          \
    }                                                                                                                  \
    TEST_CASE("TCP transport, one-byte reads: " #NAME, "[net][tcp]")                                                   \
    {                                                                                                                  \
        TcpFixture<1> f;                                                                                               \
        conformance::NAME(f);                                                                                          \
    }

TCP_CONFORMANCE(ConnectAndAccept)
TCP_CONFORMANCE(CloseCarriesReasons)
TCP_CONFORMANCE(ConnectToNothingIsRefused)
TCP_CONFORMANCE(ReliableOrderedArrivesOnceInOrder)
TCP_CONFORMANCE(ReliableUnorderedArrivesOnce)
TCP_CONFORMANCE(UnreliableIsNeverCorruptedOrDuplicated)
TCP_CONFORMANCE(UnreliableSequencedNeverGoesBack)
TCP_CONFORMANCE(MessageSizeAndChannelLimits)
TCP_CONFORMANCE(ChannelsAreIndependent)
TCP_CONFORMANCE(ManyConnectionsAtOnce)
TCP_CONFORMANCE(CloseDuringSend)
TCP_CONFORMANCE(NoEventsAfterClose)
TCP_CONFORMANCE(DestroyingATransportWithTrafficInFlight)

namespace
{

// A server, and a raw stream to it that says whatever the test wants.
struct RawRig
{
    explicit RawRig(const TcpTransportConfig& cfg = TcpFixture<0>::Config())
        : time(1), streams(time.GetClock()), server_ep(streams.AllocateEndpoint()),
          node(streams, server_ep, time.GetClock(), cfg), raw_provider(streams.MakeProvider(streams.AllocateEndpoint()))
    {
        REQUIRE(node.transport.Listen(server_ep).Ok());
        server = conformance::MakePeer(&node.transport);
    }
    void Step(u32 milliseconds)
    {
        for (u32 t = 0; t < milliseconds; ++t)
        {
            time.Advance(1'000'000ull);
            server.Drain();
        }
    }
    UniquePtr<StreamIo> OpenRaw()
    {
        UniquePtr<StreamIo> io;
        REQUIRE(raw_provider->Connect(server_ep, &io).Ok());
        return io;
    }
    SimNetwork time;
    SimStreamNetwork streams;
    Endpoint server_ep;
    TcpNode node;
    UniquePtr<StreamProvider> raw_provider;
    conformance::Peer server;
};

void SendRaw(StreamIo& io, const Vector<byte>& bytes)
{
    usize at = 0;
    while (at < bytes.Size())
    {
        usize sent = 0;
        const Status s = io.Send(ByteSpan(bytes.Data() + at, bytes.Size() - at), &sent);
        if (s.Failed())
        {
            return;
        }
        at += sent;
    }
}

Vector<byte> Bytes(const u8* p, usize n)
{
    Vector<byte> v;
    for (usize i = 0; i < n; ++i)
    {
        v.Push(static_cast<byte>(p[i]));
    }
    return v;
}

}  // namespace

TEST_CASE("TCP transport: a frame header declaring four gigabytes ends the connection", "[net][tcp]")
{
    RawRig rig;
    UniquePtr<StreamIo> io = rig.OpenRaw();
    rig.Step(30);
    const u8 header[] = {0xff, 0xff, 0xff, 0xff, 1, 2, 3};
    SendRaw(*io, Bytes(header, sizeof(header)));
    rig.Step(200);
    CHECK(rig.server.Count(EventKind::Opened) == 0);
    CHECK(rig.node.transport.ConnectionCount() == 0);
}

TEST_CASE("TCP transport: garbage before the handshake never opens a connection", "[net][tcp]")
{
    RawRig rig;
    UniquePtr<StreamIo> io = rig.OpenRaw();
    rig.Step(30);
    u8 junk[64];
    for (usize i = 0; i < sizeof(junk); ++i)
    {
        junk[i] = static_cast<u8>(i * 37 + 11);
    }
    SendRaw(*io, Bytes(junk, sizeof(junk)));
    rig.Step(200);
    CHECK(rig.server.Count(EventKind::Opened) == 0);
    CHECK(rig.node.transport.ConnectionCount() == 0);
}

TEST_CASE("TCP transport: a stream that says nothing times out of the handshake", "[net][tcp]")
{
    TcpTransportConfig cfg = TcpFixture<0>::Config();
    cfg.handshake_timeout_nanos = 1'000'000'000ull;
    RawRig rig(cfg);
    UniquePtr<StreamIo> io = rig.OpenRaw();
    rig.Step(100);
    CHECK(rig.node.transport.ConnectionCount() == 1);
    rig.Step(1200);
    CHECK(rig.node.transport.ConnectionCount() == 0);
}

TEST_CASE("TCP transport: a frame that stalls halfway is dropped by the idle time-out", "[net][tcp]")
{
    // A valid handshake from a real client transport, then a raw stream that starts a frame and stops.
    TcpTransportConfig cfg = TcpFixture<0>::Config();
    cfg.handshake_timeout_nanos = 1'000'000'000ull;
    RawRig rig(cfg);
    UniquePtr<StreamIo> io = rig.OpenRaw();
    rig.Step(30);
    const u8 half[] = {20, 0, 0, 0, 1};  // declares 20 bytes and delivers one
    SendRaw(*io, Bytes(half, sizeof(half)));
    rig.Step(1500);
    CHECK(rig.node.transport.ConnectionCount() == 0);
    CHECK(rig.server.Count(EventKind::Opened) == 0);
}

TEST_CASE("TCP transport: thousands of half-open connections stay within the cap", "[net][tcp]")
{
    TcpTransportConfig cfg = TcpFixture<0>::Config();
    cfg.max_connections = 100;
    RawRig rig(cfg);
    Vector<UniquePtr<StreamIo>> held;
    for (u32 i = 0; i < 10000; ++i)
    {
        held.Push(rig.OpenRaw());
        if (i % 500 == 0)
        {
            rig.Step(10);
            CHECK(rig.node.transport.ConnectionCount() <= 100);
        }
    }
    rig.Step(100);
    CHECK(rig.node.transport.ConnectionCount() <= 100);
}

TEST_CASE("TCP transport: a full send queue is backpressure, not a failure", "[net][tcp]")
{
    TcpFixture<0> f;
    Transport* st = f.MakeServer();
    Transport* ct = f.MakeClient();
    conformance::Peer server = conformance::MakePeer(st);
    conformance::Peer client = conformance::MakePeer(ct);
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(conformance::Connect(f, &client, &server, &cc, &sc));
    Vector<byte> msg;
    for (u32 i = 0; i < 4096; ++i)
    {
        msg.Push(static_cast<byte>(i));
    }
    usize accepted = 0;
    bool full = false;
    for (u32 i = 0; i < 100000 && !full; ++i)
    {
        const Status s = ct->Send(cc, 0, ByteSpan(msg.Data(), msg.Size()), Reliability::ReliableOrdered);
        if (s.Ok())
        {
            ++accepted;
        }
        else
        {
            CHECK(s.Is(ErrorCode::LimitExceeded));
            full = true;
        }
    }
    CHECK(full);
    conformance::SettleUntil(
        f, 600000u, [&] { return server.Count(EventKind::Message) >= accepted; }, &client, &server);
    CHECK(server.Count(EventKind::Message) == accepted);
}

TEST_CASE("TCP transport: a peer that pings and never reads the answers is dropped", "[net][tcp]")
{
    TcpTransportConfig cfg = TcpFixture<0>::Config();
    cfg.max_send_queue_bytes = 4096;
    RawRig rig(cfg);
    UniquePtr<StreamIo> io = rig.OpenRaw();
    rig.Step(30);
    const FrameWriter writer(cfg.max_message_bytes + 16);
    Vector<byte> hello;
    hello.Push(static_cast<byte>(1));  // Hello
    hello.Push(static_cast<byte>(1));  // version
    hello.Push(static_cast<byte>(4));  // channels
    for (u32 i = 0; i < 4; ++i)
    {
        hello.Push(static_cast<byte>(i == 1 ? 4 : 0));  // 1024 bytes
    }
    Vector<byte> bytes;
    REQUIRE(writer.Encode(ByteSpan(hello.Data(), hello.Size()), &bytes).Ok());
    SendRaw(*io, bytes);
    rig.Step(30);
    REQUIRE(rig.server.Count(EventKind::Opened) == 1);
    Vector<byte> ping;
    ping.Push(static_cast<byte>(4));  // Ping
    for (u32 i = 0; i < 8; ++i)
    {
        ping.Push(static_cast<byte>(0));
    }
    Vector<byte> ping_frame;
    REQUIRE(writer.Encode(ByteSpan(ping.Data(), ping.Size()), &ping_frame).Ok());
    for (u32 i = 0; i < 5000 && rig.server.Count(EventKind::Closed) == 0; ++i)
    {
        for (u32 k = 0; k < 20; ++k)
        {
            SendRaw(*io, ping_frame);
        }
        rig.Step(1);
    }
    REQUIRE(rig.server.Count(EventKind::Closed) == 1);
    CHECK(rig.server.First(EventKind::Closed)->reason == DisconnectReason::ProtocolError);
}

}  // namespace engine_placeholder::net
