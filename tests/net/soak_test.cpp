// The soak test (Phase 20 Day 281): 10,000 simulated connections to one server with mixed traffic, half of them closed
// by the client, the rest torn down with traffic in flight. The network is simulated, so it takes seconds, not minutes.

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "net/sim_network.h"
#include "net/sim_stream.h"
#include "net/tcp_transport.h"

namespace engine_placeholder::net
{
namespace
{

constexpr u32 kSoakConnections = 10000;

struct SoakCounts
{
    u32 opened = 0;
    u32 closed = 0;
    u32 messages = 0;
    Vector<ConnectionId> connections;
};

void SoakDrain(Transport* t, SoakCounts* c, bool echo)
{
    Event e;
    while (t->Poll(&e))
    {
        if (e.kind == EventKind::Opened)
        {
            ++c->opened;
            c->connections.Push(e.connection);
        }
        else if (e.kind == EventKind::Closed)
        {
            ++c->closed;
        }
        else if (e.kind == EventKind::Message)
        {
            ++c->messages;
            if (echo)
            {
                (void)t->Send(e.connection, e.channel, e.message, Reliability::ReliableOrdered);
            }
        }
    }
}

}  // namespace

TEST_CASE("Ten thousand simulated TCP connections carry mixed traffic and shut down with traffic in flight",
          "[net][soak][network]")
{
    SimNetwork time(11);
    SimStreamConfig stream_config;
    stream_config.latency_nanos = 2'000'000;
    stream_config.max_read_bytes = 700;  // streams cut at awkward places
    SimStreamNetwork streams(time.GetClock(), stream_config);
    const Endpoint server_ep = streams.AllocateEndpoint();
    UniquePtr<StreamProvider> server_provider = streams.MakeProvider(server_ep);
    UniquePtr<StreamProvider> client_provider = streams.MakeProvider(streams.AllocateEndpoint());

    TcpTransportConfig cfg;
    cfg.max_connections = kSoakConnections;
    cfg.max_connections_per_address = kSoakConnections;
    cfg.accept_limits = RateLimiterConfig{};  // no rate limit: this test is about volume
    cfg.max_total_send_queue_bytes = 1u << 30;
    cfg.timeout_nanos = 120'000'000'000ull;
    cfg.ping_interval_nanos = 5'000'000'000ull;

    SoakCounts server_counts;
    SoakCounts client_counts;
    {
        TcpTransport server(*server_provider, time.GetClock(), cfg);
        TcpTransport client(*client_provider, time.GetClock(), cfg);
        REQUIRE(server.Listen(server_ep).Ok());

        for (u32 i = 0; i < kSoakConnections; ++i)
        {
            ConnectionId id = 0;
            REQUIRE(client.Connect(server_ep, &id).Ok());
        }
        u32 steps = 0;
        while ((client_counts.opened < kSoakConnections || server_counts.opened < kSoakConnections) && steps < 20000)
        {
            time.Advance(1'000'000);
            ++steps;
            SoakDrain(&server, &server_counts, true);
            SoakDrain(&client, &client_counts, false);
        }
        REQUIRE(client_counts.opened == kSoakConnections);
        REQUIRE(server_counts.opened == kSoakConnections);

        // Mixed traffic: every connection sends three messages of different sizes on different channels.
        Vector<byte> payload;
        payload.Resize(900);
        for (usize i = 0; i < payload.Size(); ++i)
        {
            payload[i] = static_cast<byte>(i);
        }
        for (u32 i = 0; i < kSoakConnections; ++i)
        {
            const ConnectionId id = client_counts.connections[i];
            REQUIRE(client.Send(id, 0, ByteSpan(payload.Data(), 16), Reliability::ReliableOrdered).Ok());
            REQUIRE(client.Send(id, 1, ByteSpan(payload.Data(), 200 + (i % 7)), Reliability::ReliableOrdered).Ok());
            REQUIRE(client.Send(id, 2, ByteSpan(payload.Data(), 900), Reliability::ReliableOrdered).Ok());
        }
        steps = 0;
        while (client_counts.messages < 3 * kSoakConnections && steps < 20000)
        {
            time.Advance(1'000'000);
            ++steps;
            SoakDrain(&server, &server_counts, true);
            SoakDrain(&client, &client_counts, false);
        }
        CHECK(server_counts.messages == 3 * kSoakConnections);
        CHECK(client_counts.messages == 3 * kSoakConnections);

        // Half of them close, with a code.
        for (u32 i = 0; i < kSoakConnections; i += 2)
        {
            REQUIRE(client.Close(client_counts.connections[i], 7).Ok());
        }
        steps = 0;
        while (server_counts.closed < kSoakConnections / 2 && steps < 20000)
        {
            time.Advance(1'000'000);
            ++steps;
            SoakDrain(&server, &server_counts, true);
            SoakDrain(&client, &client_counts, false);
        }
        CHECK(server_counts.closed == kSoakConnections / 2);
        CHECK(client_counts.closed == kSoakConnections / 2);

        // The rest are torn down with traffic in flight: queue more, advance a little, destroy both transports.
        for (u32 i = 1; i < kSoakConnections; i += 2)
        {
            (void)client.Send(client_counts.connections[i], 2, ByteSpan(payload.Data(), 900),
                              Reliability::ReliableOrdered);
        }
        time.Advance(3'000'000);
        SoakDrain(&server, &server_counts, true);
        SoakDrain(&client, &client_counts, false);
    }
    // The transports are gone with every stream they owned; the sanitizer flavors check that nothing leaked.
    CHECK(server_counts.opened == kSoakConnections);
}

}  // namespace engine_placeholder::net
