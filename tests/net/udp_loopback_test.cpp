// The UDP transport over real loopback sockets (Phase 20 Day 274). Labelled `network`.

#include <catch2/catch_test_macros.hpp>

#include "net/udp_port.h"
#include "net/udp_transport.h"
#include "sync/thread.h"

namespace engine_placeholder::net
{

TEST_CASE("UDP transport: a handshake and messages over loopback sockets", "[net][udp][network]")
{
    Endpoint any;
    REQUIRE(Endpoint::Parse(StringView("127.0.0.1:0"), &any).Ok());
    SocketDatagramPort server_port;
    SocketDatagramPort client_port;
    if (SocketDatagramPort::Open(any, &server_port).Failed() || SocketDatagramPort::Open(any, &client_port).Failed())
    {
        SKIP("cannot bind a loopback UDP socket here");
    }
    MonotonicClock clock;
    UdpTransport server(server_port, clock);
    UdpTransport client(client_port, clock);
    REQUIRE(server.Listen(server_port.LocalEndpoint()).Ok());
    ConnectionId cc = 0;
    REQUIRE(client.Connect(server_port.LocalEndpoint(), &cc).Ok());
    bool client_open = false;
    bool server_open = false;
    ConnectionId sc = 0;
    u32 server_got = 0;
    u32 client_got = 0;
    bool sent = false;
    const u64 start = clock.NowNanos();
    while (clock.NowNanos() - start < 5'000'000'000ull && (server_got < 5 || client_got < 5))
    {
        Event e;
        while (server.Poll(&e))
        {
            if (e.kind == EventKind::Opened)
            {
                server_open = true;
                sc = e.connection;
            }
            server_got += e.kind == EventKind::Message ? 1 : 0;
        }
        while (client.Poll(&e))
        {
            client_open = client_open || e.kind == EventKind::Opened;
            client_got += e.kind == EventKind::Message ? 1 : 0;
        }
        if (client_open && server_open && !sent)
        {
            sent = true;
            const byte m[4] = {static_cast<byte>(1), static_cast<byte>(2), static_cast<byte>(3), static_cast<byte>(4)};
            for (int i = 0; i < 5; ++i)
            {
                REQUIRE(client.Send(cc, 0, ByteSpan(m, 4), Reliability::Unreliable).Ok());
                REQUIRE(server.Send(sc, 1, ByteSpan(m, 2), Reliability::Unreliable).Ok());
            }
        }
        ThisThread::Yield();
    }
    CHECK(client_open);
    CHECK(server_open);
    CHECK(server_got == 5);
    CHECK(client_got == 5);
}

}  // namespace engine_placeholder::net
