// One interface, three backends (Phase 20 Day 277): the same scripted conversation runs against the simulated, the UDP
// and the TCP transport, and what the application sees is compared.

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "net/sim_datagram.h"
#include "net/sim_network.h"
#include "net/sim_stream.h"
#include "net/sim_transport.h"
#include "net/tcp_transport.h"
#include "net/udp_transport.h"
#include "transport_conformance.h"

namespace engine_placeholder::net
{
namespace
{

constexpr u32 kCmpChannels = 4;
constexpr u32 kCmpMessages = 100;

// What one side saw: its lifecycle events, and the messages of each channel in arrival order.
struct Seen
{
    u32 opened = 0;
    u32 closed = 0;
    DisconnectReason reason = DisconnectReason::None;
    u32 code = 0;
    Vector<u32> per_channel[kCmpChannels];
};

Seen Summarize(const conformance::Peer& p)
{
    Seen s;
    for (usize i = 0; i < p.events.Size(); ++i)
    {
        const conformance::Rec& r = p.events[i];
        if (r.kind == EventKind::Opened)
        {
            ++s.opened;
        }
        else if (r.kind == EventKind::Closed)
        {
            ++s.closed;
            s.reason = r.reason;
            s.code = r.code;
        }
        else if (r.kind == EventKind::Message && r.channel < kCmpChannels && r.data.Size() == 4)
        {
            u32 v = 0;
            for (u32 b = 0; b < 4; ++b)
            {
                v |= static_cast<u32>(static_cast<u8>(r.data[b])) << (8 * b);
            }
            s.per_channel[r.channel].Push(v);
        }
    }
    return s;
}

// Runs the script: connect, 100 messages spread over 4 channels, then the client closes with code 9.
template <class F> void RunScript(F& f, Seen* server_seen, Seen* client_seen)
{
    Transport* st = f.MakeServer();
    Transport* ct = f.MakeClient();
    conformance::Peer server = conformance::MakePeer(st);
    conformance::Peer client = conformance::MakePeer(ct);
    ConnectionId cc = 0;
    ConnectionId sc = 0;
    REQUIRE(conformance::Connect(f, &client, &server, &cc, &sc));
    for (u32 i = 0; i < kCmpMessages; ++i)
    {
        byte m[4];
        for (u32 b = 0; b < 4; ++b)
        {
            m[b] = static_cast<byte>(i >> (8 * b));
        }
        REQUIRE(ct->Send(cc, static_cast<u8>(i % kCmpChannels), ByteSpan(m, 4), Reliability::ReliableOrdered).Ok());
    }
    REQUIRE(conformance::SettleUntil(
        f, 600000u, [&] { return server.Count(EventKind::Message) >= kCmpMessages; }, &client, &server));
    REQUIRE(ct->Close(cc, 9).Ok());
    REQUIRE(
        conformance::SettleUntil(f, 600000u, [&] { return server.Count(EventKind::Closed) >= 1; }, &client, &server));
    conformance::Settle(f, 100, &client, &server);
    *server_seen = Summarize(server);
    *client_seen = Summarize(client);
}

class CmpSimFixture
{
public:
    CmpSimFixture() : network_(5)
    {
        server_endpoint_ = network_.AllocateEndpoint();
        dead_endpoint_ = network_.AllocateEndpoint();
    }
    ~CmpSimFixture()
    {
        client_.Reset();
        server_.Reset();
    }
    Transport* MakeServer()
    {
        server_ = MakeUnique<SimulatedTransport>(network_, server_endpoint_);
        REQUIRE(server_->Listen(server_endpoint_).Ok());
        return server_.Get();
    }
    Transport* MakeClient()
    {
        client_ = MakeUnique<SimulatedTransport>(network_, network_.AllocateEndpoint());
        return client_.Get();
    }
    Endpoint ServerEndpoint() const { return server_endpoint_; }
    Endpoint DeadEndpoint() const { return dead_endpoint_; }
    void Destroy(Transport*) {}
    void Pump(u32 milliseconds) { network_.Advance(static_cast<u64>(milliseconds) * 1'000'000ull); }
    bool Lossy() const { return false; }

private:
    SimNetwork network_;
    UniquePtr<SimulatedTransport> server_;
    UniquePtr<SimulatedTransport> client_;
    Endpoint server_endpoint_;
    Endpoint dead_endpoint_;
};

class UdpCmpFixture
{
public:
    UdpCmpFixture() : network_(6)
    {
        server_endpoint_ = network_.AllocateEndpoint();
        dead_endpoint_ = network_.AllocateEndpoint();
    }
    ~UdpCmpFixture()
    {
        client_.Reset();
        server_.Reset();
    }
    Transport* MakeServer()
    {
        server_port_ = MakeUnique<SimDatagramPort>(network_, server_endpoint_);
        server_ = MakeUnique<UdpTransport>(*server_port_, network_.GetClock());
        REQUIRE(server_->Listen(server_endpoint_).Ok());
        return server_.Get();
    }
    Transport* MakeClient()
    {
        client_port_ = MakeUnique<SimDatagramPort>(network_, network_.AllocateEndpoint());
        client_ = MakeUnique<UdpTransport>(*client_port_, network_.GetClock());
        return client_.Get();
    }
    Endpoint ServerEndpoint() const { return server_endpoint_; }
    Endpoint DeadEndpoint() const { return dead_endpoint_; }
    void Destroy(Transport*) {}
    void Pump(u32 milliseconds) { network_.Advance(static_cast<u64>(milliseconds) * 1'000'000ull); }
    bool Lossy() const { return false; }

private:
    SimNetwork network_;
    UniquePtr<SimDatagramPort> server_port_;
    UniquePtr<SimDatagramPort> client_port_;
    UniquePtr<UdpTransport> server_;
    UniquePtr<UdpTransport> client_;
    Endpoint server_endpoint_;
    Endpoint dead_endpoint_;
};

class TcpCmpFixture
{
public:
    TcpCmpFixture() : time_(7), streams_(time_.GetClock())
    {
        server_endpoint_ = streams_.AllocateEndpoint();
        dead_endpoint_ = streams_.AllocateEndpoint();
    }
    ~TcpCmpFixture()
    {
        client_.Reset();
        server_.Reset();
    }
    Transport* MakeServer()
    {
        server_provider_ = streams_.MakeProvider(server_endpoint_);
        server_ = MakeUnique<TcpTransport>(*server_provider_, time_.GetClock());
        REQUIRE(server_->Listen(server_endpoint_).Ok());
        return server_.Get();
    }
    Transport* MakeClient()
    {
        client_provider_ = streams_.MakeProvider(streams_.AllocateEndpoint());
        client_ = MakeUnique<TcpTransport>(*client_provider_, time_.GetClock());
        return client_.Get();
    }
    Endpoint ServerEndpoint() const { return server_endpoint_; }
    Endpoint DeadEndpoint() const { return dead_endpoint_; }
    void Destroy(Transport*) {}
    void Pump(u32 milliseconds) { time_.Advance(static_cast<u64>(milliseconds) * 1'000'000ull); }
    bool Lossy() const { return false; }

private:
    SimNetwork time_;
    SimStreamNetwork streams_;
    UniquePtr<StreamProvider> server_provider_;
    UniquePtr<StreamProvider> client_provider_;
    UniquePtr<TcpTransport> server_;
    UniquePtr<TcpTransport> client_;
    Endpoint server_endpoint_;
    Endpoint dead_endpoint_;
};

bool SameSeen(const Seen& a, const Seen& b)
{
    if (a.opened != b.opened || a.closed != b.closed || a.reason != b.reason || a.code != b.code)
    {
        return false;
    }
    for (u32 c = 0; c < kCmpChannels; ++c)
    {
        if (a.per_channel[c].Size() != b.per_channel[c].Size())
        {
            return false;
        }
        for (usize i = 0; i < a.per_channel[c].Size(); ++i)
        {
            if (a.per_channel[c][i] != b.per_channel[c][i])
            {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("Transports: the same scripted conversation looks the same on the simulated, UDP and TCP transports",
          "[net][tcp][udp]")
{
    Seen sim_server;
    Seen sim_client;
    Seen udp_server;
    Seen udp_client;
    Seen tcp_server;
    Seen tcp_client;
    {
        CmpSimFixture f;
        RunScript(f, &sim_server, &sim_client);
    }
    {
        UdpCmpFixture f;
        RunScript(f, &udp_server, &udp_client);
    }
    {
        TcpCmpFixture f;
        RunScript(f, &tcp_server, &tcp_client);
    }
    CHECK(sim_server.opened == 1);
    CHECK(sim_server.closed == 1);
    CHECK(sim_server.reason == DisconnectReason::PeerClosed);
    CHECK(sim_server.code == 9);
    CHECK(sim_client.closed == 1);
    CHECK(sim_client.reason == DisconnectReason::LocalClose);
    for (u32 c = 0; c < kCmpChannels; ++c)
    {
        CHECK(sim_server.per_channel[c].Size() == kCmpMessages / kCmpChannels);
    }
    CHECK(SameSeen(sim_server, udp_server));
    CHECK(SameSeen(sim_server, tcp_server));
    CHECK(SameSeen(sim_client, udp_client));
    CHECK(SameSeen(sim_client, tcp_client));
}

}  // namespace engine_placeholder::net
