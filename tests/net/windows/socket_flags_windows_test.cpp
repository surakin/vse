// Every socket is non-blocking and not inherited by child processes from creation, accepted ones too (Phase 20 Day
// 278): the Windows twin of socket_flags_posix_test.cpp. Reads the handle's inherit flag directly, which only this test
// may do; Windows has no call that reads a socket's blocking mode, so a receive with nothing to read must report
// WouldBlock at once instead of waiting.

#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "net/socket.h"
#include "os/windows/win32.h"
#include "sync/thread.h"

namespace vse::net
{
namespace
{

bool NotInherited(const Socket& s)
{
    DWORD flags = 0;
    return ::GetHandleInformation(reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(s.NativeHandle())), &flags) != 0 &&
           (flags & HANDLE_FLAG_INHERIT) == 0;
}

bool NonBlocking(Socket& s)
{
    Array<byte, 1> buffer{};
    usize n = 0;
    return s.Receive(MutableByteSpan(buffer.Data(), buffer.Size()), &n).Is(ErrorCode::WouldBlock);
}

TEST_CASE("Windows: sockets are non-blocking and not inherited from creation, accepted ones too", "[network][socket]")
{
    Socket listener;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &listener).Ok());
    if (listener.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    REQUIRE(listener.Listen().Ok());
    CHECK(NotInherited(listener));
    Socket udp;
    REQUIRE(Socket::OpenUdp(AddressFamily::V6, &udp).Ok());
    CHECK(NotInherited(udp));
    Endpoint at;
    REQUIRE(listener.LocalEndpoint(&at).Ok());
    Socket client;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &client).Ok());
    CHECK(NotInherited(client));
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
    CHECK(NotInherited(accepted));
    CHECK(NonBlocking(accepted));
}

TEST_CASE("Windows: a UDP socket is not reset by an ICMP port unreachable", "[network][socket]")
{
    // Without SIO_UDP_CONNRESET off, a datagram sent to a closed port makes the next receive fail with WSAECONNRESET.
    Socket udp;
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &udp).Ok());
    if (udp.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Failed())
    {
        SKIP("loopback sockets are unavailable here");
    }
    Socket closed;
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &closed).Ok());
    REQUIRE(closed.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
    Endpoint gone;
    REQUIRE(closed.LocalEndpoint(&gone).Ok());
    REQUIRE(closed.Close().Ok());
    usize sent = 0;
    const Array<byte, 1> one = {byte{1}};
    REQUIRE(udp.SendTo(ByteSpan(one.Data(), one.Size()), gone, &sent).Ok());
    ThisThread::SleepForMilliseconds(100);  // the ICMP answer arrives on loopback well within this
    Array<byte, 16> buffer{};
    usize received = 0;
    Endpoint from;
    const Status r = udp.ReceiveFrom(MutableByteSpan(buffer.Data(), buffer.Size()), &received, &from);
    CHECK(r.Is(ErrorCode::WouldBlock));
    CHECK(NonBlocking(udp));
}

}  // namespace
}  // namespace vse::net
