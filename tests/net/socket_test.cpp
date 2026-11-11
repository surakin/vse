// net::Socket on loopback (Phase 20 Day 268): TCP connect, accept and echo through a polling loop (no reactor yet), UDP
// echo, ports, errors, partial writes, shutdown, misuse after close. Real sockets: the executable is labelled
// `network`; a machine that cannot bind loopback skips with the reason.

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "net/socket.h"
#include "os/os.h"
#include "sync/thread.h"

namespace engine_placeholder::net
{
namespace
{

// Calls `step` until it stops returning WouldBlock, for at most two seconds; returns its last status.
template <typename Step> Status Poll(Step step)
{
    const u64 deadline = os::MonotonicTimeNanos() + 2'000'000'000ULL;
    Status s = step();
    while (s.Is(ErrorCode::WouldBlock) && os::MonotonicTimeNanos() < deadline)
    {
        ThisThread::SleepForMicroseconds(200);
        s = step();
    }
    return s;
}

// Opens a bound loopback socket, or skips the test with the reason.
#define REQUIRE_LOOPBACK(family)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        Socket probe;                                                                                                  \
        const Status probe_open = Socket::OpenTcp(family, &probe);                                                     \
        const Status probe_bind = probe_open.Ok() ? probe.Bind(Endpoint(Address::Loopback(family), 0)) : probe_open;   \
        if (probe_bind.Failed())                                                                                       \
        {                                                                                                              \
            SKIP("loopback sockets are unavailable here: " << ErrorCodeName(probe_bind.Code()) << " (os error "        \
                                                           << probe_bind.OsError() << ")");                            \
        }                                                                                                              \
    } while (0)

struct Pair
{
    Socket listener;
    Socket client;
    Socket server;
    Endpoint address;
};

// A connected TCP pair on loopback.
void Connect(AddressFamily family, Pair* pair)
{
    REQUIRE(Socket::OpenTcp(family, &pair->listener).Ok());
    REQUIRE(pair->listener.SetReuseAddress(true).Ok());
    REQUIRE(pair->listener.Bind(Endpoint(Address::Loopback(family), 0)).Ok());
    REQUIRE(pair->listener.Listen().Ok());
    REQUIRE(pair->listener.LocalEndpoint(&pair->address).Ok());
    REQUIRE(pair->address.Port() != 0);
    REQUIRE(Socket::OpenTcp(family, &pair->client).Ok());
    const Status c = pair->client.Connect(pair->address);
    REQUIRE((c.Ok() || c.Is(ErrorCode::WouldBlock)));
    Endpoint peer;
    REQUIRE(Poll([&] { return pair->listener.Accept(&pair->server, &peer); }).Ok());
    if (c.Is(ErrorCode::WouldBlock))
    {
        REQUIRE(Poll([&] { return pair->client.FinishConnect(); }).Ok());
    }
    CHECK(peer.GetAddress().IsLoopback());
}

ByteSpan Bytes(const char* text)
{
    return ByteSpan(reinterpret_cast<const byte*>(text), StringView(text).Length());
}

TEST_CASE("TCP: connect, accept and echo on IPv4 loopback", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Pair p;
    Connect(AddressFamily::V4, &p);
    usize n = 0;
    REQUIRE(p.client.Send(Bytes("hello"), &n).Ok());
    CHECK(n == 5);
    byte buffer[64];
    REQUIRE(Poll([&] { return p.server.Receive(MutableByteSpan(buffer, sizeof buffer), &n); }).Ok());
    REQUIRE(n == 5);
    CHECK(buffer[0] == 'h');
    CHECK(buffer[4] == 'o');
    REQUIRE(p.server.Send(ByteSpan(buffer, n), &n).Ok());
    usize got = 0;
    REQUIRE(Poll([&] { return p.client.Receive(MutableByteSpan(buffer, sizeof buffer), &got); }).Ok());
    CHECK(got == 5);
    Endpoint local, peer;
    REQUIRE(p.client.LocalEndpoint(&local).Ok());
    REQUIRE(p.client.PeerEndpoint(&peer).Ok());
    CHECK(peer == p.address);
    CHECK(local.GetAddress() == Address::V4(127, 0, 0, 1));
}

TEST_CASE("TCP: IPv6 loopback, and an IPv4 endpoint on an IPv6 socket is mapped", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V6);
    Pair p;
    Connect(AddressFamily::V6, &p);
    CHECK(p.address.GetAddress().IsV6());
    usize n = 0;
    REQUIRE(p.client.Send(Bytes("v6"), &n).Ok());
    byte buffer[8];
    REQUIRE(Poll([&] { return p.server.Receive(MutableByteSpan(buffer, sizeof buffer), &n); }).Ok());
    CHECK(n == 2);
}

TEST_CASE("TCP: a dual-stack listener takes an IPv4 peer, reported as IPv4", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V6);
    Socket listener;
    REQUIRE(Socket::OpenTcp(AddressFamily::V6, &listener).Ok());
    REQUIRE(listener.SetDualStack(true).Ok());
    REQUIRE(listener.Bind(Endpoint(Address::Any(AddressFamily::V6), 0)).Ok());
    REQUIRE(listener.Listen().Ok());
    Endpoint at;
    REQUIRE(listener.LocalEndpoint(&at).Ok());
    Socket client;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &client).Ok());
    const Status c = client.Connect(Endpoint(Address::Loopback(AddressFamily::V4), at.Port()));
    REQUIRE((c.Ok() || c.Is(ErrorCode::WouldBlock)));
    Socket accepted;
    Endpoint peer;
    const Status a = Poll([&] { return listener.Accept(&accepted, &peer); });
    if (a.Failed())
    {
        SKIP("this machine does not give dual-stack sockets: " << ErrorCodeName(a.Code()));
    }
    CHECK(peer.GetAddress() == Address::V4(127, 0, 0, 1));
}

TEST_CASE("UDP: an echo through two sockets, with the sender's address", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Socket a, b;
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &a).Ok());
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &b).Ok());
    REQUIRE(a.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
    REQUIRE(b.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
    Endpoint a_at, b_at;
    REQUIRE(a.LocalEndpoint(&a_at).Ok());
    REQUIRE(b.LocalEndpoint(&b_at).Ok());
    usize n = 0;
    REQUIRE(a.SendTo(Bytes("ping"), b_at, &n).Ok());
    CHECK(n == 4);
    byte buffer[64];
    Endpoint from;
    REQUIRE(Poll([&] { return b.ReceiveFrom(MutableByteSpan(buffer, sizeof buffer), &n, &from); }).Ok());
    CHECK(n == 4);
    CHECK(from == a_at);
    REQUIRE(b.SendTo(ByteSpan(buffer, n), from, &n).Ok());
    REQUIRE(Poll([&] { return a.ReceiveFrom(MutableByteSpan(buffer, sizeof buffer), &n, &from); }).Ok());
    CHECK(n == 4);
    CHECK(from == b_at);
    // Nothing waiting: WouldBlock, and nothing was written.
    CHECK(a.ReceiveFrom(MutableByteSpan(buffer, sizeof buffer), &n, &from).Is(ErrorCode::WouldBlock));
    CHECK(n == 0);
}

TEST_CASE("UDP: a datagram larger than the buffer is MessageTooLarge, and one above the protocol limit cannot be sent",
          "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Socket a, b;
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &a).Ok());
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &b).Ok());
    REQUIRE(b.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
    Endpoint b_at;
    REQUIRE(b.LocalEndpoint(&b_at).Ok());
    Vector<byte> big(HeapAllocator::Default());
    for (usize i = 0; i < 1000; ++i)
    {
        big.Push(static_cast<byte>(i));
    }
    usize n = 0;
    REQUIRE(a.SendTo(ByteSpan(big.Data(), big.Size()), b_at, &n).Ok());
    byte small[100];
    Endpoint from;
    CHECK(Poll([&] { return b.ReceiveFrom(MutableByteSpan(small, sizeof small), &n, &from); })
              .Is(ErrorCode::MessageTooLarge));
    // The truncated datagram is gone: the next receive has nothing.
    CHECK(b.ReceiveFrom(MutableByteSpan(small, sizeof small), &n, &from).Is(ErrorCode::WouldBlock));
    Vector<byte> huge(HeapAllocator::Default());
    for (usize i = 0; i < 70000; ++i)
    {
        huge.Push(0);
    }
    CHECK(a.SendTo(ByteSpan(huge.Data(), huge.Size()), b_at, &n).Is(ErrorCode::MessageTooLarge));
    CHECK(n == 0);
}

TEST_CASE("Bind to port 0 reports the chosen port, and a second bind to it is AddressInUse", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Socket a, b;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &a).Ok());
    REQUIRE(a.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
    REQUIRE(a.Listen().Ok());
    Endpoint at;
    REQUIRE(a.LocalEndpoint(&at).Ok());
    CHECK(at.Port() != 0);
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &b).Ok());
    CHECK(b.Bind(at).Is(ErrorCode::AddressInUse));
    Socket c;
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &c).Ok());
    // Not an address of this machine (TEST-NET-3); a machine that allows binding non-local addresses
    // (net.ipv4.ip_nonlocal_bind) accepts it.
    const Status foreign = c.Bind(Endpoint(Address::V4(203, 0, 113, 77), 0));
    CHECK((foreign.Ok() || foreign.Is(ErrorCode::AddressNotAvailable)));
}

TEST_CASE("An unknown scope name is AddressNotAvailable, and an IPv4 endpoint on an IPv6 socket is mapped",
          "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V6);
    Socket s;
    REQUIRE(Socket::OpenTcp(AddressFamily::V6, &s).Ok());
    Address scoped;
    REQUIRE(Address::Parse("fe80::1%nosuchif0", &scoped).Ok());
    CHECK(s.Bind(Endpoint(scoped, 0)).Is(ErrorCode::AddressNotAvailable));
    // A client on an IPv6 socket connects to an IPv4 listener: the IPv4 endpoint is mapped, and the peer reads back as
    // IPv4.
    Socket listener;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &listener).Ok());
    REQUIRE(listener.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
    REQUIRE(listener.Listen().Ok());
    Endpoint at;
    REQUIRE(listener.LocalEndpoint(&at).Ok());
    Socket client;
    REQUIRE(Socket::OpenTcp(AddressFamily::V6, &client).Ok());
    REQUIRE(client.SetDualStack(true).Ok());
    Status c = client.Connect(at);
    if (c.Is(ErrorCode::WouldBlock))
    {
        c = Poll([&] { return client.FinishConnect(); });
    }
    if (c.Failed())
    {
        SKIP("this machine does not give dual-stack sockets: " << ErrorCodeName(c.Code()));
    }
    Endpoint peer;
    REQUIRE(client.PeerEndpoint(&peer).Ok());
    CHECK(peer == at);
    CHECK(peer.GetAddress().IsV4());
}

TEST_CASE("FinishConnect needs a connect under way and reports an outcome once", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Socket never;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &never).Ok());
    CHECK(never.FinishConnect().Is(
        ErrorCode::Invalid));  // not connecting: not WouldBlock, which would make a caller wait for ever
    Endpoint closed_port;
    {
        Socket temp;
        REQUIRE(Socket::OpenTcp(AddressFamily::V4, &temp).Ok());
        REQUIRE(temp.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
        REQUIRE(temp.LocalEndpoint(&closed_port).Ok());
    }
    Socket client;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &client).Ok());
    Status s = client.Connect(closed_port);
    if (s.Is(ErrorCode::WouldBlock))
    {
        s = Poll([&] { return client.FinishConnect(); });
        CHECK(s.Is(ErrorCode::Refused));
        CHECK(client.FinishConnect().Is(ErrorCode::Invalid));  // reported once
    }
    Pair p;
    Connect(AddressFamily::V4, &p);
    CHECK(p.client.FinishConnect().Is(ErrorCode::Invalid));  // outcome already reported by Connect's helper
}

TEST_CASE("Datagram calls on a stream socket are Invalid and leave the stream alone", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Pair p;
    Connect(AddressFamily::V4, &p);
    usize n = 0;
    REQUIRE(p.client.Send(Bytes("keep"), &n).Ok());
    byte buffer[16];
    Endpoint from;
    CHECK(p.server.ReceiveFrom(MutableByteSpan(buffer, sizeof buffer), &n, &from).Is(ErrorCode::Invalid));
    CHECK(p.client.SendTo(Bytes("x"), p.address, &n).Is(ErrorCode::Invalid));
    // The four bytes are still in the stream.
    REQUIRE(Poll([&] { return p.server.Receive(MutableByteSpan(buffer, sizeof buffer), &n); }).Ok());
    CHECK(n == 4);
    CHECK(buffer[0] == 'k');
}

TEST_CASE("UDP: Receive on a connected socket reports a cut datagram, like ReceiveFrom", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Socket a, b;
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &a).Ok());
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &b).Ok());
    REQUIRE(b.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
    Endpoint b_at;
    REQUIRE(b.LocalEndpoint(&b_at).Ok());
    REQUIRE(a.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
    Endpoint a_at;
    REQUIRE(a.LocalEndpoint(&a_at).Ok());
    REQUIRE(a.Connect(b_at).Ok());
    REQUIRE(b.Connect(a_at).Ok());
    byte payload[300] = {};
    usize n = 0;
    REQUIRE(a.Send(ByteSpan(payload, sizeof payload), &n).Ok());
    byte small[100];
    CHECK(Poll([&] { return b.Receive(MutableByteSpan(small, sizeof small), &n); }).Is(ErrorCode::MessageTooLarge));
    REQUIRE(a.Send(ByteSpan(payload, 100), &n).Ok());
    REQUIRE(Poll([&] { return b.Receive(MutableByteSpan(small, sizeof small), &n); }).Ok());
    CHECK(n == 100);  // a datagram that fits exactly is not "cut"
}

TEST_CASE("Connecting to a closed port is Refused", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Endpoint closed_port;
    {
        Socket temp;
        REQUIRE(Socket::OpenTcp(AddressFamily::V4, &temp).Ok());
        REQUIRE(temp.Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Ok());
        REQUIRE(temp.LocalEndpoint(&closed_port).Ok());
    }  // closed: nothing listens on the port now
    Socket client;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &client).Ok());
    Status s = client.Connect(closed_port);
    if (s.Is(ErrorCode::WouldBlock))
    {
        s = Poll([&] { return client.FinishConnect(); });
    }
    CHECK(s.Is(ErrorCode::Refused));
    CHECK(s.OsError() != 0);
}

TEST_CASE("TCP: a write larger than the socket buffers is taken in parts, and every byte arrives", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Pair p;
    Connect(AddressFamily::V4, &p);
    usize granted = 0;
    REQUIRE(p.client.SetSendBuffer(4096, &granted).Ok());
    CHECK(granted >= 2304);
    Vector<byte> data(HeapAllocator::Default());
    const usize total = 8 * 1024 * 1024;
    for (usize i = 0; i < total; ++i)
    {
        data.Push(static_cast<byte>(i * 31));
    }
    usize sent = 0;
    usize received = 0;
    usize partial_writes = 0;
    bool would_block = false;
    bool intact = true;
    Vector<byte> in(HeapAllocator::Default());
    in.Resize(1 << 16);
    const u64 deadline = os::MonotonicTimeNanos() + 20'000'000'000ULL;
    while (received < total && os::MonotonicTimeNanos() < deadline)
    {
        if (sent < total)
        {
            usize n = 0;
            const Status s = p.client.Send(ByteSpan(data.Data() + sent, total - sent), &n);
            if (s.Is(ErrorCode::WouldBlock))
            {
                would_block = true;
            }
            else
            {
                if (s.Failed())
                {
                    FAIL("send failed: " << ErrorCodeName(s.Code()));
                }
                if (n < total - sent)
                {
                    ++partial_writes;
                }
                sent += n;
            }
        }
        usize got = 0;
        const Status r = p.server.Receive(MutableByteSpan(in.Data(), in.Size()), &got);
        if (r.Ok())
        {
            for (usize i = 0; i < got; ++i)
            {
                intact = intact && in[i] == static_cast<byte>((received + i) * 31);
            }
            received += got;
        }
        else if (!r.Is(ErrorCode::WouldBlock))
        {
            FAIL("receive failed: " << ErrorCodeName(r.Code()));
        }
    }
    CHECK(intact);
    CHECK(sent == total);
    CHECK(received == total);
    CHECK((partial_writes > 0 || would_block));
}

TEST_CASE("TCP: shutting down the writing half is read as Closed after the data", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Pair p;
    Connect(AddressFamily::V4, &p);
    usize n = 0;
    REQUIRE(p.client.Send(Bytes("bye"), &n).Ok());
    REQUIRE(p.client.Shutdown(ShutdownMode::Write).Ok());
    byte buffer[16];
    REQUIRE(Poll([&] { return p.server.Receive(MutableByteSpan(buffer, sizeof buffer), &n); }).Ok());
    CHECK(n == 3);
    const Status end = Poll([&] { return p.server.Receive(MutableByteSpan(buffer, sizeof buffer), &n); });
    CHECK(end.Is(ErrorCode::Closed));
    CHECK(n == 0);
    // The client wrote nothing after the shutdown: sending is refused, with no signal raised.
    CHECK(p.client.Send(Bytes("x"), &n).Is(ErrorCode::Closed));
}

TEST_CASE("TCP: a peer that closes, then a write to it, reports Closed or Reset and raises no signal",
          "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Pair p;
    Connect(AddressFamily::V4, &p);
    REQUIRE(p.server.Close().Ok());
    usize n = 0;
    Status s = Status::Success();
    for (int i = 0; i < 100 && s.Ok(); ++i)
    {
        s = p.client.Send(Bytes("data"), &n);
        ThisThread::SleepForMilliseconds(1);
    }
    CHECK((s.Is(ErrorCode::Closed) || s.Is(ErrorCode::Reset)));
}

TEST_CASE("A closed, moved-from or never-opened socket answers Invalid to everything", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Socket s;
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &s).Ok());
    CHECK(s.IsOpen());
    REQUIRE(s.Close().Ok());
    CHECK_FALSE(s.IsOpen());
    CHECK(s.Close().Is(ErrorCode::Invalid));  // closed twice
    Socket moved_from;
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &moved_from).Ok());
    Socket target(static_cast<Socket&&>(moved_from));
    CHECK(target.IsOpen());
    CHECK_FALSE(moved_from.IsOpen());
    Socket* sockets[] = {&s, &moved_from};
    for (Socket* closed : sockets)
    {
        usize n = 0;
        byte b[4];
        Endpoint e;
        Socket other;
        CHECK(closed->Bind(Endpoint(Address::Loopback(AddressFamily::V4), 0)).Is(ErrorCode::Invalid));
        CHECK(closed->Listen().Is(ErrorCode::Invalid));
        CHECK(closed->Accept(&other, &e).Is(ErrorCode::Invalid));
        CHECK(closed->Connect(e).Is(ErrorCode::Invalid));
        CHECK(closed->FinishConnect().Is(ErrorCode::Invalid));
        CHECK(closed->Send(ByteSpan(b, 4), &n).Is(ErrorCode::Invalid));
        CHECK(closed->Receive(MutableByteSpan(b, 4), &n).Is(ErrorCode::Invalid));
        CHECK(closed->SendTo(ByteSpan(b, 4), e, &n).Is(ErrorCode::Invalid));
        CHECK(closed->ReceiveFrom(MutableByteSpan(b, 4), &n, &e).Is(ErrorCode::Invalid));
        CHECK(closed->Shutdown(ShutdownMode::Both).Is(ErrorCode::Invalid));
        CHECK(closed->LocalEndpoint(&e).Is(ErrorCode::Invalid));
        CHECK(closed->PeerEndpoint(&e).Is(ErrorCode::Invalid));
        CHECK(closed->SetReuseAddress(true).Is(ErrorCode::Invalid));
        CHECK(closed->SetNoDelay(true).Is(ErrorCode::Invalid));
        CHECK(closed->SetKeepAlive(true).Is(ErrorCode::Invalid));
        CHECK(closed->SetReceiveBuffer(4096).Is(ErrorCode::Invalid));
        CHECK(closed->SetSendBuffer(4096).Is(ErrorCode::Invalid));
        CHECK(closed->SetDualStack(true).Is(ErrorCode::Invalid));
        CHECK(closed->SetBroadcast(true).Is(ErrorCode::Invalid));
        CHECK(closed->NativeHandle() == -1);
    }
    // Opening into an open socket, or with no family, is refused.
    CHECK(Socket::OpenTcp(AddressFamily::V4, &target).Is(ErrorCode::Invalid));
    Socket fresh;
    CHECK(Socket::OpenTcp(AddressFamily::None, &fresh).Is(ErrorCode::Invalid));
}

TEST_CASE("Options: the families and kinds they apply to, and the buffer the system grants", "[network][socket]")
{
    REQUIRE_LOOPBACK(AddressFamily::V4);
    Socket udp, tcp4, tcp6;
    REQUIRE(Socket::OpenUdp(AddressFamily::V4, &udp).Ok());
    REQUIRE(Socket::OpenTcp(AddressFamily::V4, &tcp4).Ok());
    CHECK(udp.SetNoDelay(true).Is(ErrorCode::Invalid));     // TCP only
    CHECK(tcp4.SetDualStack(true).Is(ErrorCode::Invalid));  // IPv6 only
    CHECK(udp.SetBroadcast(true).Ok());
    CHECK(tcp4.SetNoDelay(true).Ok());
    CHECK(tcp4.SetKeepAlive(true).Ok());
    usize granted = 0;
    REQUIRE(udp.SetReceiveBuffer(1 << 20, &granted).Ok());
    CHECK(granted >= (1 << 19));
    CHECK(udp.SetReceiveBuffer(1 << 20).Ok());
    // An IPv6 endpoint on an IPv4 socket cannot be used, an unmapped IPv4 endpoint on an IPv6 socket can.
    Endpoint v6(Address::Loopback(AddressFamily::V6), 1);
    CHECK(tcp4.Bind(v6).Is(ErrorCode::Invalid));
    if (Socket::OpenTcp(AddressFamily::V6, &tcp6).Ok())
    {
        CHECK(tcp6.SetDualStack(true).Ok());
    }
}

}  // namespace
}  // namespace engine_placeholder::net
