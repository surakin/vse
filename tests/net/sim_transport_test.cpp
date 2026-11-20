// The simulated network and transport (Phase 20 Day 273): the conformance suite over a clean link and over a hostile
// one, impairment rates measured over a million packets, and replay determinism.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "net/sim_transport.h"
#include "transport_conformance.h"

namespace vse::net
{
namespace
{

Endpoint Ep(const char* text)
{
    Endpoint e;
    REQUIRE(Endpoint::Parse(StringView(text), &e).Ok());
    return e;
}

LinkConfig HostileLink()
{
    LinkConfig l;
    l.latency_nanos = 20'000'000;
    l.jitter_nanos = 10'000'000;
    l.loss = 0.10;
    l.reorder = 0.05;
    l.duplicate = 0.05;
    l.corrupt = 0.02;
    return l;
}

LinkConfig CleanLink()
{
    LinkConfig l;
    l.latency_nanos = 5'000'000;
    return l;
}

template <bool kHostile> class SimFixture
{
public:
    SimFixture() : network_(0x5eed1234ull)
    {
        network_.SetDefaultLink(kHostile ? HostileLink() : CleanLink());
        server_endpoint_ = Ep("127.0.0.1:9000");
        dead_endpoint_ = Ep("127.0.0.1:9999");
    }
    ~SimFixture()
    {
        for (usize i = 0; i < transports_.Size(); ++i)
        {
            transports_[i].Reset();
        }
    }
    Transport* MakeServer()
    {
        transports_.Push(MakeUnique<SimulatedTransport>(network_, server_endpoint_));
        REQUIRE(transports_[transports_.Size() - 1]->Listen(server_endpoint_).Ok());
        return transports_[transports_.Size() - 1].Get();
    }
    Transport* MakeClient()
    {
        transports_.Push(MakeUnique<SimulatedTransport>(network_, network_.AllocateEndpoint()));
        return transports_[transports_.Size() - 1].Get();
    }
    Endpoint ServerEndpoint() const { return server_endpoint_; }
    Endpoint DeadEndpoint() const { return dead_endpoint_; }
    void Destroy(Transport* t)
    {
        for (usize i = 0; i < transports_.Size(); ++i)
        {
            if (transports_[i].Get() == t)
            {
                transports_[i].Reset();
            }
        }
    }
    void Pump(u32 milliseconds) { network_.Advance(static_cast<u64>(milliseconds) * 1'000'000ull); }
    bool Lossy() const { return kHostile; }
    SimNetwork& Network() { return network_; }

private:
    SimNetwork network_;
    Vector<UniquePtr<SimulatedTransport>> transports_;
    Endpoint server_endpoint_;
    Endpoint dead_endpoint_;
};

using Clean = SimFixture<false>;
using Hostile = SimFixture<true>;

}  // namespace

#define SIM_CONFORMANCE(NAME)                                                                                          \
    TEST_CASE("Sim transport, clean link: " #NAME, "[net][sim]")                                                       \
    {                                                                                                                  \
        Clean f;                                                                                                       \
        conformance::NAME(f);                                                                                          \
    }                                                                                                                  \
    TEST_CASE("Sim transport, hostile link: " #NAME, "[net][sim]")                                                     \
    {                                                                                                                  \
        Hostile f;                                                                                                     \
        conformance::NAME(f);                                                                                          \
    }

SIM_CONFORMANCE(ConnectAndAccept)
SIM_CONFORMANCE(CloseCarriesReasons)
SIM_CONFORMANCE(ConnectToNothingIsRefused)
SIM_CONFORMANCE(ReliableOrderedArrivesOnceInOrder)
SIM_CONFORMANCE(ReliableUnorderedArrivesOnce)
SIM_CONFORMANCE(UnreliableIsNeverCorruptedOrDuplicated)
SIM_CONFORMANCE(UnreliableSequencedNeverGoesBack)
SIM_CONFORMANCE(MessageSizeAndChannelLimits)
SIM_CONFORMANCE(ChannelsAreIndependent)
SIM_CONFORMANCE(ManyConnectionsAtOnce)
SIM_CONFORMANCE(CloseDuringSend)
SIM_CONFORMANCE(NoEventsAfterClose)
SIM_CONFORMANCE(DestroyingATransportWithTrafficInFlight)

namespace
{

class CountingSink final : public PacketSink
{
public:
    void OnPacket(const Endpoint&, ByteSpan packet) override
    {
        ++received;
        if (packet.Size() != expected_size)
        {
            ++wrong_size;
        }
    }
    void OnTick(u64) override {}
    void OnUnreachable(const Endpoint&) override { ++unreachable; }
    u64 received = 0;
    u64 wrong_size = 0;
    u64 unreachable = 0;
    usize expected_size = 100;
};

// Sends a million packets one way and returns the counters.
SimCounters Blast(u64 seed, const LinkConfig& link, usize packets, CountingSink* sink)
{
    SimNetwork net(seed);
    net.SetDefaultLink(link);
    const Endpoint a = Ep("127.0.0.1:1");
    const Endpoint b = Ep("127.0.0.1:2");
    CountingSink src;
    REQUIRE(net.Attach(a, &src).Ok());
    REQUIRE(net.Attach(b, sink).Ok());
    Vector<byte> bytes;
    for (usize i = 0; i < sink->expected_size; ++i)
    {
        bytes.Push(static_cast<byte>(i));
    }
    for (usize i = 0; i < packets; ++i)
    {
        net.Transmit(a, b, ByteSpan(bytes.Data(), bytes.Size()));
        if (i % 1000 == 999)
        {
            net.Advance(SimNetwork::kTickNanos);
        }
    }
    net.Advance(5'000'000'000ull);
    net.Detach(a);
    net.Detach(b);
    return net.Counters();
}

}  // namespace

TEST_CASE("Sim network: impairment rates over a million packets match the configuration", "[net][sim]")
{
    constexpr usize kN = 1'000'000;
    LinkConfig l;
    l.latency_nanos = 10'000'000;
    l.jitter_nanos = 5'000'000;
    l.loss = 0.05;
    l.duplicate = 0.02;
    l.reorder = 0.03;
    l.corrupt = 0.01;
    CountingSink sink;
    const SimCounters c = Blast(42, l, kN, &sink);
    CHECK(c.sent == kN);
    const f64 n = static_cast<f64>(kN);
    CHECK(static_cast<f64>(c.dropped_loss) / n == Catch::Approx(0.05).margin(0.002));
    const f64 survivors = n - static_cast<f64>(c.dropped_loss);
    CHECK(static_cast<f64>(c.duplicated) / survivors == Catch::Approx(0.02).margin(0.002));
    CHECK(static_cast<f64>(c.reordered) / survivors == Catch::Approx(0.03).margin(0.002));
    CHECK(static_cast<f64>(c.corrupted) / survivors == Catch::Approx(0.01).margin(0.002));
    CHECK(c.dropped_mtu == 0);
    CHECK(c.delivered == sink.received);
    CHECK(c.delivered == survivors + c.duplicated);  // every surviving packet and each duplicate arrives
    CHECK(sink.wrong_size == 0);
}

TEST_CASE("Sim network: bursts, MTU, and a full bandwidth queue drop what they should", "[net][sim]")
{
    {
        LinkConfig l;
        l.burst_start = 0.01;
        l.burst_length = 5;
        CountingSink sink;
        const SimCounters c = Blast(7, l, 200000, &sink);
        // A burst drops 5 in a row; the loss fraction is about start*5 / (1 + start*4).
        const f64 expected = 0.01 * 5 / (1 + 0.01 * 4);
        CHECK(static_cast<f64>(c.dropped_loss) / 200000.0 == Catch::Approx(expected).margin(0.004));
    }
    {
        LinkConfig l;
        l.mtu = 50;
        CountingSink sink;
        const SimCounters c = Blast(1, l, 1000, &sink);
        CHECK(c.dropped_mtu == 1000);
        CHECK(c.delivered == 0);
    }
    {
        LinkConfig l;
        l.bandwidth_bps = 800'000;  // 100 kB/s
        l.queue_bytes = 10'000;
        CountingSink sink;
        const SimCounters c = Blast(1, l, 2000, &sink);  // 200 kB offered within 2 ms: the queue overflows
        CHECK(c.dropped_queue > 0);
        CHECK(c.delivered + c.dropped_queue == 2000);
    }
}

TEST_CASE("Sim network: the same seed gives the same trace, another seed a different one", "[net][sim]")
{
    const auto run = [](u64 seed, Vector<byte>* trace)
    {
        Hostile f;
        f.Network();  // (the fixture seeds with a constant; use a network of our own for the seed)
        SimNetwork net(seed);
        net.SetDefaultLink(HostileLink());
        net.EnableTrace();
        SimulatedTransport server(net, Ep("127.0.0.1:9000"));
        SimulatedTransport client(net, net.AllocateEndpoint());
        REQUIRE(server.Listen(Ep("127.0.0.1:9000")).Ok());
        ConnectionId cc = 0;
        REQUIRE(client.Connect(Ep("127.0.0.1:9000"), &cc).Ok());
        Vector<byte> m;
        for (u32 i = 0; i < 100; ++i)
        {
            conformance::MakeMessage(i, 64, &m);
            REQUIRE(client.Send(cc, 0, ByteSpan(m.Data(), m.Size()), Reliability::ReliableOrdered).Ok());
        }
        Event e;
        for (u32 t = 0; t < 100; ++t)
        {
            net.Advance(10'000'000);
            while (server.Poll(&e))
            {
            }
            while (client.Poll(&e))
            {
            }
        }
        const ByteSpan tr = net.Trace();
        trace->Clear();
        for (usize i = 0; i < tr.Size(); ++i)
        {
            trace->Push(tr[i]);
        }
    };
    Vector<byte> a;
    Vector<byte> b;
    Vector<byte> c;
    run(1234, &a);
    run(1234, &b);
    run(1235, &c);
    REQUIRE(a.Size() > 1000);
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);
    CHECK((a.Size() != c.Size() || MemCompare(a.Data(), c.Data(), a.Size()) != 0));
}

TEST_CASE("Sim transport: a silent peer times out; a listener that is full refuses", "[net][sim]")
{
    SimNetwork net(9);
    net.SetDefaultLink(CleanLink());
    SimTransportConfig small;
    small.max_connections = 2;
    SimulatedTransport server(net, Ep("127.0.0.1:9000"), small);
    REQUIRE(server.Listen(Ep("127.0.0.1:9000")).Ok());
    REQUIRE(server.Listen(Ep("127.0.0.1:9000")).Is(ErrorCode::Invalid));  // twice
    REQUIRE(server.Listen(Ep("127.0.0.1:9001")).Is(ErrorCode::Invalid));  // not its own endpoint
    Vector<UniquePtr<SimulatedTransport>> clients;
    Vector<ConnectionId> ids;
    for (int i = 0; i < 3; ++i)
    {
        clients.Push(MakeUnique<SimulatedTransport>(net, net.AllocateEndpoint()));
        ConnectionId id = 0;
        REQUIRE(clients[i]->Connect(Ep("127.0.0.1:9000"), &id).Ok());
        ids.Push(id);
    }
    net.Advance(500'000'000);
    u32 opened = 0;
    u32 refused = 0;
    for (usize i = 0; i < clients.Size(); ++i)
    {
        Event e;
        while (clients[i]->Poll(&e))
        {
            opened += e.kind == EventKind::Opened ? 1 : 0;
            refused += e.kind == EventKind::Closed && e.reason == DisconnectReason::Refused ? 1 : 0;
        }
    }
    CHECK(opened == 2);
    CHECK(refused == 1);
    // Silence: the first client goes quiet (detached by destruction); the server times out its connection.
    clients[0].Reset();
    net.Advance(12'000'000'000ull);
    u32 timed_out = 0;
    Event e;
    while (server.Poll(&e))
    {
        timed_out += e.kind == EventKind::Closed && e.reason == DisconnectReason::TimedOut ? 1 : 0;
    }
    CHECK(timed_out == 1);
}

namespace
{

u32 TestChecksum(const Vector<byte>& body)
{
    u32 hash = 2166136261u;
    for (usize i = 0; i < body.Size(); ++i)
    {
        hash ^= static_cast<u8>(body[i]);
        hash *= 16777619u;
    }
    return hash;
}

void PutU32(Vector<byte>* v, u32 x)
{
    for (u32 i = 0; i < 4; ++i)
    {
        v->Push(static_cast<byte>((x >> (8 * i)) & 0xff));
    }
}

}  // namespace

TEST_CASE("Sim transport: a transport whose endpoint is taken does not detach the owner", "[net][sim]")
{
    SimNetwork net(3);
    net.SetDefaultLink(CleanLink());
    SimulatedTransport server(net, Ep("127.0.0.1:9000"));
    REQUIRE(server.Listen(Ep("127.0.0.1:9000")).Ok());
    {
        SimulatedTransport usurper(net, Ep("127.0.0.1:9000"));  // AddressInUse inside
    }
    SimulatedTransport client(net, net.AllocateEndpoint());
    ConnectionId cc = 0;
    REQUIRE(client.Connect(Ep("127.0.0.1:9000"), &cc).Ok());
    net.Advance(300'000'000);
    Event e;
    bool opened = false;
    while (server.Poll(&e))
    {
        opened = opened || e.kind == EventKind::Opened;
    }
    CHECK(opened);
}

TEST_CASE("Sim transport: a client that restarts at the same endpoint gets a fresh connection", "[net][sim]")
{
    SimNetwork net(4);
    net.SetDefaultLink(CleanLink());
    SimulatedTransport server(net, Ep("127.0.0.1:9000"));
    REQUIRE(server.Listen(Ep("127.0.0.1:9000")).Ok());
    const Endpoint where = Ep("127.0.0.1:20500");
    {
        SimulatedTransport old_client(net, where);
        ConnectionId cc = 0;
        REQUIRE(old_client.Connect(Ep("127.0.0.1:9000"), &cc).Ok());
        net.Advance(100'000'000);
        const byte b[1] = {static_cast<byte>(1)};
        REQUIRE(old_client.Send(cc, 0, ByteSpan(b, 1), Reliability::ReliableOrdered).Ok());
        net.Advance(100'000'000);
    }  // dies without closing
    SimulatedTransport client(net, where);  // same endpoint, same first connection id
    ConnectionId cc = 0;
    REQUIRE(client.Connect(Ep("127.0.0.1:9000"), &cc).Ok());
    net.Advance(100'000'000);
    const byte b[1] = {static_cast<byte>(2)};
    REQUIRE(client.Send(cc, 0, ByteSpan(b, 1), Reliability::ReliableOrdered).Ok());
    net.Advance(300'000'000);
    u32 opened = 0;
    u32 delivered_new = 0;
    Event e;
    while (server.Poll(&e))
    {
        opened += e.kind == EventKind::Opened ? 1 : 0;
        delivered_new += e.kind == EventKind::Message && e.message.Size() == 1 && e.message[0] == static_cast<byte>(2)
                             ? 1
                             : 0;
    }
    CHECK(opened == 2);  // the old connection and the new one, not one re-accepted
    CHECK(delivered_new == 1);
}

TEST_CASE("Sim transport: closing a connect that was already accepted tells the server", "[net][sim]")
{
    SimNetwork net(5);
    LinkConfig l = CleanLink();
    l.latency_nanos = 50'000'000;
    net.SetDefaultLink(l);
    SimulatedTransport server(net, Ep("127.0.0.1:9000"));
    REQUIRE(server.Listen(Ep("127.0.0.1:9000")).Ok());
    SimulatedTransport client(net, net.AllocateEndpoint());
    ConnectionId cc = 0;
    REQUIRE(client.Connect(Ep("127.0.0.1:9000"), &cc).Ok());
    net.Advance(60'000'000);  // the server has the request, its accept is on the way
    REQUIRE(client.Close(cc, 11).Ok());
    net.Advance(500'000'000);
    Event e;
    bool closed_by_peer = false;
    while (server.Poll(&e))
    {
        closed_by_peer = closed_by_peer || (e.kind == EventKind::Closed && e.reason == DisconnectReason::PeerClosed &&
                                            e.close_code == 11);
    }
    CHECK(closed_by_peer);
}

TEST_CASE("Sim transport: a reliable message far beyond the window is neither delivered nor acknowledged", "[net][sim]")
{
    SimNetwork net(6);
    net.SetDefaultLink(CleanLink());
    SimulatedTransport server(net, Ep("127.0.0.1:9000"));
    REQUIRE(server.Listen(Ep("127.0.0.1:9000")).Ok());
    SimulatedTransport client(net, net.AllocateEndpoint());
    ConnectionId cc = 0;
    REQUIRE(client.Connect(Ep("127.0.0.1:9000"), &cc).Ok());
    net.Advance(100'000'000);
    Event e;
    while (server.Poll(&e))
    {
    }
    // Forge, from the client's endpoint, a ReliableUnordered and a ReliableOrdered message with huge sequence numbers
    // to the server's first connection (id 1).
    for (u8 rel : {static_cast<u8>(Reliability::ReliableUnordered), static_cast<u8>(Reliability::ReliableOrdered)})
    {
        Vector<byte> body;
        body.Push(static_cast<byte>(4));  // kData
        PutU32(&body, 1);
        body.Push(static_cast<byte>(0));
        body.Push(static_cast<byte>(rel));
        PutU32(&body, 1'000'000);
        body.Push(static_cast<byte>(9));
        Vector<byte> packet;
        PutU32(&packet, TestChecksum(body));
        for (usize i = 0; i < body.Size(); ++i)
        {
            packet.Push(body[i]);
        }
        net.Transmit(client.LocalEndpoint(), Ep("127.0.0.1:9000"), ByteSpan(packet.Data(), packet.Size()));
    }
    const u64 before = net.Counters().sent;
    net.Advance(200'000'000);
    u32 messages = 0;
    while (server.Poll(&e))
    {
        messages += e.kind == EventKind::Message ? 1 : 0;
    }
    CHECK(messages == 0);
    CHECK(net.Counters().sent ==
          before);  // and no acknowledgement went back (only keep-alives would, and none are due)
}

}  // namespace vse::net
