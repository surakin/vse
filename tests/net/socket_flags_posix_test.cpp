// Every socket is non-blocking and close-on-exec from creation, including accepted ones (Phase 20 Day 268). Reads the
// descriptor flags directly, which only this POSIX test may do.

#include <fcntl.h>

#include <catch2/catch_test_macros.hpp>

#include "net/socket.h"

namespace engine_placeholder::net
{
namespace
{

bool NonBlockingAndCloseOnExec(const Socket& s)
{
    const int fd = static_cast<int>(s.NativeHandle());
    const int status = ::fcntl(fd, F_GETFL);
    const int descriptor = ::fcntl(fd, F_GETFD);
    return status >= 0 && (status & O_NONBLOCK) != 0 && descriptor >= 0 && (descriptor & FD_CLOEXEC) != 0;
}

TEST_CASE("Sockets are non-blocking and close-on-exec from creation, accepted ones too", "[network][socket]")
{
    Socket listener;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &listener).Ok());
    if (listener.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    REQUIRE(listener.Listen().Ok());
    CHECK(NonBlockingAndCloseOnExec(listener));
    Socket udp;
    REQUIRE(Socket::OpenUdp(AddressFamily::V6, &udp).Ok());
    CHECK(NonBlockingAndCloseOnExec(udp));
    Endpoint at;
    REQUIRE(listener.LocalEndpoint(&at).Ok());
    Socket client;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &client).Ok());
    CHECK(NonBlockingAndCloseOnExec(client));
    const Status c = client.Connect(at);
    REQUIRE((c.Ok() || c.Is(ErrorCode::WouldBlock)));
    Socket accepted;
    Endpoint peer;
    Status a = listener.Accept(&accepted, &peer);
    for (int i = 0; i < 2000 && a.Is(ErrorCode::WouldBlock); ++i)
    {
        a = listener.Accept(&accepted, &peer);
    }
    REQUIRE(a.Ok());
    CHECK(NonBlockingAndCloseOnExec(accepted));
}

}  // namespace
}  // namespace engine_placeholder::net
