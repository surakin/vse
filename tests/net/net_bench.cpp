// The network benchmarks (Phase 20 Day 281). Hidden: run with `net_tests_sockets "[net-bench]"` from a Release build.
// Each prints one line per measurement; the results are recorded in the networking design spec.

#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "net/clock.h"
#include "net/sim_network.h"
#include "net/sim_stream.h"
#include "net/socket_stream.h"
#include "net/tcp_transport.h"
#include "net/udp_port.h"
#include "net/udp_transport.h"
#include "os/console.h"
#include "sync/thread.h"

namespace engine_placeholder::net
{
namespace
{

// Opens a connection from `client` to `server` (listening at `to`), draining both until both are open.
bool BenchConnect(Transport* server, Transport* client, const Endpoint& to, const Clock& clock, ConnectionId* cc,
                  ConnectionId* sc)
{
    if (client->Connect(to, cc).Failed())
    {
        return false;
    }
    bool c_open = false;
    bool s_open = false;
    const u64 start = clock.NowNanos();
    while (!(c_open && s_open) && clock.NowNanos() - start < 5'000'000'000ull)
    {
        Event e;
        while (server->Poll(&e))
        {
            if (e.kind == EventKind::Opened)
            {
                s_open = true;
                *sc = e.connection;
            }
        }
        while (client->Poll(&e))
        {
            c_open = c_open || e.kind == EventKind::Opened;
        }
        ThisThread::Yield();
    }
    return c_open && s_open;
}

// Round trips of `size` bytes through an echo; prints p50/p99/p999.
void BenchRoundTrips(const char* name, Transport* server, Transport* client, ConnectionId cc, const Clock& clock,
                     usize size, u32 count)
{
    Vector<u64> micros;
    Vector<byte> payload;
    payload.Resize(size);
    for (u32 i = 0; i < count; ++i)
    {
        const u64 start = clock.NowNanos();
        REQUIRE(client->Send(cc, 0, ByteSpan(payload.Data(), payload.Size()), Reliability::ReliableOrdered).Ok());
        bool echoed = false;
        while (!echoed && clock.NowNanos() - start < 5'000'000'000ull)
        {
            Event e;
            while (server->Poll(&e))
            {
                if (e.kind == EventKind::Message)
                {
                    (void)server->Send(e.connection, e.channel, e.message, Reliability::ReliableOrdered);
                }
            }
            while (client->Poll(&e))
            {
                echoed = echoed || e.kind == EventKind::Message;
            }
        }
        REQUIRE(echoed);
        micros.Push((clock.NowNanos() - start) / 1000);
    }
    Sort(Span<u64>(micros.Data(), micros.Size()));
    const auto at = [&](f64 p)
    {
        usize k = static_cast<usize>(p * static_cast<f64>(micros.Size()));
        k = k >= micros.Size() ? micros.Size() - 1 : k;
        return static_cast<unsigned long long>(micros[k]);
    };
    os::Print("net-bench: %s echo %zu B: p50 %llu us, p99 %llu us, p999 %llu us\n", name, size, at(0.5), at(0.99),
              at(0.999));
}

Endpoint AnyLoopback()
{
    Endpoint any;
    REQUIRE(Endpoint::Parse(StringView("127.0.0.1:0"), &any).Ok());
    return any;
}

}  // namespace

TEST_CASE("Benchmark: loopback echo latency over UDP", "[.][net-bench]")
{
    MonotonicClock clock;
    SocketDatagramPort sp;
    SocketDatagramPort cp;
    REQUIRE(SocketDatagramPort::Open(AnyLoopback(), &sp).Ok());
    REQUIRE(SocketDatagramPort::Open(AnyLoopback(), &cp).Ok());
    UdpTransport server(sp, clock);
    UdpTransport client(cp, clock);
    REQUIRE(server.Listen(sp.LocalEndpoint()).Ok());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(BenchConnect(&server, &client, sp.LocalEndpoint(), clock, &cc, &sc));
    BenchRoundTrips("udp", &server, &client, cc, clock, 64, 20000);
    BenchRoundTrips("udp", &server, &client, cc, clock, 1024, 20000);
}

TEST_CASE("Benchmark: loopback echo latency over TCP", "[.][net-bench]")
{
    MonotonicClock clock;
    SocketStreamProvider sp;
    SocketStreamProvider cp;
    TcpTransport server(sp, clock);
    TcpTransport client(cp, clock);
    REQUIRE(server.Listen(AnyLoopback()).Ok());
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(BenchConnect(&server, &client, sp.LocalEndpoint(), clock, &cc, &sc));
    BenchRoundTrips("tcp", &server, &client, cc, clock, 64, 20000);
    BenchRoundTrips("tcp", &server, &client, cc, clock, 1024, 20000);
}

TEST_CASE("Benchmark: CPU per connection with 1000 idle-to-chatty simulated TCP connections", "[.][net-bench]")
{
    SimNetwork time(5);
    SimStreamNetwork streams(time.GetClock());
    const Endpoint server_ep = streams.AllocateEndpoint();
    UniquePtr<StreamProvider> sp = streams.MakeProvider(server_ep);
    UniquePtr<StreamProvider> cp = streams.MakeProvider(streams.AllocateEndpoint());
    TcpTransportConfig cfg;
    cfg.max_connections = 1000;
    cfg.max_connections_per_address = 1000;
    cfg.accept_limits = RateLimiterConfig{};
    TcpTransport server(*sp, time.GetClock(), cfg);
    TcpTransport client(*cp, time.GetClock(), cfg);
    REQUIRE(server.Listen(server_ep).Ok());
    Vector<ConnectionId> ids;
    for (u32 i = 0; i < 1000; ++i)
    {
        ConnectionId id = 0;
        REQUIRE(client.Connect(server_ep, &id).Ok());
    }
    u32 opened = 0;
    for (u32 step = 0; step < 5000 && opened < 1000; ++step)
    {
        time.Advance(1'000'000);
        Event e;
        while (server.Poll(&e))
        {
        }
        while (client.Poll(&e))
        {
            if (e.kind == EventKind::Opened)
            {
                ++opened;
                ids.Push(e.connection);
            }
        }
    }
    REQUIRE(opened == 1000);
    // 1000 connections, each sends one 64-byte message per simulated 100 ms for 10 simulated seconds.
    MonotonicClock wall;
    Vector<byte> payload;
    payload.Resize(64);
    u64 delivered = 0;
    const u64 t0 = wall.NowNanos();
    for (u32 step = 0; step < 10000; ++step)
    {
        if (step % 100 == 0)
        {
            for (u32 i = 0; i < ids.Size(); ++i)
            {
                (void)client.Send(ids[i], 0, ByteSpan(payload.Data(), 64), Reliability::ReliableOrdered);
            }
        }
        time.Advance(1'000'000);
        Event e;
        while (server.Poll(&e))
        {
            delivered += e.kind == EventKind::Message ? 1 : 0;
        }
        while (client.Poll(&e))
        {
        }
    }
    const u64 wall_nanos = wall.NowNanos() - t0;
    os::Print("net-bench: 1000 sim TCP connections, 10 s simulated: %llu messages, %.1f ms CPU (%.2f%% of one core of "
              "real time at 10 s), %.1f us per connection-second\n",
              static_cast<unsigned long long>(delivered), static_cast<f64>(wall_nanos) / 1e6,
              static_cast<f64>(wall_nanos) / 1e9 / 10.0 * 100.0, static_cast<f64>(wall_nanos) / 1e3 / 1000.0 / 10.0);
}

TEST_CASE("Benchmark: simulated TCP throughput", "[.][net-bench]")
{
    SimNetwork time(6);
    SimStreamNetwork streams(time.GetClock());
    const Endpoint server_ep = streams.AllocateEndpoint();
    UniquePtr<StreamProvider> sp = streams.MakeProvider(server_ep);
    UniquePtr<StreamProvider> cp = streams.MakeProvider(streams.AllocateEndpoint());
    TcpTransport server(*sp, time.GetClock());
    TcpTransport client(*cp, time.GetClock());
    REQUIRE(server.Listen(server_ep).Ok());
    ConnectionId cc = 0;
    {
        REQUIRE(client.Connect(server_ep, &cc).Ok());
        bool c = false;
        bool s = false;
        for (u32 i = 0; i < 1000 && !(c && s); ++i)
        {
            time.Advance(1'000'000);
            Event e;
            while (server.Poll(&e))
            {
                s = s || e.kind == EventKind::Opened;
            }
            while (client.Poll(&e))
            {
                c = c || e.kind == EventKind::Opened;
            }
        }
        REQUIRE((c && s));
    }
    MonotonicClock wall;
    Vector<byte> payload;
    payload.Resize(16384);
    usize received = 0;
    const usize target = 256u << 20;
    const u64 t0 = wall.NowNanos();
    usize sent = 0;
    while (received < target)
    {
        if (sent < target &&
            client.Send(cc, 0, ByteSpan(payload.Data(), payload.Size()), Reliability::ReliableOrdered).Ok())
        {
            sent += payload.Size();
        }
        time.Advance(1'000'000);
        Event e;
        while (server.Poll(&e))
        {
            received += e.kind == EventKind::Message ? e.message.Size() : 0;
        }
        while (client.Poll(&e))
        {
        }
    }
    const f64 seconds = static_cast<f64>(wall.NowNanos() - t0) / 1e9;
    os::Print("net-bench: simulated TCP, 16 KiB messages: %.0f MiB/s of wall time\n",
              static_cast<f64>(received) / (1024.0 * 1024.0) / seconds);
}

}  // namespace engine_placeholder::net
