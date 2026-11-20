// The UDP transport's round-trip estimate, congestion control, pacing, application cap, unreliable drop policy,
// dead-peer detection and giving up during the handshake (Phase 20 Day 276), measured on the simulated network.

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "net/sim_datagram.h"
#include "net/udp_transport.h"

namespace vse::net
{
namespace
{

Endpoint CgEp(const char* text)
{
    Endpoint e;
    REQUIRE(Endpoint::Parse(StringView(text), &e).Ok());
    return e;
}

LinkConfig CgLink(u64 latency_ms, u64 jitter_ms, f64 loss)
{
    LinkConfig l;
    l.latency_nanos = latency_ms * 1'000'000;
    l.jitter_nanos = jitter_ms * 1'000'000;
    l.loss = loss;
    return l;
}

UdpTransportConfig CgConfig()
{
    UdpTransportConfig c;
    c.seed = 5;
    c.max_message_bytes = 1u << 20;
    c.ack_delay_nanos = 1'000'000;
    return c;
}

struct CgNode
{
    CgNode(SimNetwork& net, const Endpoint& at, const UdpTransportConfig& cfg)
        : port(net, at), transport(port, net.GetClock(), cfg)
    {
    }
    SimDatagramPort port;
    UdpTransport transport;
};

// One server, one client transport that may hold several connections, on one network.
struct CgRig
{
    explicit CgRig(u64 seed, const UdpTransportConfig& server_cfg = CgConfig(),
                   const UdpTransportConfig& client_cfg = CgConfig())
        : net(seed), server_ep(CgEp("127.0.0.1:9000")), client_ep(CgEp("127.0.0.1:9001")),
          server(net, server_ep, server_cfg), client(net, client_ep, client_cfg)
    {
        REQUIRE(server.transport.Listen(server_ep).Ok());
    }
    ConnectionId Connect()
    {
        ConnectionId id = 0;
        REQUIRE(client.transport.Connect(server_ep, &id).Ok());
        client_ids.Push(id);
        return id;
    }
    // Advances one tick and collects events.
    void Step()
    {
        net.Advance(SimNetwork::kTickNanos);
        Event e;
        while (server.transport.Poll(&e))
        {
            if (e.kind == EventKind::Opened)
            {
                server_ids.Push(e.connection);
            }
            else if (e.kind == EventKind::Message)
            {
                Count(server_bytes, server_ids, e.connection, e.message.Size());
            }
            else if (e.kind == EventKind::Closed)
            {
                server_closed = true;
                server_reason = e.reason;
            }
        }
        while (client.transport.Poll(&e))
        {
            if (e.kind == EventKind::Opened)
            {
                ++client_opened;
            }
            else if (e.kind == EventKind::Message)
            {
                client_bytes += e.message.Size();
            }
            else if (e.kind == EventKind::Closed)
            {
                client_closed = true;
                client_reason = e.reason;
            }
        }
    }
    void Run(u32 ms)
    {
        for (u32 t = 0; t < ms; ++t)
        {
            Step();
        }
    }
    template <class Done> bool RunUntil(u32 ms, Done done)
    {
        for (u32 t = 0; t < ms; ++t)
        {
            Step();
            if (done())
            {
                return true;
            }
        }
        return false;
    }
    static void Count(Vector<u64>& bytes, const Vector<ConnectionId>& ids, ConnectionId id, usize n)
    {
        for (usize i = 0; i < ids.Size(); ++i)
        {
            if (ids[i] == id)
            {
                while (bytes.Size() <= i)
                {
                    bytes.Push(0);
                }
                bytes[i] += n;
            }
        }
    }
    u64 ServerBytes(usize which) const { return which < server_bytes.Size() ? server_bytes[which] : 0; }
    u64 TotalServerBytes() const
    {
        u64 n = 0;
        for (usize i = 0; i < server_bytes.Size(); ++i)
        {
            n += server_bytes[i];
        }
        return n;
    }
    // Queues `bytes` of 1000-byte reliable messages on a connection, keeping at most `ahead` messages queued.
    void Feed(ConnectionId id, u64* remaining, usize ahead = 400)
    {
        ConnectionStats st;
        while (*remaining > 0 && client.transport.Statistics(id, &st).Ok() && st.send_queue_messages < ahead)
        {
            const usize n = *remaining < 1000 ? static_cast<usize>(*remaining) : 1000;
            Vector<byte> m;
            m.Resize(n);
            for (usize i = 0; i < n; ++i)
            {
                m[i] = static_cast<byte>(i);
            }
            if (!client.transport.Send(id, 0, ByteSpan(m.Data(), n), Reliability::ReliableOrdered).Ok())
            {
                break;
            }
            *remaining -= n;
        }
    }
    SimNetwork net;
    Endpoint server_ep;
    Endpoint client_ep;
    CgNode server;
    CgNode client;
    Vector<ConnectionId> client_ids;
    Vector<ConnectionId> server_ids;
    Vector<u64> server_bytes;
    u64 client_bytes = 0;
    u32 client_opened = 0;
    bool server_closed = false;
    bool client_closed = false;
    DisconnectReason server_reason = DisconnectReason::None;
    DisconnectReason client_reason = DisconnectReason::None;
};

LinkConfig Bottleneck()
{
    LinkConfig l = CgLink(20, 0, 0);
    l.bandwidth_bps = 1'000'000;  // 125,000 bytes per second
    l.queue_bytes = 20'000;
    return l;
}

}  // namespace

TEST_CASE("UDP congestion: a bulk transfer over 1 Mbit/s and 40 ms keeps the link busy without flooding it",
          "[net][udp][congestion]")
{
    CgRig r(1);
    r.net.SetDefaultLink(Bottleneck());
    const ConnectionId c = r.Connect();
    REQUIRE(r.RunUntil(5000, [&] { return r.client_opened == 1; }));
    u64 remaining = 4'000'000;
    // Warm up, then measure what the server receives in 10 seconds.
    for (u32 t = 0; t < 8000; ++t)
    {
        r.Feed(c, &remaining);
        r.Step();
    }
    const u64 start_bytes = r.TotalServerBytes();
    const SimCounters before = r.net.Counters();
    for (u32 t = 0; t < 10000; ++t)
    {
        r.Feed(c, &remaining);
        r.Step();
    }
    const f64 rate = static_cast<f64>(r.TotalServerBytes() - start_bytes) / 10.0;
    const SimCounters after = r.net.Counters();
    CHECK(rate > 125'000 * 0.9 * (1000.0 / 1018.0));  // within 10 percent of the cap, headers excluded
    CHECK(rate <= 125'000);
    // The bottleneck queue did not grow without bound: few packets were dropped by it.
    const f64 dropped = static_cast<f64>(after.dropped_queue - before.dropped_queue);
    const f64 sent = static_cast<f64>(after.sent - before.sent);
    CHECK(dropped / sent < 0.05);
    ConnectionStats st;
    REQUIRE(r.client.transport.Statistics(c, &st).Ok());
    CHECK(st.rtt_seconds > 0.04);
    CHECK(st.rtt_seconds < 0.30);
    CHECK(st.window_bytes >= 2400);
}

TEST_CASE("UDP congestion: two connections over one bottleneck share it within a factor of two",
          "[net][udp][congestion]")
{
    CgRig r(2);
    r.net.SetDefaultLink(Bottleneck());
    const ConnectionId a = r.Connect();
    const ConnectionId b = r.Connect();
    REQUIRE(r.RunUntil(5000, [&] { return r.client_opened == 2 && r.server_ids.Size() == 2; }));
    u64 left_a = 4'000'000;
    u64 left_b = 4'000'000;
    for (u32 t = 0; t < 10000; ++t)
    {
        r.Feed(a, &left_a);
        r.Feed(b, &left_b);
        r.Step();
    }
    const u64 base_a = r.ServerBytes(0);
    const u64 base_b = r.ServerBytes(1);
    for (u32 t = 0; t < 40000; ++t)
    {
        r.Feed(a, &left_a);
        r.Feed(b, &left_b);
        r.Step();
    }
    const f64 got_a = static_cast<f64>(r.ServerBytes(0) - base_a);
    const f64 got_b = static_cast<f64>(r.ServerBytes(1) - base_b);
    REQUIRE(got_a > 0);
    REQUIRE(got_b > 0);
    CHECK(got_a / got_b < 2.0);
    CHECK(got_b / got_a < 2.0);
    CHECK((got_a + got_b) / 40.0 > 125'000 * 0.7);
}

TEST_CASE("UDP congestion: loss bursts are recovered from and the window is cut", "[net][udp][congestion]")
{
    CgRig r(3);
    LinkConfig l = CgLink(20, 2, 0);
    l.burst_start = 0.01;
    l.burst_length = 6;
    r.net.SetDefaultLink(l);
    const ConnectionId c = r.Connect();
    REQUIRE(r.RunUntil(20000, [&] { return r.client_opened == 1; }));
    u64 remaining = 1'000'000;
    const u64 total = remaining;
    REQUIRE(r.RunUntil(600000,
                       [&]
                       {
                           r.Feed(c, &remaining);
                           return r.TotalServerBytes() == total;
                       }));
    UdpConnectionInfo info;
    REQUIRE(r.client.transport.Info(c, &info).Ok());
    CHECK(info.window_cuts + info.timeouts_fired > 0);
    CHECK(info.frames_resent > 0);
}

TEST_CASE("UDP congestion: the connection survives a 5 second blackout and times out after a long one",
          "[net][udp][congestion]")
{
    {
        CgRig r(4);
        r.net.SetDefaultLink(CgLink(20, 0, 0));
        const ConnectionId c = r.Connect();
        REQUIRE(r.RunUntil(5000, [&] { return r.client_opened == 1; }));
        r.net.SetLink(r.client_ep, r.server_ep, CgLink(20, 0, 1.0));
        r.net.SetLink(r.server_ep, r.client_ep, CgLink(20, 0, 1.0));
        u64 remaining = 50'000;
        r.Feed(c, &remaining);
        r.Run(5000);
        CHECK_FALSE(r.client_closed);
        CHECK_FALSE(r.server_closed);
        r.net.SetLink(r.client_ep, r.server_ep, CgLink(20, 0, 0));
        r.net.SetLink(r.server_ep, r.client_ep, CgLink(20, 0, 0));
        REQUIRE(r.RunUntil(60000, [&] { return r.TotalServerBytes() == 50'000; }));
        CHECK_FALSE(r.client_closed);
    }
    {
        CgRig r(5);
        r.net.SetDefaultLink(CgLink(20, 0, 0));
        const ConnectionId c = r.Connect();
        REQUIRE(r.RunUntil(5000, [&] { return r.client_opened == 1; }));
        r.net.SetLink(r.client_ep, r.server_ep, CgLink(20, 0, 1.0));
        r.net.SetLink(r.server_ep, r.client_ep, CgLink(20, 0, 1.0));
        u64 remaining = 50'000;
        r.Feed(c, &remaining);
        REQUIRE(r.RunUntil(30000, [&] { return r.client_closed && r.server_closed; }));
        CHECK(r.client_reason == DisconnectReason::TimedOut);
        CHECK(r.server_reason == DisconnectReason::TimedOut);
    }
}

TEST_CASE("UDP congestion: the round-trip estimate is within 10 percent over a jittery path", "[net][udp][congestion]")
{
    CgRig r(6);
    r.net.SetDefaultLink(CgLink(25, 10, 0));  // 2 x (25 + 5 mean jitter) = 60 ms
    const ConnectionId c = r.Connect();
    REQUIRE(r.RunUntil(5000, [&] { return r.client_opened == 1; }));
    for (u32 i = 0; i < 400; ++i)
    {
        const byte m[200] = {};
        REQUIRE(r.client.transport.Send(c, 0, ByteSpan(m, 200), Reliability::ReliableOrdered).Ok());
        r.Run(50);
    }
    ConnectionStats st;
    REQUIRE(r.client.transport.Statistics(c, &st).Ok());
    CHECK(st.rtt_seconds > 0.060 * 0.9);
    CHECK(st.rtt_seconds < 0.060 * 1.1);
    CHECK(st.jitter_seconds > 0);
}

TEST_CASE("UDP congestion: the application cap holds the send rate", "[net][udp][congestion]")
{
    UdpTransportConfig cfg = CgConfig();
    cfg.max_bandwidth_bytes_per_second = 50'000;
    CgRig r(7, CgConfig(), cfg);
    r.net.SetDefaultLink(CgLink(10, 0, 0));
    const ConnectionId c = r.Connect();
    REQUIRE(r.RunUntil(5000, [&] { return r.client_opened == 1; }));
    u64 remaining = 2'000'000;
    for (u32 t = 0; t < 3000; ++t)
    {
        r.Feed(c, &remaining);
        r.Step();
    }
    const u64 base = r.TotalServerBytes();
    for (u32 t = 0; t < 10000; ++t)
    {
        r.Feed(c, &remaining);
        r.Step();
    }
    const f64 rate = static_cast<f64>(r.TotalServerBytes() - base) / 10.0;
    CHECK(rate > 50'000 * 0.9 * (1000.0 / 1018.0));
    CHECK(rate < 50'000 * 1.1);
}

TEST_CASE("UDP congestion: a full queue discards the oldest unreliable messages and delays reliable ones",
          "[net][udp][congestion]")
{
    UdpTransportConfig cfg = CgConfig();
    cfg.max_queued_frames = 50;
    CgRig r(8, CgConfig(), cfg);
    r.net.SetDefaultLink(CgLink(5, 0, 0));
    const ConnectionId c = r.Connect();
    REQUIRE(r.RunUntil(5000, [&] { return r.client_opened == 1; }));
    for (u32 i = 0; i < 200; ++i)
    {
        byte m[4] = {static_cast<byte>(i), 0, 0, 0};
        REQUIRE(r.client.transport.Send(c, 1, ByteSpan(m, 4), Reliability::Unreliable).Ok());
    }
    UdpConnectionInfo info;
    REQUIRE(r.client.transport.Info(c, &info).Ok());
    CHECK(info.dropped_unreliable == 150);
    // The newest 50 are the ones that go out.
    r.Run(500);
    CHECK(r.TotalServerBytes() == 50 * 4);
    // Reliable messages are refused, not dropped, while the queue is full.
    for (u32 i = 0; i < 50; ++i)
    {
        const byte m[4] = {};
        REQUIRE(r.client.transport.Send(c, 2, ByteSpan(m, 4), Reliability::ReliableOrdered).Ok());
    }
    const byte m[4] = {};
    CHECK(r.client.transport.Send(c, 2, ByteSpan(m, 4), Reliability::ReliableOrdered).Is(ErrorCode::LimitExceeded));
}

TEST_CASE("UDP congestion: a client that closes during the handshake frees the server's connection at once",
          "[net][udp][congestion]")
{
    CgRig r(9);
    r.net.SetDefaultLink(CgLink(10, 0, 0));
    const ConnectionId c = r.Connect();
    r.Run(25);  // the Challenge has arrived and the response is on its way; the server's Accepted will be lost
    r.net.SetLink(r.server_ep, r.client_ep, CgLink(10, 0, 1.0));
    REQUIRE(r.RunUntil(5000, [&] { return r.server_ids.Size() == 1; }));
    CHECK(r.client_opened == 0);
    REQUIRE(r.client.transport.Close(c, 7).Ok());
    REQUIRE(r.RunUntil(2000, [&] { return r.server_closed; }));
    CHECK(r.server_reason == DisconnectReason::PeerClosed);
}

TEST_CASE("UDP congestion: a Disconnect that names a handshake only closes that client's connection",
          "[net][udp][congestion]")
{
    CgRig r(10);
    r.net.SetDefaultLink(CgLink(5, 0, 0));
    r.Connect();
    REQUIRE(r.RunUntil(5000, [&] { return r.client_opened == 1 && r.server_ids.Size() == 1; }));
    // A forged Disconnect with connection id 0 and a guessed salt, from the right address.
    SimDatagramPort forger(r.net, CgEp("127.0.0.1:9002"));
    Vector<byte> p;
    p.Push(static_cast<byte>(0x26));  // Disconnect, version 1
    for (u32 i = 0; i < 14; ++i)
    {
        p.Push(static_cast<byte>(0));
    }
    CHECK(forger.SendTo(ByteSpan(p.Data(), p.Size()), r.server_ep).Ok());
    r.Run(200);
    CHECK_FALSE(r.server_closed);
    CHECK_FALSE(r.client_closed);
}

TEST_CASE("UDP congestion: the algorithm's trace is pinned", "[net][udp][congestion]")
{
    CgRig r(11);
    r.net.SetDefaultLink(Bottleneck());
    const ConnectionId c = r.Connect();
    REQUIRE(r.RunUntil(5000, [&] { return r.client_opened == 1; }));
    u64 remaining = 300'000;
    REQUIRE(r.RunUntil(60000,
                       [&]
                       {
                           r.Feed(c, &remaining);
                           return r.TotalServerBytes() == 300'000;
                       }));
    UdpConnectionInfo info;
    REQUIRE(r.client.transport.Info(c, &info).Ok());
    ConnectionStats st;
    REQUIRE(r.client.transport.Statistics(c, &st).Ok());
    INFO("window_cuts=" << info.window_cuts << " timeouts=" << info.timeouts_fired << " resent=" << info.frames_resent
                        << " packets=" << st.packets_sent << " time=" << r.net.NowNanos() / 1'000'000);
    // Slow start overshoots the 20 KB queue once; the values below are the algorithm's exact behavior on this seed.
    CHECK(info.window_cuts == 3);
    CHECK(info.timeouts_fired == 0);
    CHECK(info.frames_resent == 46);
    CHECK(st.packets_sent == 346);
    CHECK(r.net.NowNanos() / 1'000'000 == 2586);
}

TEST_CASE("UDP congestion: unreliable traffic lost in a blackout does not close the window for good",
          "[net][udp][congestion]")
{
    CgRig r(12);
    r.net.SetDefaultLink(CgLink(10, 0, 0));
    const ConnectionId c = r.Connect();
    REQUIRE(r.RunUntil(5000, [&] { return r.client_opened == 1; }));
    r.net.SetLink(r.client_ep, r.server_ep, CgLink(10, 0, 1.0));
    r.net.SetLink(r.server_ep, r.client_ep, CgLink(10, 0, 1.0));
    Vector<byte> m;
    m.Resize(1000);
    for (u32 i = 0; i < 40; ++i)
    {
        REQUIRE(r.client.transport.Send(c, 1, ByteSpan(m.Data(), m.Size()), Reliability::Unreliable).Ok());
    }
    r.Run(3000);  // everything sent is lost; no packet and no acknowledgement gets through
    r.net.SetLink(r.client_ep, r.server_ep, CgLink(10, 0, 0));
    r.net.SetLink(r.server_ep, r.client_ep, CgLink(10, 0, 0));
    r.Run(3000);
    const u64 before = r.TotalServerBytes();
    for (u32 i = 0; i < 20; ++i)
    {
        REQUIRE(r.client.transport.Send(c, 1, ByteSpan(m.Data(), m.Size()), Reliability::Unreliable).Ok());
    }
    r.Run(2000);
    CHECK(r.TotalServerBytes() - before == 20 * 1000);
}

}  // namespace vse::net
