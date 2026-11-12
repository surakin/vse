// The UDP transport's packet layer and handshake (Phase 20 Day 274): wire helpers, the cookie handshake under loss,
// spoofed floods, replays, capacity limits, sequence wraparound, and what happens when a peer disappears.

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "net/sim_datagram.h"
#include "net/udp_transport.h"

namespace engine_placeholder::net
{
namespace
{

Endpoint UEp(const char* text)
{
    Endpoint e;
    if (Endpoint::Parse(StringView(text), &e).Failed())
    {
        // The literals in this file are valid; a test that checked would fail at static initialization.
    }
    return e;
}

// A port that records what the transport sends and can hold back one kind of packet.
class RecordingPort final : public DatagramPort
{
public:
    RecordingPort(SimNetwork& net, const Endpoint& local) : inner_(net, local) {}
    Status SendTo(ByteSpan bytes, const Endpoint& to) override
    {
        Vector<byte> copy;
        for (usize i = 0; i < bytes.Size(); ++i)
        {
            copy.Push(bytes[i]);
        }
        sent.Push(Move(copy));
        if (hold_kind >= 0 && bytes.Size() > 0 && (static_cast<u8>(bytes[0]) & 0x1f) == static_cast<u8>(hold_kind))
        {
            Vector<byte> again;
            for (usize i = 0; i < bytes.Size(); ++i)
            {
                again.Push(bytes[i]);
            }
            held.Push(Move(again));
            return Status::Success();
        }
        return inner_.SendTo(bytes, to);
    }
    Status ReceiveFrom(MutableByteSpan buffer, usize* received, Endpoint* from) override
    {
        return inner_.ReceiveFrom(buffer, received, from);
    }
    Endpoint LocalEndpoint() const override { return inner_.LocalEndpoint(); }

    Vector<Vector<byte>> sent;
    Vector<Vector<byte>> held;
    int hold_kind = -1;

private:
    SimDatagramPort inner_;
};

LinkConfig Link(f64 loss, f64 reorder, f64 duplicate)
{
    LinkConfig l;
    l.latency_nanos = 10'000'000;
    l.jitter_nanos = 5'000'000;
    l.loss = loss;
    l.reorder = reorder;
    l.duplicate = duplicate;
    return l;
}

struct Rec
{
    EventKind kind;
    ConnectionId connection;
    bool incoming;
    DisconnectReason reason;
    u32 code;
    u8 channel;
    Vector<byte> data;
};

struct Side
{
    Side(SimNetwork& net, const Endpoint& at, const UdpTransportConfig& cfg)
        : port(net, at), transport(port, net.GetClock(), cfg)
    {
    }
    void Drain()
    {
        Event e;
        while (transport.Poll(&e))
        {
            Rec r;
            r.kind = e.kind;
            r.connection = e.connection;
            r.incoming = e.incoming;
            r.reason = e.reason;
            r.code = e.close_code;
            r.channel = e.channel;
            for (usize i = 0; i < e.message.Size(); ++i)
            {
                r.data.Push(e.message[i]);
            }
            events.Push(Move(r));
        }
    }
    usize Count(EventKind k) const
    {
        usize n = 0;
        for (usize i = 0; i < events.Size(); ++i)
        {
            n += events[i].kind == k ? 1 : 0;
        }
        return n;
    }
    const Rec* First(EventKind k) const
    {
        for (usize i = 0; i < events.Size(); ++i)
        {
            if (events[i].kind == k)
            {
                return &events[i];
            }
        }
        return nullptr;
    }
    RecordingPort port;
    UdpTransport transport;
    Vector<Rec> events;
};

void Run(SimNetwork& net, u32 ms, Side* a, Side* b = nullptr, Side* c = nullptr)
{
    for (u32 t = 0; t < ms; t += 5)
    {
        net.Advance(5'000'000);
        a->Drain();
        if (b != nullptr)
        {
            b->Drain();
        }
        if (c != nullptr)
        {
            c->Drain();
        }
    }
}

UdpTransportConfig TestConfig()
{
    UdpTransportConfig c;
    c.seed = 77;
    return c;
}

const Endpoint kServer = UEp("127.0.0.1:9000");

}  // namespace

TEST_CASE("UDP protocol: SipHash-2-4 matches the published vectors", "[net][udp]")
{
    const u64 k0 = 0x0706050403020100ull;
    const u64 k1 = 0x0f0e0d0c0b0a0908ull;
    CHECK(udp::SipHash24(k0, k1, ByteSpan()) == 0x726fdb47dd0e0e31ull);
    const byte one[1] = {static_cast<byte>(0)};
    CHECK(udp::SipHash24(k0, k1, ByteSpan(one, 1)) == 0x74f839c593dc67fdull);
    // 15 bytes 00..0e: the last vector of the paper's table.
    byte fifteen[15];
    for (u32 i = 0; i < 15; ++i)
    {
        fifteen[i] = static_cast<byte>(i);
    }
    CHECK(udp::SipHash24(k0, k1, ByteSpan(fifteen, 15)) == 0xa129ca6149be45e5ull);
}

TEST_CASE("UDP protocol: sequence comparison and the receive window across wraparound", "[net][udp]")
{
    CHECK(udp::SeqNewer(1, 0));
    CHECK(udp::SeqNewer(0, 65535));
    CHECK(udp::SeqNewer(5, 65530));
    CHECK_FALSE(udp::SeqNewer(65530, 5));
    CHECK_FALSE(udp::SeqNewer(7, 7));
    CHECK(udp::SeqDistance(3, 65534) == 5);

    udp::ReceiveWindow w;
    CHECK(w.OnReceive(65534) == udp::SeqVerdict::Fresh);
    CHECK(w.OnReceive(1) == udp::SeqVerdict::Fresh);  // 65535 and 0 skipped
    CHECK(w.Latest() == 1);
    CHECK(w.OnReceive(0) == udp::SeqVerdict::Fresh);  // reordered across the boundary
    CHECK(w.OnReceive(0) == udp::SeqVerdict::Duplicate);
    CHECK(w.OnReceive(65534) == udp::SeqVerdict::Duplicate);
    CHECK(w.OnReceive(65535) == udp::SeqVerdict::Fresh);
    CHECK(w.OnReceive(1) == udp::SeqVerdict::Duplicate);
    CHECK((w.Bits() & 0x7) == 0x7);  // 0, 65535, 65534 all seen
    CHECK(w.OnReceive(40) == udp::SeqVerdict::Fresh);
    CHECK(w.OnReceive(65534) == udp::SeqVerdict::TooOld);  // 42 behind
    CHECK(w.OnReceive(9) == udp::SeqVerdict::Fresh);       // 31 behind, never seen
    CHECK(w.OnReceive(8) == udp::SeqVerdict::Fresh);       // 32 behind: the last bit of the window
    CHECK(w.OnReceive(7) == udp::SeqVerdict::TooOld);      // 33 behind
}

TEST_CASE("UDP protocol: the send log reports each acknowledged packet once", "[net][udp]")
{
    udp::SendLog log;
    for (u16 s = 65530; s != 6; ++s)
    {
        log.OnSent(s);
    }
    Vector<u16> newly;
    // The peer saw 2 and (bits) 1, 0, 65535: bit0 = 1, bit1 = 0, bit2 = 65535.
    CHECK(log.OnAck(2, 0b111, &newly) == 4);
    CHECK(newly.Size() == 4);
    CHECK(log.OnAck(2, 0b111, &newly) == 0);  // already acknowledged
    CHECK(log.OnAck(100, 0, &newly) == 0);    // never sent
}

TEST_CASE("UDP transport: connect, exchange messages, acknowledgements flow", "[net][udp]")
{
    SimNetwork net(11);
    net.SetDefaultLink(Link(0, 0, 0));
    Side server(net, kServer, TestConfig());
    Side client(net, UEp("127.0.0.1:20001"), TestConfig());
    REQUIRE(server.transport.Listen(kServer).Ok());
    ConnectionId cc = 0;
    REQUIRE(client.transport.Connect(kServer, &cc).Ok());
    Run(net, 300, &server, &client);
    REQUIRE(client.Count(EventKind::Opened) == 1);
    REQUIRE(server.Count(EventKind::Opened) == 1);
    CHECK(server.First(EventKind::Opened)->incoming);
    CHECK_FALSE(client.First(EventKind::Opened)->incoming);
    const ConnectionId sc = server.First(EventKind::Opened)->connection;
    const byte msg[3] = {static_cast<byte>(1), static_cast<byte>(2), static_cast<byte>(3)};
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE(client.transport.Send(cc, 2, ByteSpan(msg, 3), Reliability::Unreliable).Ok());
        REQUIRE(server.transport.Send(sc, 5, ByteSpan(msg, 2), Reliability::Unreliable).Ok());
        Run(net, 20, &server, &client);
    }
    Run(net, 200, &server, &client);
    CHECK(server.Count(EventKind::Message) == 10);
    CHECK(client.Count(EventKind::Message) == 10);
    CHECK(server.First(EventKind::Message)->channel == 2);
    CHECK(client.First(EventKind::Message)->channel == 5);
    UdpConnectionInfo info;
    REQUIRE(client.transport.Info(cc, &info).Ok());
    CHECK(info.connected);
    CHECK(info.packets_acked >= 10);
    CHECK(info.duplicates == 0);
    // Reliable messages are queued; sending before the handshake is refused.
    CHECK(client.transport.Send(cc, 0, ByteSpan(msg, 3), Reliability::ReliableOrdered).Ok());
    ConnectionId c2 = 0;
    REQUIRE(client.transport.Connect(kServer, &c2).Ok());
    CHECK(client.transport.Send(c2, 0, ByteSpan(msg, 3), Reliability::Unreliable).Is(ErrorCode::NotConnected));
}

TEST_CASE("UDP transport: the handshake completes under 0 to 60 percent loss with reordering and duplication",
          "[net][udp]")
{
    const f64 losses[] = {0.0, 0.2, 0.4, 0.6};
    for (u32 k = 0; k < 4; ++k)
    {
        for (u64 seed = 1; seed <= 5; ++seed)
        {
            SimNetwork net(seed * 100 + k);
            net.SetDefaultLink(Link(losses[k], 0.1, 0.1));
            UdpTransportConfig cfg = TestConfig();
            cfg.connect_timeout_nanos = 120'000'000'000ull;
            cfg.timeout_nanos = 120'000'000'000ull;
            Side server(net, kServer, cfg);
            Side client(net, UEp("127.0.0.1:20001"), cfg);
            REQUIRE(server.transport.Listen(kServer).Ok());
            ConnectionId cc = 0;
            REQUIRE(client.transport.Connect(kServer, &cc).Ok());
            Run(net, 100000, &server, &client);
            INFO("loss " << losses[k] << " seed " << seed);
            CHECK(client.Count(EventKind::Opened) == 1);
            CHECK(server.Count(EventKind::Opened) == 1);  // duplicates and repeats make one connection
            CHECK(client.Count(EventKind::Closed) == 0);
            CHECK(server.transport.ConnectionCount() == 1);
        }
    }
}

TEST_CASE("UDP transport: spoofed hello floods leave the server without state and never amplify", "[net][udp]")
{
    class Victim final : public PacketSink
    {
    public:
        void OnPacket(const Endpoint&, ByteSpan packet) override
        {
            bytes += packet.Size();
            ++packets;
        }
        void OnTick(u64) override {}
        u64 bytes = 0;
        u64 packets = 0;
    };
    SimNetwork net(21);
    net.SetDefaultLink(Link(0, 0, 0));
    Side server(net, kServer, TestConfig());
    REQUIRE(server.transport.Listen(kServer).Ok());
    Victim victim;
    const Endpoint victim_at = UEp("127.0.0.1:31000");
    REQUIRE(net.Attach(victim_at, &victim).Ok());
    // Full-size requests and responses from the victim's address (spoofed) and from thousands of addresses nobody owns.
    Vector<byte> request;
    request.Push(static_cast<byte>(udp::FirstByte(udp::PacketKind::ConnectRequest)));
    for (u32 i = 0; i < 8; ++i)
    {
        request.Push(static_cast<byte>(i));
    }
    while (request.Size() < udp::kHandshakeBytes)
    {
        request.Push(static_cast<byte>(0));
    }
    Vector<byte> response;
    for (usize i = 0; i < request.Size(); ++i)
    {
        response.Push(request[i]);
    }
    response[0] = static_cast<byte>(udp::FirstByte(udp::PacketKind::ChallengeResponse));
    u64 sent_bytes = 0;
    for (u32 i = 0; i < 10000; ++i)
    {
        const Endpoint from = i % 2 == 0
                                  ? victim_at
                                  : Endpoint(Address::Loopback(AddressFamily::V4), static_cast<u16>(32000 + i % 20000));
        net.Transmit(from, kServer, ByteSpan(request.Data(), request.Size()));
        net.Transmit(from, kServer, ByteSpan(response.Data(), response.Size()));
        sent_bytes += request.Size() + response.Size();
        if (i % 100 == 99)
        {
            Run(net, 5, &server);
        }
    }
    Run(net, 50, &server);
    CHECK(server.transport.ConnectionCount() == 0);
    CHECK(server.Count(EventKind::Opened) == 0);
    CHECK(server.transport.Counters().challenges_sent == 10000);
    CHECK(server.transport.Counters().dropped_bad_cookie == 10000);
    CHECK(victim.bytes < sent_bytes / 20);  // far less than the flood that forged its address
    // A request that is not full-size gets no answer at all.
    const u64 before = server.transport.Counters().challenges_sent;
    net.Transmit(victim_at, kServer, ByteSpan(request.Data(), 600));
    Run(net, 50, &server);
    CHECK(server.transport.Counters().challenges_sent == before);
    CHECK(server.transport.Counters().dropped_short >= 1);
}

TEST_CASE("UDP transport: garbage, wrong versions and oversize datagrams are dropped and counted", "[net][udp]")
{
    SimNetwork net(31);
    LinkConfig l = Link(0, 0, 0);
    l.mtu = 4000;
    net.SetDefaultLink(l);
    Side server(net, kServer, TestConfig());
    REQUIRE(server.transport.Listen(kServer).Ok());
    const Endpoint from = UEp("127.0.0.1:30000");
    const byte wrong_version[20] = {static_cast<byte>(0xe0)};
    net.Transmit(from, kServer, ByteSpan(wrong_version, 20));
    const byte unknown_kind[20] = {static_cast<byte>((udp::kVersion << 5) | 31)};
    net.Transmit(from, kServer, ByteSpan(unknown_kind, 20));
    Vector<byte> big;
    big.Resize(3000);
    net.Transmit(from, kServer, ByteSpan(big.Data(), big.Size()));
    const byte short_payload[4] = {static_cast<byte>(udp::FirstByte(udp::PacketKind::Payload))};
    net.Transmit(from, kServer, ByteSpan(short_payload, 4));
    net.Transmit(from, kServer, ByteSpan());
    Run(net, 50, &server);
    const UdpCounters& c = server.transport.Counters();
    CHECK(c.dropped_unknown >= 3);  // the wrong version, the unknown kind, the empty datagram
    CHECK(c.dropped_oversize == 1);
    CHECK(c.dropped_short == 1);
    CHECK(server.transport.ConnectionCount() == 0);
}

TEST_CASE("UDP transport: a replayed or stale handshake is rejected", "[net][udp]")
{
    SimNetwork net(41);
    net.SetDefaultLink(Link(0, 0, 0));
    UdpTransportConfig cfg = TestConfig();
    cfg.cookie_period_nanos = 60'000'000'000ull;
    Side server(net, kServer, cfg);
    Side client(net, UEp("127.0.0.1:20001"), cfg);
    REQUIRE(server.transport.Listen(kServer).Ok());
    ConnectionId cc = 0;
    REQUIRE(client.transport.Connect(kServer, &cc).Ok());
    Run(net, 300, &server, &client);
    REQUIRE(server.Count(EventKind::Opened) == 1);
    // The recorded response, replayed while the connection lives: answered again, no second connection.
    const Vector<byte>* response = nullptr;
    for (usize i = 0; i < client.port.sent.Size(); ++i)
    {
        if ((static_cast<u8>(client.port.sent[i][0]) & 0x1f) == static_cast<u8>(udp::PacketKind::ChallengeResponse))
        {
            response = &client.port.sent[i];
        }
    }
    REQUIRE(response != nullptr);
    net.Transmit(UEp("127.0.0.1:20001"), kServer, ByteSpan(response->Data(), response->Size()));
    Run(net, 50, &server, &client);
    CHECK(server.transport.ConnectionCount() == 1);
    CHECK(server.Count(EventKind::Opened) == 1);
    // After the connection ended and was reported, the same response is a stale replay.
    const ConnectionId sc = server.First(EventKind::Opened)->connection;
    REQUIRE(server.transport.Close(sc, 0).Ok());
    Run(net, 100, &server, &client);
    REQUIRE(server.transport.ConnectionCount() == 0);
    // Many other handshakes come and go: the recorded one is still remembered while its cookie lives.
    for (u32 i = 0; i < 70; ++i)
    {
        Side other(net, Endpoint(Address::V4(127, 0, static_cast<u8>(1 + i), 1), 21000), cfg);
        ConnectionId oc = 0;
        REQUIRE(other.transport.Connect(kServer, &oc).Ok());
        Run(net, 150, &server, &other);
        REQUIRE(other.transport.Close(oc, 0).Ok());
        Run(net, 60, &server, &other);
    }
    const usize opened_before = server.Count(EventKind::Opened);
    net.Transmit(UEp("127.0.0.1:20001"), kServer, ByteSpan(response->Data(), response->Size()));
    Run(net, 50, &server, &client);
    CHECK(server.transport.ConnectionCount() == 0);
    CHECK(server.Count(EventKind::Opened) == opened_before);  // the others came and went; the replay added none
    CHECK(server.transport.Counters().dropped_stale >= 1);
}

TEST_CASE("UDP transport: a cookie older than two periods is refused", "[net][udp]")
{
    SimNetwork net(51);
    net.SetDefaultLink(Link(0, 0, 0));
    UdpTransportConfig cfg = TestConfig();
    cfg.cookie_period_nanos = 2'000'000'000ull;
    cfg.connect_timeout_nanos = 60'000'000'000ull;
    Side server(net, kServer, cfg);
    Side client(net, UEp("127.0.0.1:20001"), cfg);
    REQUIRE(server.transport.Listen(kServer).Ok());
    client.port.hold_kind = static_cast<int>(udp::PacketKind::ChallengeResponse);
    ConnectionId cc = 0;
    REQUIRE(client.transport.Connect(kServer, &cc).Ok());
    Run(net, 500, &server, &client);
    REQUIRE(client.port.held.Size() >= 1);
    Run(net, 6000, &server, &client);  // three periods pass; the client keeps repeating, all held
    client.port.hold_kind = -1;
    const Vector<byte>& old = client.port.held[0];
    net.Transmit(UEp("127.0.0.1:20001"), kServer, ByteSpan(old.Data(), old.Size()));
    Run(net, 50, &server, &client);
    CHECK(server.Count(EventKind::Opened) == 0);
    CHECK(server.transport.Counters().dropped_bad_cookie >= 1);
    // The client's own later repeats carry the same old cookie; a new request obtains a fresh one and succeeds.
    ConnectionId c2 = 0;
    REQUIRE(client.transport.Connect(kServer, &c2).Ok());
    Run(net, 500, &server, &client);
    CHECK(server.Count(EventKind::Opened) == 1);
}

TEST_CASE("UDP transport: sequence numbers wrap at 65535 with reordering and duplication", "[net][udp]")
{
    SimNetwork net(61);
    net.SetDefaultLink(Link(0.05, 0.25, 0.2));
    UdpTransportConfig cfg = TestConfig();
    cfg.initial_sequence = 65480;
    cfg.timeout_nanos = 60'000'000'000ull;
    Side server(net, kServer, cfg);
    Side client(net, UEp("127.0.0.1:20001"), cfg);
    REQUIRE(server.transport.Listen(kServer).Ok());
    ConnectionId cc = 0;
    REQUIRE(client.transport.Connect(kServer, &cc).Ok());
    Run(net, 3000, &server, &client);
    REQUIRE(client.Count(EventKind::Opened) == 1);
    for (u32 i = 0; i < 200; ++i)
    {
        byte m[4];
        for (u32 b = 0; b < 4; ++b)
        {
            m[b] = static_cast<byte>((i >> (8 * b)) & 0xff);
        }
        REQUIRE(client.transport.Send(cc, 0, ByteSpan(m, 4), Reliability::Unreliable).Ok());
        Run(net, 10, &server, &client);
    }
    Run(net, 2000, &server, &client);
    // Every message at most once, none invented.
    Vector<u8> seen;
    seen.Resize(200);
    bool good = true;
    usize delivered = 0;
    for (usize i = 0; i < server.events.Size(); ++i)
    {
        if (server.events[i].kind != EventKind::Message)
        {
            continue;
        }
        u32 index = 0;
        for (u32 b = 0; b < 4; ++b)
        {
            index |= static_cast<u32>(static_cast<u8>(server.events[i].data[b])) << (8 * b);
        }
        good = good && index < 200 && seen[index] == 0;
        if (index < 200)
        {
            seen[index] = 1;
        }
        ++delivered;
    }
    CHECK(good);
    CHECK(delivered > 100);
    UdpConnectionInfo info;
    REQUIRE(client.transport.Info(cc, &info).Ok());
    CHECK(info.next_sequence < 65480);  // wrapped
    UdpConnectionInfo sinfo;
    REQUIRE(server.transport.Info(server.First(EventKind::Opened)->connection, &sinfo).Ok());
    CHECK(sinfo.duplicates > 0);  // the link duplicated packets; the window caught them
    CHECK(info.packets_acked > 100);
}

TEST_CASE("UDP transport: capacity and per-address limits deny a connection", "[net][udp]")
{
    SimNetwork net(71);
    net.SetDefaultLink(Link(0, 0, 0));
    UdpTransportConfig cfg = TestConfig();
    cfg.max_connections = 3;
    cfg.max_connections_per_address = 2;
    Side server(net, kServer, cfg);
    REQUIRE(server.transport.Listen(kServer).Ok());
    Vector<UniquePtr<Side>> clients;
    for (u32 i = 0; i < 4; ++i)
    {
        clients.Push(MakeUnique<Side>(net, Endpoint(Address::Loopback(AddressFamily::V4), static_cast<u16>(20001 + i)),
                                      TestConfig()));
        ConnectionId id = 0;
        REQUIRE(clients[i]->transport.Connect(kServer, &id).Ok());
        Run(net, 300, &server, clients[i].Get());
    }
    // All four come from one address (127.0.0.1): two are accepted, two denied by the address limit.
    u32 opened = 0;
    u32 refused = 0;
    for (usize i = 0; i < clients.Size(); ++i)
    {
        opened += static_cast<u32>(clients[i]->Count(EventKind::Opened));
        refused += clients[i]->Count(EventKind::Closed) == 1 &&
                           clients[i]->First(EventKind::Closed)->reason == DisconnectReason::Refused
                       ? 1
                       : 0;
    }
    CHECK(opened == 2);
    CHECK(refused == 2);
    CHECK(server.transport.ConnectionCount() == 2);
}

TEST_CASE("UDP transport: close carries its code; a restarted server resets stale clients; silence times out",
          "[net][udp]")
{
    SimNetwork net(81);
    net.SetDefaultLink(Link(0, 0, 0));
    {
        Side server(net, kServer, TestConfig());
        Side client(net, UEp("127.0.0.1:20001"), TestConfig());
        REQUIRE(server.transport.Listen(kServer).Ok());
        ConnectionId cc = 0;
        REQUIRE(client.transport.Connect(kServer, &cc).Ok());
        Run(net, 300, &server, &client);
        REQUIRE(client.transport.Close(cc, 42).Ok());
        Run(net, 300, &server, &client);
        REQUIRE(server.Count(EventKind::Closed) == 1);
        CHECK(server.First(EventKind::Closed)->reason == DisconnectReason::PeerClosed);
        CHECK(server.First(EventKind::Closed)->code == 42);
        CHECK(client.First(EventKind::Closed)->reason == DisconnectReason::LocalClose);
    }
    {
        // A server that lost its state answers a payload with Reset.
        Side client(net, UEp("127.0.0.1:20002"), TestConfig());
        ConnectionId cc = 0;
        {
            Side server(net, kServer, TestConfig());
            REQUIRE(server.transport.Listen(kServer).Ok());
            REQUIRE(client.transport.Connect(kServer, &cc).Ok());
            Run(net, 300, &server, &client);
            REQUIRE(client.Count(EventKind::Opened) == 1);
        }
        Side reborn(net, kServer, TestConfig());
        REQUIRE(reborn.transport.Listen(kServer).Ok());
        const byte m[1] = {static_cast<byte>(1)};
        REQUIRE(client.transport.Send(cc, 0, ByteSpan(m, 1), Reliability::Unreliable).Ok());
        Run(net, 100, &reborn, &client);
        REQUIRE(client.Count(EventKind::Closed) == 1);
        CHECK(client.First(EventKind::Closed)->reason == DisconnectReason::Reset);
        CHECK(reborn.transport.Counters().resets_sent == 1);
    }
    {
        // A client that vanishes without a word: the server times out, and a connect to nothing times out too.
        Side server(net, kServer, TestConfig());
        REQUIRE(server.transport.Listen(kServer).Ok());
        {
            Side client(net, UEp("127.0.0.1:20003"), TestConfig());
            ConnectionId cc = 0;
            REQUIRE(client.transport.Connect(kServer, &cc).Ok());
            Run(net, 300, &server, &client);
        }
        Run(net, 11000, &server);
        REQUIRE(server.Count(EventKind::Closed) == 1);
        CHECK(server.First(EventKind::Closed)->reason == DisconnectReason::TimedOut);
        Side lonely(net, UEp("127.0.0.1:20004"), TestConfig());
        ConnectionId cc = 0;
        REQUIRE(lonely.transport.Connect(UEp("127.0.0.1:9999"), &cc).Ok());
        Run(net, 6000, &lonely);
        REQUIRE(lonely.Count(EventKind::Closed) == 1);
        CHECK(lonely.First(EventKind::Closed)->reason == DisconnectReason::TimedOut);
    }
}

TEST_CASE("UDP transport: a peer that has received nothing acknowledges nothing", "[net][udp]")
{
    SimNetwork net(91);
    net.SetDefaultLink(Link(0, 0, 0));
    Side server(net, kServer, TestConfig());
    Side client(net, UEp("127.0.0.1:20001"), TestConfig());
    REQUIRE(server.transport.Listen(kServer).Ok());
    ConnectionId cc = 0;
    REQUIRE(client.transport.Connect(kServer, &cc).Ok());
    Run(net, 100, &server, &client);
    REQUIRE(server.Count(EventKind::Opened) == 1);
    const ConnectionId sc = server.First(EventKind::Opened)->connection;
    // Everything the server sends from now on is lost; the client therefore has received no payload at all.
    net.SetLink(kServer, UEp("127.0.0.1:20001"),
                []
                {
                    LinkConfig l;
                    l.loss = 1.0;
                    return l;
                }());
    const byte m[2] = {static_cast<byte>(1), static_cast<byte>(2)};
    REQUIRE(server.transport.Send(sc, 0, ByteSpan(m, 2), Reliability::Unreliable).Ok());  // the server's sequence 0
    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(client.transport.Send(cc, 0, ByteSpan(m, 2), Reliability::Unreliable).Ok());
        Run(net, 20, &server, &client);
    }
    Run(net, 100, &server, &client);
    UdpConnectionInfo info;
    REQUIRE(server.transport.Info(sc, &info).Ok());
    CHECK(info.packets_acked == 0);
}

TEST_CASE("UDP transport: payloads for a connection being closed are not answered with a Reset", "[net][udp]")
{
    SimNetwork net(92);
    net.SetDefaultLink(Link(0, 0, 0));
    Side server(net, kServer, TestConfig());
    Side client(net, UEp("127.0.0.1:20001"), TestConfig());
    REQUIRE(server.transport.Listen(kServer).Ok());
    ConnectionId cc = 0;
    REQUIRE(client.transport.Connect(kServer, &cc).Ok());
    Run(net, 100, &server, &client);
    const ConnectionId sc = server.First(EventKind::Opened)->connection;
    net.SetLink(kServer, UEp("127.0.0.1:20001"),
                []
                {
                    LinkConfig l;
                    l.loss = 1.0;
                    return l;
                }());
    REQUIRE(server.transport.Close(sc, 5).Ok());
    server.Drain();  // reports the close and removes the connection; its Disconnect packets are still being repeated
    const byte m[1] = {static_cast<byte>(1)};
    for (int i = 0; i < 3; ++i)
    {
        REQUIRE(client.transport.Send(cc, 0, ByteSpan(m, 1), Reliability::Unreliable).Ok());
        Run(net, 10, &server, &client);
    }
    CHECK(server.transport.Counters().resets_sent == 0);
}

TEST_CASE("UDP transport: an address that churns handshakes is denied alone", "[net][udp]")
{
    SimNetwork net(93);
    net.SetDefaultLink(Link(0, 0, 0));
    UdpTransportConfig cfg = TestConfig();
    cfg.cookie_period_nanos = 600'000'000'000ull;
    Side server(net, kServer, cfg);
    REQUIRE(server.transport.Listen(kServer).Ok());
    // One address connects and disconnects over and over until the server stops taking it.
    u32 accepted = 0;
    u32 denied = 0;
    for (u32 i = 0; i < 80; ++i)
    {
        Side churn(net, Endpoint(Address::V4(10, 0, 0, 1), static_cast<u16>(22000 + i)), cfg);
        ConnectionId id = 0;
        REQUIRE(churn.transport.Connect(kServer, &id).Ok());
        Run(net, 150, &server, &churn);
        if (churn.Count(EventKind::Opened) == 1)
        {
            ++accepted;
            REQUIRE(churn.transport.Close(id, 0).Ok());
            Run(net, 60, &server, &churn);
        }
        else if (churn.Count(EventKind::Closed) == 1 &&
                 churn.First(EventKind::Closed)->reason == DisconnectReason::Refused)
        {
            ++denied;
        }
    }
    CHECK(accepted == 64);
    CHECK(denied == 16);
    // Another address is not affected.
    Side other(net, Endpoint(Address::V4(10, 0, 0, 2), 22000), cfg);
    ConnectionId id = 0;
    REQUIRE(other.transport.Connect(kServer, &id).Ok());
    Run(net, 150, &server, &other);
    CHECK(other.Count(EventKind::Opened) == 1);
}

}  // namespace engine_placeholder::net
