// The TCP transport over real loopback sockets (Phase 20 Day 277). Labelled `network`.

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "net/socket_stream.h"
#include "net/tcp_transport.h"
#include "sync/thread.h"

namespace vse::net
{

TEST_CASE("TCP transport: messages, a large message and a close over loopback sockets", "[net][tcp][network]")
{
    Endpoint any;
    REQUIRE(Endpoint::Parse(StringView("127.0.0.1:0"), &any).Ok());
    SocketStreamProvider server_provider;
    SocketStreamProvider client_provider;
    MonotonicClock clock;
    TcpTransport server(server_provider, clock);
    TcpTransport client(client_provider, clock);
    if (server.Listen(any).Failed())
    {
        SKIP("cannot bind a loopback TCP socket here");
    }
    ConnectionId cc = 0;
    REQUIRE(client.Connect(server_provider.LocalEndpoint(), &cc).Ok());
    Vector<byte> big;
    for (usize i = 0; i < (1u << 20); ++i)
    {
        big.Push(static_cast<byte>(i * 31 + (i >> 8)));
    }
    bool client_open = false;
    bool server_open = false;
    bool sent = false;
    bool closed = false;
    ConnectionId sc = 0;
    u32 small_got = 0;
    bool big_ok = false;
    u32 client_got = 0;
    DisconnectReason reason = DisconnectReason::None;
    u32 code = 0;
    const u64 start = clock.NowNanos();
    while (clock.NowNanos() - start < 10'000'000'000ull && !closed)
    {
        Event e;
        while (server.Poll(&e))
        {
            if (e.kind == EventKind::Opened)
            {
                server_open = true;
                sc = e.connection;
            }
            else if (e.kind == EventKind::Message)
            {
                if (e.message.Size() == big.Size())
                {
                    big_ok = MemCompare(e.message.Data(), big.Data(), big.Size()) == 0;
                }
                else
                {
                    ++small_got;
                }
            }
        }
        while (client.Poll(&e))
        {
            client_open = client_open || e.kind == EventKind::Opened;
            client_got += e.kind == EventKind::Message ? 1 : 0;
            if (e.kind == EventKind::Closed)
            {
                closed = true;
                reason = e.reason;
                code = e.close_code;
            }
        }
        if (client_open && server_open && !sent)
        {
            sent = true;
            const byte m[4] = {static_cast<byte>(1), static_cast<byte>(2), static_cast<byte>(3), static_cast<byte>(4)};
            for (int i = 0; i < 5; ++i)
            {
                REQUIRE(client.Send(cc, 0, ByteSpan(m, 4), Reliability::ReliableOrdered).Ok());
            }
            REQUIRE(client.Send(cc, 1, ByteSpan(big.Data(), big.Size()), Reliability::ReliableOrdered).Ok());
            REQUIRE(server.Send(sc, 1, ByteSpan(m, 2), Reliability::ReliableUnordered).Ok());
        }
        if (sent && small_got == 5 && big_ok && client_got == 1 && server.Close(sc, 77).Ok())
        {
            sent = true;
            small_got = 100;  // close once
        }
        ThisThread::Yield();
    }
    CHECK(small_got >= 5);
    CHECK(big_ok);
    CHECK(client_got == 1);
    CHECK(closed);
    CHECK(reason == DisconnectReason::PeerClosed);
    CHECK(code == 77);
}

TEST_CASE("TCP transport: a connect to a closed port is refused", "[net][tcp][network]")
{
    Endpoint any;
    REQUIRE(Endpoint::Parse(StringView("127.0.0.1:0"), &any).Ok());
    Endpoint dead;
    {
        SocketStreamProvider probe;
        if (probe.Listen(any).Failed())
        {
            SKIP("cannot bind a loopback TCP socket here");
        }
        dead = probe.LocalEndpoint();
    }
    SocketStreamProvider provider;
    MonotonicClock clock;
    TcpTransport client(provider, clock);
    ConnectionId cc = 0;
    REQUIRE(client.Connect(dead, &cc).Ok());
    bool refused = false;
    const u64 start = clock.NowNanos();
    while (clock.NowNanos() - start < 5'000'000'000ull && !refused)
    {
        Event e;
        while (client.Poll(&e))
        {
            refused = refused || (e.kind == EventKind::Closed && e.reason == DisconnectReason::Refused);
        }
        ThisThread::Yield();
    }
    CHECK(refused);
}

}  // namespace vse::net
