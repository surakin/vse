// The UDP transport's data plane (Phase 20 Day 275): the conformance suite over simulated networks at 0 to 60 percent
// loss, large fragmented messages, channel isolation, and hostile fragment streams within their budgets.

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "net/sim_datagram.h"
#include "net/udp_transport.h"
#include "transport_conformance.h"

namespace engine_placeholder::net
{
namespace
{

Endpoint ChEp(const char* text)
{
    Endpoint e;
    REQUIRE(Endpoint::Parse(StringView(text), &e).Ok());
    return e;
}

LinkConfig MakeLink(f64 loss, f64 reorder, f64 duplicate)
{
    LinkConfig l;
    l.latency_nanos = 15'000'000;
    l.jitter_nanos = 8'000'000;
    l.loss = loss;
    l.reorder = reorder;
    l.duplicate = duplicate;
    return l;
}

struct Node
{
    Node(SimNetwork& net, const Endpoint& at, const UdpTransportConfig& cfg)
        : port(net, at), transport(port, net.GetClock(), cfg)
    {
    }
    SimDatagramPort port;
    UdpTransport transport;
};

template <u32 kLossPercent> class UdpFixture
{
public:
    UdpFixture() : network_(0xc0ffee00ull + kLossPercent)
    {
        network_.SetDefaultLink(
            MakeLink(kLossPercent / 100.0, kLossPercent == 0 ? 0.0 : 0.1, kLossPercent == 0 ? 0.0 : 0.1));
        server_endpoint_ = ChEp("127.0.0.1:9000");
        dead_endpoint_ = ChEp("127.0.0.1:9999");
    }
    ~UdpFixture()
    {
        for (usize i = 0; i < nodes_.Size(); ++i)
        {
            nodes_[i].Reset();
        }
    }
    UdpTransportConfig Config() const
    {
        UdpTransportConfig c;
        c.seed = 1234;
        c.max_message_bytes = 8192;
        c.max_connections_per_address = 1024;
        c.connect_timeout_nanos = 60'000'000'000ull;
        c.handshake_retry_nanos = 100'000'000ull;
        c.timeout_nanos = 60'000'000'000ull;
        return c;
    }
    Transport* MakeServer()
    {
        nodes_.Push(MakeUnique<Node>(network_, server_endpoint_, Config()));
        REQUIRE(nodes_[nodes_.Size() - 1]->transport.Listen(server_endpoint_).Ok());
        return &nodes_[nodes_.Size() - 1]->transport;
    }
    Transport* MakeClient()
    {
        nodes_.Push(MakeUnique<Node>(network_, network_.AllocateEndpoint(), Config()));
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
    void Pump(u32 milliseconds) { network_.Advance(static_cast<u64>(milliseconds) * 1'000'000ull); }
    bool Lossy() const { return kLossPercent != 0; }
    SimNetwork& Network() { return network_; }

private:
    SimNetwork network_;
    Vector<UniquePtr<Node>> nodes_;
    Endpoint server_endpoint_;
    Endpoint dead_endpoint_;
};

}  // namespace

#define UDP_CONFORMANCE(NAME)                                                                                          \
    TEST_CASE("UDP transport, 0% loss: " #NAME, "[net][udp]")                                                          \
    {                                                                                                                  \
        UdpFixture<0> f;                                                                                               \
        conformance::NAME(f);                                                                                          \
    }                                                                                                                  \
    TEST_CASE("UDP transport, 5% loss: " #NAME, "[net][udp]")                                                          \
    {                                                                                                                  \
        UdpFixture<5> f;                                                                                               \
        conformance::NAME(f);                                                                                          \
    }                                                                                                                  \
    TEST_CASE("UDP transport, 30% loss: " #NAME, "[net][udp]")                                                         \
    {                                                                                                                  \
        UdpFixture<30> f;                                                                                              \
        conformance::NAME(f);                                                                                          \
    }                                                                                                                  \
    TEST_CASE("UDP transport, 60% loss: " #NAME, "[net][udp]")                                                         \
    {                                                                                                                  \
        UdpFixture<60> f;                                                                                              \
        conformance::NAME(f);                                                                                          \
    }

UDP_CONFORMANCE(ConnectAndAccept)
UDP_CONFORMANCE(CloseCarriesReasons)
UDP_CONFORMANCE(ConnectToNothingIsRefused)
UDP_CONFORMANCE(ReliableOrderedArrivesOnceInOrder)
UDP_CONFORMANCE(ReliableUnorderedArrivesOnce)
UDP_CONFORMANCE(UnreliableIsNeverCorruptedOrDuplicated)
UDP_CONFORMANCE(UnreliableSequencedNeverGoesBack)
UDP_CONFORMANCE(MessageSizeAndChannelLimits)
UDP_CONFORMANCE(ChannelsAreIndependent)
UDP_CONFORMANCE(ManyConnectionsAtOnce)
UDP_CONFORMANCE(CloseDuringSend)
UDP_CONFORMANCE(NoEventsAfterClose)
UDP_CONFORMANCE(DestroyingATransportWithTrafficInFlight)

namespace
{

// A 1 MiB-class message with a pattern that shows any shifted or missing byte.
void Pattern(u32 seed, usize size, Vector<byte>* out)
{
    out->Clear();
    u32 x = seed * 2654435761u + 1;
    for (usize i = 0; i < size; ++i)
    {
        x = x * 1664525u + 1013904223u;
        out->Push(static_cast<byte>(x >> 24));
    }
}

bool Same(const Vector<byte>& a, ByteSpan b)
{
    return a.Size() == b.Size() && (a.Size() == 0 || MemCompare(a.Data(), b.Data(), a.Size()) == 0);
}

struct Pair
{
    explicit Pair(u64 seed, const LinkConfig& link, const UdpTransportConfig& server_cfg,
                  const UdpTransportConfig& client_cfg)
        : net(seed), server_ep(ChEp("127.0.0.1:9000")), server(net, server_ep, server_cfg),
          client(net, ChEp("127.0.0.1:9001"), client_cfg)
    {
        net.SetDefaultLink(link);
        REQUIRE(server.transport.Listen(server_ep).Ok());
        REQUIRE(client.transport.Connect(server_ep, &cc).Ok());
    }
    // Advances time, collecting events, until `done` or the budget runs out.
    template <class Done> bool Until(u32 ms, Done done)
    {
        for (u32 t = 0; t < ms; t += 2)
        {
            net.Advance(2'000'000);
            Event e;
            while (server.transport.Poll(&e))
            {
                Take(e, server_events, &sc);
            }
            while (client.transport.Poll(&e))
            {
                Take(e, client_events, nullptr);
            }
            if (done())
            {
                return true;
            }
        }
        return false;
    }
    struct Ev
    {
        EventKind kind;
        u8 channel;
        DisconnectReason reason;
        Vector<byte> data;
    };
    static void Take(const Event& e, Vector<Ev>& into, ConnectionId* conn)
    {
        Ev v;
        v.kind = e.kind;
        v.channel = e.channel;
        v.reason = e.reason;
        for (usize i = 0; i < e.message.Size(); ++i)
        {
            v.data.Push(e.message[i]);
        }
        if (conn != nullptr && e.kind == EventKind::Opened)
        {
            *conn = e.connection;
        }
        into.Push(Move(v));
    }
    usize Messages(const Vector<Ev>& v) const
    {
        usize n = 0;
        for (usize i = 0; i < v.Size(); ++i)
        {
            n += v[i].kind == EventKind::Message ? 1 : 0;
        }
        return n;
    }
    SimNetwork net;
    Endpoint server_ep;
    Node server;
    Node client;
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    Vector<Ev> server_events;
    Vector<Ev> client_events;
};

UdpTransportConfig BigConfig()
{
    UdpTransportConfig c;
    c.seed = 99;
    c.max_message_bytes = 1u << 20;
    c.timeout_nanos = 120'000'000'000ull;
    c.connect_timeout_nanos = 120'000'000'000ull;
    return c;
}

}  // namespace

namespace
{

bool Opened(const Vector<Pair::Ev>& v)
{
    for (usize i = 0; i < v.Size(); ++i)
    {
        if (v[i].kind == EventKind::Opened)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("UDP transport: a 1 MiB message arrives intact under every impairment", "[net][udp]")
{
    const f64 losses[] = {0.0, 0.05, 0.30, 0.60};
    for (u32 k = 0; k < 4; ++k)
    {
        Pair p(500 + k, MakeLink(losses[k], 0.1, 0.1), BigConfig(), BigConfig());
        REQUIRE(p.Until(60000, [&] { return Opened(p.client_events) && p.sc != 0; }));
        Vector<byte> msg;
        Pattern(k + 1, 1u << 20, &msg);
        REQUIRE(p.client.transport.Send(p.cc, 3, ByteSpan(msg.Data(), msg.Size()), Reliability::ReliableOrdered).Ok());
        REQUIRE(p.Until(900000, [&] { return p.Messages(p.server_events) == 1; }));
        for (usize i = 0; i < p.server_events.Size(); ++i)
        {
            if (p.server_events[i].kind == EventKind::Message)
            {
                CHECK(p.server_events[i].channel == 3);
                CHECK(Same(msg, ByteSpan(p.server_events[i].data.Data(), p.server_events[i].data.Size())));
            }
        }
        UdpConnectionInfo info;
        REQUIRE(p.client.transport.Info(p.cc, &info).Ok());
        if (losses[k] > 0)
        {
            CHECK(info.frames_resent > 0);
        }
        // Everything was acknowledged in the end: nothing is left queued.
        ConnectionStats stats;
        REQUIRE(
            p.Until(120000, [&]
                    { return p.client.transport.Statistics(p.cc, &stats).Ok() && stats.send_queue_messages == 0; }));
        CHECK(stats.send_queue_messages == 0);
        CHECK(stats.send_queue_bytes == 0);
    }
}

TEST_CASE("UDP transport: many mixed-size messages arrive once and in order on every channel", "[net][udp]")
{
    const f64 losses[] = {0.0, 0.2, 0.5};
    for (u32 k = 0; k < 3; ++k)
    {
        Pair p(900 + k, MakeLink(losses[k], 0.1, 0.1), BigConfig(), BigConfig());
        REQUIRE(p.Until(60000, [&] { return Opened(p.client_events) && p.sc != 0; }));
        const u32 kCount = 400;
        u64 x = 12345 + k;
        Vector<usize> sizes;
        for (u32 i = 0; i < kCount; ++i)
        {
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            const u32 pick = static_cast<u32>(x >> 33) % 10;
            usize size = 4 + static_cast<usize>((x >> 20) % 200);
            if (pick == 0)
            {
                size = 1100 + static_cast<usize>((x >> 20) % 9000);
            }
            else if (pick == 1)
            {
                size = 1024 * 3;
            }
            sizes.Push(size);
        }
        for (u32 i = 0; i < kCount; ++i)
        {
            Vector<byte> msg;
            Pattern(i, sizes[i], &msg);
            msg[0] = static_cast<byte>(i);
            msg[1] = static_cast<byte>(i >> 8);
            msg[2] = static_cast<byte>(i >> 16);
            msg[3] = static_cast<byte>(i >> 24);
            const u8 channel = static_cast<u8>(i % 4);
            for (;;)
            {
                const Status st = p.client.transport.Send(p.cc, channel, ByteSpan(msg.Data(), msg.Size()),
                                                          Reliability::ReliableOrdered);
                if (st.Ok())
                {
                    break;
                }
                REQUIRE(st.Is(ErrorCode::LimitExceeded));
                p.Until(20, [] { return false; });
            }
        }
        REQUIRE(p.Until(900000, [&] { return p.Messages(p.server_events) == kCount; }));
        u32 next[4] = {0, 1, 2, 3};
        for (usize i = 0; i < p.server_events.Size(); ++i)
        {
            const Pair::Ev& e = p.server_events[i];
            if (e.kind != EventKind::Message)
            {
                continue;
            }
            const u32 index = static_cast<u32>(static_cast<u8>(e.data[0])) |
                              (static_cast<u32>(static_cast<u8>(e.data[1])) << 8) |
                              (static_cast<u32>(static_cast<u8>(e.data[2])) << 16) |
                              (static_cast<u32>(static_cast<u8>(e.data[3])) << 24);
            REQUIRE(e.channel < 4);
            CHECK(index == next[e.channel]);
            next[e.channel] += 4;
            REQUIRE(index < kCount);
            Vector<byte> want;
            Pattern(index, sizes[index], &want);
            want[0] = static_cast<byte>(index);
            want[1] = static_cast<byte>(index >> 8);
            want[2] = static_cast<byte>(index >> 16);
            want[3] = static_cast<byte>(index >> 24);
            CHECK(Same(want, ByteSpan(e.data.Data(), e.data.Size())));
        }
    }
}

TEST_CASE("UDP transport: a large message on a high channel does not hold up small ones on a low channel", "[net][udp]")
{
    Pair p(31, MakeLink(0.0, 0, 0), BigConfig(), BigConfig());
    REQUIRE(p.Until(10000, [&] { return Opened(p.client_events) && p.sc != 0; }));
    Vector<byte> big;
    Pattern(7, 1u << 20, &big);
    REQUIRE(p.client.transport.Send(p.cc, 7, ByteSpan(big.Data(), big.Size()), Reliability::ReliableOrdered).Ok());
    for (u8 i = 0; i < 20; ++i)
    {
        const byte b[2] = {static_cast<byte>(i), static_cast<byte>(0x5a)};
        REQUIRE(p.client.transport.Send(p.cc, 0, ByteSpan(b, 2), Reliability::ReliableOrdered).Ok());
    }
    REQUIRE(p.Until(60000, [&] { return p.Messages(p.server_events) == 21; }));
    usize big_at = 0;
    usize last_small = 0;
    for (usize i = 0; i < p.server_events.Size(); ++i)
    {
        if (p.server_events[i].kind == EventKind::Message)
        {
            (p.server_events[i].channel == 7 ? big_at : last_small) = i;
        }
    }
    CHECK(last_small < big_at);
}

namespace
{

// A port that lets a test send raw datagrams from the client's own endpoint and learn its connection's wire id.
class InjectPort final : public DatagramPort
{
public:
    InjectPort(SimNetwork& net, const Endpoint& local) : inner_(net, local) {}
    Status SendTo(ByteSpan bytes, const Endpoint& to) override
    {
        udp::PacketKind kind = udp::PacketKind::Payload;
        if (bytes.Size() > 0 && udp::ParseFirstByte(static_cast<u8>(bytes[0]), &kind) &&
            kind == udp::PacketKind::Payload)
        {
            udp::PayloadHeader h;
            ByteSpan body;
            if (udp::ReadPayloadHeader(bytes, &h, &body))
            {
                wire_id = h.connection;
                next_sequence = static_cast<u16>(h.sequence + 1000);
                known = true;
            }
        }
        return inner_.SendTo(bytes, to);
    }
    Status ReceiveFrom(MutableByteSpan buffer, usize* received, Endpoint* from) override
    {
        return inner_.ReceiveFrom(buffer, received, from);
    }
    Endpoint LocalEndpoint() const override { return inner_.LocalEndpoint(); }

    // Sends one payload packet holding one data frame, built by hand.
    void Frame(const Endpoint& to, u8 channel, u8 kind, u16 seq, u16 length, const u16* fragment_index,
               const u16* fragment_count, u32 total, usize payload_bytes)
    {
        Vector<byte> p;
        udp::PayloadHeader h;
        h.connection = wire_id;
        h.sequence = next_sequence++;
        udp::WritePayloadHeader(h, &p);
        p.Push(static_cast<byte>(1));
        p.Push(static_cast<byte>(channel));
        p.Push(static_cast<byte>(kind));
        p.Push(static_cast<byte>(seq & 0xff));
        p.Push(static_cast<byte>(seq >> 8));
        p.Push(static_cast<byte>(length & 0xff));
        p.Push(static_cast<byte>(length >> 8));
        if (fragment_index != nullptr)
        {
            p.Push(static_cast<byte>(*fragment_index & 0xff));
            p.Push(static_cast<byte>(*fragment_index >> 8));
            p.Push(static_cast<byte>(*fragment_count & 0xff));
            p.Push(static_cast<byte>(*fragment_count >> 8));
            for (u32 i = 0; i < 4; ++i)
            {
                p.Push(static_cast<byte>(total >> (8 * i)));
            }
        }
        for (usize i = 0; i < payload_bytes; ++i)
        {
            p.Push(static_cast<byte>(0xaa));
        }
        CHECK(inner_.SendTo(ByteSpan(p.Data(), p.Size()), to).Ok());
    }

    u16 wire_id = 0;
    u16 next_sequence = 0;
    bool known = false;

private:
    SimDatagramPort inner_;
};

struct Attack
{
    Attack()
        : net(5), server_ep(ChEp("127.0.0.1:9000")), server_port(net, server_ep),
          server(server_port, net.GetClock(), BigConfig()), client_ep(ChEp("127.0.0.1:9001")),
          client_port(net, client_ep), client(client_port, net.GetClock(), BigConfig())
    {
        net.SetDefaultLink(MakeLink(0, 0, 0));
        REQUIRE(server.Listen(server_ep).Ok());
        REQUIRE(client.Connect(server_ep, &cc).Ok());
        Run(300);
        REQUIRE(sc != 0);
        const byte b[1] = {static_cast<byte>(1)};
        REQUIRE(client.Send(cc, 0, ByteSpan(b, 1), Reliability::ReliableOrdered).Ok());
        Run(300);
        REQUIRE(client_port.known);
    }
    void Run(u32 ms)
    {
        for (u32 t = 0; t < ms; ++t)
        {
            net.Advance(1'000'000);
            Event e;
            while (server.Poll(&e))
            {
                if (e.kind == EventKind::Opened)
                {
                    sc = e.connection;
                }
                else if (e.kind == EventKind::Message)
                {
                    ++messages;
                }
                else if (e.kind == EventKind::Closed)
                {
                    closed = true;
                    reason = e.reason;
                }
            }
            while (client.Poll(&e))
            {
            }
        }
    }
    SimNetwork net;
    Endpoint server_ep;
    SimDatagramPort server_port;
    UdpTransport server;
    Endpoint client_ep;
    InjectPort client_port;
    UdpTransport client;
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    u32 messages = 0;
    bool closed = false;
    DisconnectReason reason = DisconnectReason::None;
};

}  // namespace

TEST_CASE("UDP transport: an unreliable fragment declaring a gigabyte is dropped, a reliable one ends the connection",
          "[net][udp]")
{
    {
        Attack a;
        const u16 index = 0;
        const u16 count = 2;
        a.client_port.Frame(a.server_ep, 0, static_cast<u8>(Reliability::Unreliable) | 0x80, 1, 1024, &index, &count,
                            1u << 30, 1024);
        const u16 many = 60000;
        a.client_port.Frame(a.server_ep, 0, static_cast<u8>(Reliability::Unreliable) | 0x80, 2, 1024, &index, &many,
                            1u << 30, 1024);
        a.Run(200);
        UdpConnectionInfo info;
        REQUIRE(a.server.Info(a.sc, &info).Ok());
        CHECK(info.buffered_bytes == 0);
        CHECK(info.reassemblies == 0);
        CHECK_FALSE(a.closed);
    }
    {
        Attack a;
        const u16 index = 0;
        const u16 many = 60000;
        a.client_port.Frame(a.server_ep, 0, static_cast<u8>(Reliability::ReliableOrdered) | 0x80, 1, 1024, &index,
                            &many, 1u << 30, 1024);
        a.Run(200);
        CHECK(a.closed);
        CHECK(a.reason == DisconnectReason::ProtocolError);
    }
}

TEST_CASE("UDP transport: thousands of one-fragment messages stay within the reassembly budget", "[net][udp]")
{
    Attack a;
    const u16 index = 0;
    const u16 count = 2;
    for (u16 i = 0; i < 4000; ++i)
    {
        a.client_port.Frame(a.server_ep, 1, static_cast<u8>(Reliability::Unreliable) | 0x80, static_cast<u16>(i + 10),
                            1024, &index, &count, 1025, 1024);
        if (i % 20 == 19)
        {
            a.Run(1);
        }
    }
    a.Run(100);
    UdpConnectionInfo info;
    REQUIRE(a.server.Info(a.sc, &info).Ok());
    CHECK(info.reassemblies <= BigConfig().max_reassemblies);
    CHECK(info.buffered_bytes <= BigConfig().max_receive_buffer_bytes);
    CHECK(a.messages == 1);  // only the first message of the setup was delivered
    CHECK_FALSE(a.closed);
    // The incomplete messages are dropped when their time runs out.
    a.Run(6000);
    REQUIRE(a.server.Info(a.sc, &info).Ok());
    CHECK(info.reassemblies == 0);
    CHECK(info.buffered_bytes == 0);
}

TEST_CASE("UDP transport: fragments that disagree or have the wrong length never produce a message", "[net][udp]")
{
    Attack a;
    const u16 zero = 0;
    const u16 one = 1;
    const u16 two = 2;
    const u8 kind = static_cast<u8>(Reliability::Unreliable) | 0x80;
    // The first fragment declares 1500 bytes in 2 pieces; the second disagrees about the total.
    a.client_port.Frame(a.server_ep, 2, kind, 20, 1024, &zero, &two, 1500, 1024);
    a.client_port.Frame(a.server_ep, 2, kind, 20, 600, &one, &two, 1624, 600);
    // A last fragment of the wrong length for the declared total.
    a.client_port.Frame(a.server_ep, 2, kind, 21, 1024, &zero, &two, 1500, 1024);
    a.client_port.Frame(a.server_ep, 2, kind, 21, 100, &one, &two, 1500, 100);
    // A non-last fragment shorter than 1024.
    a.client_port.Frame(a.server_ep, 2, kind, 22, 500, &zero, &two, 1500, 500);
    // An index outside its count, a count of 1, a total above the largest message.
    a.client_port.Frame(a.server_ep, 2, kind, 23, 1024, &two, &two, 1500, 1024);
    a.client_port.Frame(a.server_ep, 2, kind, 24, 1024, &zero, &one, 1024, 1024);
    a.client_port.Frame(a.server_ep, 2, kind, 25, 1024, &zero, &two, (1u << 20) + 5000, 1024);
    a.Run(200);
    CHECK(a.messages == 1);
    CHECK_FALSE(a.closed);
    // A consistent pair, in either order, is delivered once, even if the first piece arrives twice.
    a.client_port.Frame(a.server_ep, 2, kind, 30, 476, &one, &two, 1500, 476);
    a.client_port.Frame(a.server_ep, 2, kind, 30, 1024, &zero, &two, 1500, 1024);
    a.client_port.Frame(a.server_ep, 2, kind, 30, 1024, &zero, &two, 1500, 1024);
    a.Run(200);
    CHECK(a.messages == 2);
}

TEST_CASE("UDP transport: a reliable message far ahead of the receive window ends the connection", "[net][udp]")
{
    Attack a;
    a.client_port.Frame(a.server_ep, 0, static_cast<u8>(Reliability::ReliableOrdered), 5000, 1, nullptr, nullptr, 0, 1);
    a.Run(200);
    CHECK(a.closed);
    CHECK(a.reason == DisconnectReason::ProtocolError);
}

TEST_CASE("UDP transport: duplicated fragments of delivered messages leave nothing behind", "[net][udp]")
{
    // Heavy duplication and reordering: copies of fragments arrive after their message was completed or delivered.
    UdpTransportConfig cfg = BigConfig();
    Pair p(77, MakeLink(0.1, 0.3, 0.6), cfg, cfg);
    REQUIRE(p.Until(60000, [&] { return Opened(p.client_events) && p.sc != 0; }));
    const u32 kCount = 800;  // more than max_reassemblies: a leak would end the connection
    for (u32 i = 0; i < kCount; ++i)
    {
        Vector<byte> msg;
        Pattern(i, 2000 + (i % 5) * 300, &msg);
        for (;;)
        {
            const Status st = p.client.transport.Send(p.cc, 0, ByteSpan(msg.Data(), msg.Size()),
                                                      Reliability::ReliableOrdered);
            if (st.Ok())
            {
                break;
            }
            REQUIRE(st.Is(ErrorCode::LimitExceeded));
            p.Until(20, [] { return false; });
        }
    }
    REQUIRE(p.Until(600000, [&] { return p.Messages(p.server_events) == kCount; }));
    p.Until(2000, [] { return false; });
    UdpConnectionInfo info;
    REQUIRE(p.server.transport.Info(p.sc, &info).Ok());
    CHECK(info.reassemblies == 0);
    CHECK(info.buffered_bytes == 0);
    for (usize i = 0; i < p.server_events.Size(); ++i)
    {
        CHECK(p.server_events[i].kind != EventKind::Closed);
    }
}

TEST_CASE("UDP transport: a message limit the wire cannot carry is lowered, and larger messages are refused",
          "[net][udp]")
{
    UdpTransportConfig cfg = BigConfig();
    cfg.max_message_bytes = static_cast<usize>(100) << 20;  // 102,400 fragments: more than a 16-bit count holds
    Pair p(3, MakeLink(0, 0, 0), cfg, cfg);
    REQUIRE(p.Until(10000, [&] { return Opened(p.client_events) && p.sc != 0; }));
    CHECK(p.client.transport.MaxMessageBytes() == static_cast<usize>(65535) * 1024);
    Vector<byte> big;
    big.Resize(static_cast<usize>(65535) * 1024 + 1);
    CHECK(p.client.transport.Send(p.cc, 0, ByteSpan(big.Data(), big.Size()), Reliability::ReliableOrdered)
              .Is(ErrorCode::MessageTooLarge));
}

}  // namespace engine_placeholder::net
