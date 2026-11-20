// Hostile peers against the transports (Phase 20 Day 279): handshake floods, bad cookies, spoofed sources, a global
// receive budget, acknowledgements and sequence numbers that lie, slow-loris streams and accept floods. Each attack
// ends within its limits and the server stays good for a client that behaves. (Fragment bombs and the reorder-gap
// attack are in udp_channels_test.cpp, half-open TCP streams in tcp_transport_test.cpp.)

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "net/frame.h"
#include "net/sim_datagram.h"
#include "net/sim_network.h"
#include "net/sim_stream.h"
#include "net/tcp_transport.h"
#include "net/udp_protocol.h"
#include "net/udp_transport.h"
#include "transport_conformance.h"

namespace vse::net
{
namespace
{

Endpoint SeEp(const char* text)
{
    Endpoint e;
    REQUIRE(Endpoint::Parse(StringView(text), &e).Ok());
    return e;
}

LinkConfig SeLink()
{
    LinkConfig l;
    l.latency_nanos = 5'000'000;
    return l;
}

Vector<byte> SeRequest(u64 salt)
{
    Vector<byte> p;
    p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::ConnectRequest)));
    for (u32 i = 0; i < 8; ++i)
    {
        p.Push(static_cast<byte>(salt >> (8 * i)));
    }
    p.Resize(udp::kHandshakeBytes);
    return p;
}

Vector<byte> SeBadResponse(u64 salt)
{
    Vector<byte> p;
    p.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::ChallengeResponse)));
    for (u32 i = 0; i < 16; ++i)
    {
        p.Push(static_cast<byte>(salt >> (8 * (i % 8))) ^ static_cast<byte>(i));
    }
    p.Resize(udp::kHandshakeBytes);
    return p;
}

struct SeUdp
{
    explicit SeUdp(const UdpTransportConfig& cfg)
        : net(31), server_ep(SeEp("127.0.0.1:9000")), server_port(net, server_ep),
          server(server_port, net.GetClock(), cfg)
    {
        net.SetDefaultLink(SeLink());
        REQUIRE(server.Listen(server_ep).Ok());
        peer = conformance::MakePeer(&server);
    }
    void Run(u32 ms)
    {
        for (u32 t = 0; t < ms; ++t)
        {
            net.Advance(1'000'000);
            peer.Drain();
        }
    }
    // A client at `address` connects, sends a message, and gets it echoed as a count: true if the server serves it.
    bool GoodClientConnects(const char* address)
    {
        SimDatagramPort port(net, SeEp(address));
        UdpTransportConfig cfg;
        cfg.seed = 99;
        UdpTransport client(port, net.GetClock(), cfg);
        conformance::Peer client_peer = conformance::MakePeer(&client);
        ConnectionId cc = 0;
        if (client.Connect(server_ep, &cc).Failed())
        {
            return false;
        }
        const usize before = peer.Count(EventKind::Message);
        bool sent = false;
        for (u32 t = 0; t < 3000; ++t)
        {
            net.Advance(1'000'000);
            peer.Drain();
            client_peer.Drain();
            if (!sent && client_peer.Count(EventKind::Opened) != 0)
            {
                const byte m[3] = {static_cast<byte>(1), static_cast<byte>(2), static_cast<byte>(3)};
                if (client.Send(cc, 0, ByteSpan(m, 3), Reliability::ReliableOrdered).Failed())
                {
                    return false;
                }
                sent = true;
            }
            if (peer.Count(EventKind::Message) > before)
            {
                return true;
            }
        }
        return false;
    }
    SimNetwork net;
    Endpoint server_ep;
    SimDatagramPort server_port;
    UdpTransport server;
    conformance::Peer peer;
};

}  // namespace

TEST_CASE("Security: a handshake flood from one address is cut at its burst and a good client is still served",
          "[net][security][udp]")
{
    UdpTransportConfig cfg;
    cfg.seed = 1;
    SeUdp rig(cfg);
    SimDatagramPort attacker(rig.net, SeEp("127.0.0.2:5000"));
    for (u32 i = 0; i < 10000; ++i)
    {
        const Vector<byte> p = SeRequest(1000 + i);
        (void)attacker.SendTo(ByteSpan(p.Data(), p.Size()), rig.server_ep);
    }
    rig.Run(50);
    const u64 challenges = rig.server.Counters().challenges_sent;
    CHECK(challenges <= 220);  // the address burst of 200, plus what refilled while the packets arrived
    CHECK(rig.server.Counters().dropped_rate_limited >= 9700);
    CHECK(rig.server.ConnectionCount() == 0);
    CHECK(rig.GoodClientConnects("127.0.0.3:6000"));
}

TEST_CASE("Security: spoofed sources are bounded by the global handshake rate and leave no state",
          "[net][security][udp]")
{
    UdpTransportConfig cfg;
    cfg.seed = 2;
    cfg.handshake_limits.total_per_second = 1000;
    cfg.handshake_limits.total_burst = 500;
    SeUdp rig(cfg);
    Vector<UniquePtr<SimDatagramPort>> sources;
    for (u32 i = 0; i < 400; ++i)
    {
        char text[32];
        FormatTo(text, sizeof(text), "10.1.%u.%u:4000", i / 200, i % 200 + 1);
        sources.Push(MakeUnique<SimDatagramPort>(rig.net, SeEp(text)));
    }
    for (u32 round = 0; round < 10; ++round)
    {
        for (u32 i = 0; i < sources.Size(); ++i)
        {
            const Vector<byte> p = SeRequest(round * 1000 + i);
            (void)sources[i]->SendTo(ByteSpan(p.Data(), p.Size()), rig.server_ep);
        }
    }
    rig.Run(20);
    CHECK(rig.server.Counters().challenges_sent <= 520);
    CHECK(rig.server.Counters().dropped_rate_limited >= 3400);
    CHECK(rig.server.ConnectionCount() == 0);
    rig.Run(2000);  // the bucket refills: a good client is served
    CHECK(rig.GoodClientConnects("127.0.0.3:6000"));
}

TEST_CASE("Security: bad cookies from a forged source never lock that address out", "[net][security][udp]")
{
    UdpTransportConfig cfg;
    cfg.seed = 3;
    SeUdp rig(cfg);
    SimDatagramPort spoofer(rig.net, SeEp("127.0.0.2:5000"));
    for (u32 i = 0; i < 500; ++i)
    {
        const Vector<byte> p = SeBadResponse(i);
        (void)spoofer.SendTo(ByteSpan(p.Data(), p.Size()), rig.server_ep);
    }
    rig.Run(50);
    CHECK(rig.server.Counters().dropped_bad_cookie == 500);
    CHECK(rig.server.ConnectionCount() == 0);
    // The address the packets claimed is served at once: nothing bans on unverified evidence.
    CHECK(rig.GoodClientConnects("127.0.0.2:5001"));
}

TEST_CASE("Security: the receive budget over all connections ends the one that would pass it", "[net][security][udp]")
{
    UdpTransportConfig cfg;
    cfg.seed = 4;
    cfg.max_message_bytes = 1u << 20;
    cfg.max_total_receive_buffer_bytes = 40000;
    cfg.max_receive_buffer_bytes = 16u << 20;
    SeUdp rig(cfg);
    SimDatagramPort port(rig.net, SeEp("127.0.0.2:5000"));
    UdpTransportConfig client_cfg;
    client_cfg.seed = 7;
    client_cfg.max_message_bytes = 1u << 20;
    UdpTransport client(port, rig.net.GetClock(), client_cfg);
    conformance::Peer client_peer = conformance::MakePeer(&client);
    ConnectionId cc = 0;
    REQUIRE(client.Connect(rig.server_ep, &cc).Ok());
    for (u32 t = 0; t < 300; ++t)
    {
        rig.net.Advance(1'000'000);
        rig.peer.Drain();
        client_peer.Drain();
    }
    Vector<byte> big;
    big.Resize(200000);
    REQUIRE(client.Send(cc, 0, ByteSpan(big.Data(), big.Size()), Reliability::ReliableOrdered).Ok());
    for (u32 t = 0; t < 3000; ++t)
    {
        rig.net.Advance(1'000'000);
        rig.peer.Drain();
        client_peer.Drain();
    }
    CHECK(rig.server.Counters().budget_closes >= 1);
    CHECK(rig.peer.Count(EventKind::Message) == 0);
    REQUIRE(rig.peer.First(EventKind::Closed) != nullptr);
    CHECK(rig.peer.First(EventKind::Closed)->reason == DisconnectReason::ProtocolError);
    // A message that fits is still served to another client.
    CHECK(rig.GoodClientConnects("127.0.0.3:6000"));
}

namespace
{

// A client port that learns its connection's wire id and can send hand-made payload packets from the client's address.
class SeInjectPort final : public DatagramPort
{
public:
    SeInjectPort(SimNetwork& net, const Endpoint& local) : inner_(net, local) {}
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
                last_sequence = h.sequence;
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

    // An empty payload packet with the given sequence and acknowledgement fields.
    void Forge(const Endpoint& to, u16 sequence, u16 ack, u32 bits)
    {
        udp::PayloadHeader h;
        h.connection = wire_id;
        h.sequence = sequence;
        h.ack = ack;
        h.ack_bits = bits;
        h.has_ack = true;
        Vector<byte> p;
        udp::WritePayloadHeader(h, &p);
        (void)inner_.SendTo(ByteSpan(p.Data(), p.Size()), to);
    }

    u16 wire_id = 0;
    u16 last_sequence = 0;
    bool known = false;

private:
    SimDatagramPort inner_;
};

struct SeLiar
{
    SeLiar()
        : net(41), server_ep(SeEp("127.0.0.1:9000")), server_port(net, server_ep),
          server(server_port, net.GetClock(), Config()), client_ep(SeEp("127.0.0.1:9001")), client_port(net, client_ep),
          client(client_port, net.GetClock(), Config())
    {
        net.SetDefaultLink(SeLink());
        REQUIRE(server.Listen(server_ep).Ok());
        REQUIRE(client.Connect(server_ep, &cc).Ok());
        server_peer = conformance::MakePeer(&server);
        client_peer = conformance::MakePeer(&client);
        Run(300);
        const byte b[1] = {static_cast<byte>(1)};
        REQUIRE(client.Send(cc, 0, ByteSpan(b, 1), Reliability::ReliableOrdered).Ok());
        Run(300);
        REQUIRE(client_port.known);
        sc = server_peer.First(EventKind::Opened)->connection;
    }
    static UdpTransportConfig Config()
    {
        UdpTransportConfig c;
        c.seed = 8;
        return c;
    }
    void Run(u32 ms)
    {
        for (u32 t = 0; t < ms; ++t)
        {
            net.Advance(1'000'000);
            server_peer.Drain();
            client_peer.Drain();
        }
    }
    SimNetwork net;
    Endpoint server_ep;
    SimDatagramPort server_port;
    UdpTransport server;
    Endpoint client_ep;
    SeInjectPort client_port;
    UdpTransport client;
    conformance::Peer server_peer;
    conformance::Peer client_peer;
    ConnectionId cc = 0;
    ConnectionId sc = 0;
};

}  // namespace

TEST_CASE("Security: acknowledgements of packets never sent acknowledge nothing", "[net][security][udp]")
{
    SeLiar rig;
    // The server sends a little to the client, so it has packets in flight to lie about.
    for (u32 i = 0; i < 5; ++i)
    {
        const byte m[2] = {static_cast<byte>(i), static_cast<byte>(9)};
        REQUIRE(rig.server.Send(rig.sc, 0, ByteSpan(m, 2), Reliability::ReliableOrdered).Ok());
    }
    UdpConnectionInfo before;
    REQUIRE(rig.server.Info(rig.sc, &before).Ok());
    // The client's side claims to have received everything, from sequences far away and all 32 before each.
    for (u32 i = 0; i < 200; ++i)
    {
        rig.client_port.Forge(rig.server_ep, static_cast<u16>(rig.client_port.last_sequence + 1 + i),
                              static_cast<u16>(40000 + i * 97), 0xffffffffu);
    }
    rig.Run(100);
    UdpConnectionInfo after;
    REQUIRE(rig.server.Info(rig.sc, &after).Ok());
    ConnectionStats stats;
    REQUIRE(rig.server.Statistics(rig.sc, &stats).Ok());
    CHECK(after.packets_acked <= stats.packets_sent);
    CHECK(rig.server_peer.Count(EventKind::Closed) == 0);
    // Reliable messages the server sends afterwards still arrive.
    rig.Run(2000);  // the five messages sent above arrive first
    const usize had = rig.client_peer.Count(EventKind::Message);
    CHECK(had == 5);
    const byte m[2] = {static_cast<byte>(200), static_cast<byte>(1)};
    REQUIRE(rig.server.Send(rig.sc, 0, ByteSpan(m, 2), Reliability::ReliableOrdered).Ok());
    rig.Run(2000);
    CHECK(rig.client_peer.Count(EventKind::Message) == had + 1);
}

TEST_CASE("Security: sequence numbers that jump, repeat and run backwards are dropped or ignored",
          "[net][security][udp]")
{
    SeLiar rig;
    const usize messages = rig.server_peer.Count(EventKind::Message);
    const u16 base = rig.client_port.last_sequence;
    for (u32 i = 0; i < 1000; ++i)
    {
        rig.client_port.Forge(rig.server_ep, static_cast<u16>(base + 1), 0, 0);  // the same sequence again and again
    }
    for (u32 i = 0; i < 200; ++i)
    {
        rig.client_port.Forge(rig.server_ep, static_cast<u16>(base + 40000 + i), 0, 0);  // a leap
        rig.client_port.Forge(rig.server_ep, static_cast<u16>(base - 1000 - i), 0, 0);   // far behind
    }
    rig.client_port.Forge(rig.server_ep, 0xffff, 0, 0);
    rig.client_port.Forge(rig.server_ep, 0, 0, 0);
    rig.Run(100);
    UdpConnectionInfo info;
    REQUIRE(rig.server.Info(rig.sc, &info).Ok());
    CHECK(info.duplicates >= 900);
    CHECK(rig.server_peer.Count(EventKind::Closed) == 0);
    CHECK(rig.server_peer.Count(EventKind::Message) == messages);
    // And the connection still carries a real message.
    const byte m[2] = {static_cast<byte>(5), static_cast<byte>(6)};
    REQUIRE(rig.client.Send(rig.cc, 0, ByteSpan(m, 2), Reliability::ReliableOrdered).Ok());
    rig.Run(3000);
    CHECK(rig.server_peer.Count(EventKind::Message) == messages + 1);
}

namespace
{

struct SeTcp
{
    explicit SeTcp(const TcpTransportConfig& cfg)
        : time(51), streams(time.GetClock()), server_ep(streams.AllocateEndpoint()),
          server_provider(streams.MakeProvider(server_ep)), server(*server_provider, time.GetClock(), cfg)
    {
        REQUIRE(server.Listen(server_ep).Ok());
        peer = conformance::MakePeer(&server);
    }
    void Run(u32 ms)
    {
        for (u32 t = 0; t < ms; ++t)
        {
            time.Advance(1'000'000);
            peer.Drain();
        }
    }
    bool GoodClientConnects(const char* address)
    {
        UniquePtr<StreamProvider> provider = streams.MakeProvider(SeEp(address));
        TcpTransport client(*provider, time.GetClock());
        conformance::Peer client_peer = conformance::MakePeer(&client);
        ConnectionId cc = 0;
        if (client.Connect(server_ep, &cc).Failed())
        {
            return false;
        }
        const usize before = peer.Count(EventKind::Message);
        bool sent = false;
        for (u32 t = 0; t < 3000; ++t)
        {
            time.Advance(1'000'000);
            peer.Drain();
            client_peer.Drain();
            if (!sent && client_peer.Count(EventKind::Opened) != 0)
            {
                const byte m[3] = {static_cast<byte>(1), static_cast<byte>(2), static_cast<byte>(3)};
                if (client.Send(cc, 0, ByteSpan(m, 3), Reliability::ReliableOrdered).Failed())
                {
                    return false;
                }
                sent = true;
            }
            if (peer.Count(EventKind::Message) > before)
            {
                return true;
            }
        }
        return false;
    }
    SimNetwork time;
    SimStreamNetwork streams;
    Endpoint server_ep;
    UniquePtr<StreamProvider> server_provider;
    TcpTransport server;
    conformance::Peer peer;
};

}  // namespace

TEST_CASE("Security: slow-loris streams time out, earn a ban, and a good client is served", "[net][security][tcp]")
{
    TcpTransportConfig cfg;
    cfg.handshake_timeout_nanos = 200'000'000;
    cfg.accept_limits.strikes_to_ban = 3;
    cfg.accept_limits.ban_nanos = 30'000'000'000ull;
    SeTcp rig(cfg);
    UniquePtr<StreamProvider> attacker = rig.streams.MakeProvider(SeEp("127.0.0.2:5000"));
    Vector<UniquePtr<StreamIo>> held;
    for (u32 i = 0; i < 50; ++i)
    {
        UniquePtr<StreamIo> io;
        if (attacker->Connect(rig.server_ep, &io).Ok())
        {
            held.Push(Move(io));
        }
    }
    rig.Run(50);
    for (u32 i = 0; i < held.Size(); ++i)
    {
        const byte one[1] = {static_cast<byte>(1)};  // a byte of a frame header, then silence
        usize sent = 0;
        (void)held[i]->Send(ByteSpan(one, 1), &sent);
    }
    rig.Run(600);
    CHECK(rig.server.ConnectionCount() == 0);
    CHECK(rig.server.AcceptLimiterCounters().bans == 1);
    // More attempts from the banned address are closed on arrival.
    for (u32 i = 0; i < 20; ++i)
    {
        UniquePtr<StreamIo> io;
        if (attacker->Connect(rig.server_ep, &io).Ok())
        {
            held.Push(Move(io));
        }
    }
    rig.Run(100);
    CHECK(rig.server.ConnectionCount() == 0);
    CHECK(rig.server.AcceptLimiterCounters().refused_banned >= 20);
    CHECK(rig.GoodClientConnects("127.0.0.3:6000"));
}

TEST_CASE("Security: an accept flood from one address is cut at the per-address limits", "[net][security][tcp]")
{
    TcpTransportConfig cfg;
    cfg.max_connections_per_address = 40;
    SeTcp rig(cfg);
    UniquePtr<StreamProvider> attacker = rig.streams.MakeProvider(SeEp("127.0.0.2:5000"));
    Vector<UniquePtr<StreamIo>> held;
    for (u32 i = 0; i < 5000; ++i)
    {
        UniquePtr<StreamIo> io;
        if (attacker->Connect(rig.server_ep, &io).Ok())
        {
            held.Push(Move(io));
        }
        if (i % 100 == 0)
        {
            rig.Run(12);
            CHECK(rig.server.ConnectionCount() <= 40);
        }
    }
    rig.Run(100);
    CHECK(rig.server.ConnectionCount() <= 40);
    CHECK(rig.server.AcceptLimiterCounters().refused_address > 0);
    CHECK(rig.GoodClientConnects("127.0.0.3:6000"));
}

TEST_CASE("Security: the send budget over all connections is backpressure, not memory", "[net][security][tcp]")
{
    TcpTransportConfig cfg;
    cfg.max_total_send_queue_bytes = 64 * 1024;
    cfg.max_send_queue_bytes = 1u << 20;
    cfg.max_message_bytes = 8192;
    SeTcp rig(cfg);
    // A raw peer that completes the handshake and never reads: the server's output to it piles up.
    UniquePtr<StreamProvider> raw = rig.streams.MakeProvider(SeEp("127.0.0.2:5000"));
    UniquePtr<StreamIo> io;
    REQUIRE(raw->Connect(rig.server_ep, &io).Ok());
    const FrameWriter writer(8192 + 16);
    Vector<byte> hello;
    hello.Push(static_cast<byte>(1));
    hello.Push(static_cast<byte>(1));
    hello.Push(static_cast<byte>(4));
    for (u32 k = 0; k < 4; ++k)
    {
        hello.Push(static_cast<byte>(k == 1 ? 32 : 0));
    }
    Vector<byte> bytes;
    REQUIRE(writer.Encode(ByteSpan(hello.Data(), hello.Size()), &bytes).Ok());
    rig.Run(30);
    usize sent = 0;
    REQUIRE(io->Send(ByteSpan(bytes.Data(), bytes.Size()), &sent).Ok());
    rig.Run(50);
    REQUIRE(rig.peer.Count(EventKind::Opened) == 1);
    const ConnectionId id = rig.peer.First(EventKind::Opened)->connection;
    Vector<byte> msg;
    msg.Resize(4096);
    bool refused = false;
    usize accepted = 0;
    for (u32 i = 0; i < 100 && !refused; ++i)
    {
        const Status s = rig.server.Send(id, 0, ByteSpan(msg.Data(), msg.Size()), Reliability::ReliableOrdered);
        if (s.Ok())
        {
            ++accepted;
        }
        else
        {
            CHECK(s.Is(ErrorCode::LimitExceeded));
            refused = true;
        }
    }
    CHECK(refused);
    CHECK(accepted * 4096 <= 64 * 1024 + 4096);
}

}  // namespace vse::net
