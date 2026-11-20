// net_demo: Phase 20 Day 281. A headless server and a client in one process run the same scripted conversation over
// each transport: connect, 100 messages on 4 channels, one large message, a reply from the server, and a close with a
// code. The transports are the simulated one, UDP and TCP, either over a simulated network (--simulated: deterministic,
// no sockets) or over real loopback sockets with ephemeral ports. The UDP and TCP limits come from the net.* settings'
// defaults.
//
//   net_demo [--simulated]   use the simulated network instead of loopback sockets
//            [--large N]     bytes of the large message (default 262144; the simulated transport carries 1 KiB at most)
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "lang/utility.h"
#include "net/clock.h"
#include "net/net_settings.h"
#include "net/sim_datagram.h"
#include "net/sim_network.h"
#include "net/sim_stream.h"
#include "net/sim_transport.h"
#include "net/socket_stream.h"
#include "net/tcp_transport.h"
#include "net/udp_port.h"
#include "net/udp_transport.h"
#include "os/console.h"
#include "settings/registry.h"
#include "sync/thread.h"

using namespace vse;
using namespace vse::net;

namespace
{

constexpr u32 kChannels = 4;

struct Seen
{
    u32 opened = 0;
    u32 closed = 0;
    DisconnectReason reason = DisconnectReason::None;
    u32 code = 0;
    u32 per_channel[kChannels] = {};
    u32 in_order_errors = 0;
    u32 next_expected[kChannels] = {};
    usize large_bytes = 0;
    bool large_ok = false;
    u32 replies = 0;
};

u8 PatternByte(usize i)
{
    return static_cast<u8>(i * 131 + (i >> 9));
}

// Drains a transport's events into `seen`; remembers the connection of the first Opened.
void Drain(Transport* t, Seen* seen, ConnectionId* connection, usize large)
{
    Event e;
    while (t->Poll(&e))
    {
        if (e.kind == EventKind::Opened)
        {
            ++seen->opened;
            *connection = e.connection;
        }
        else if (e.kind == EventKind::Closed)
        {
            ++seen->closed;
            seen->reason = e.reason;
            seen->code = e.close_code;
        }
        else if (e.kind == EventKind::Message)
        {
            if (e.message.Size() == 4 && e.channel < kChannels)
            {
                u32 v = 0;
                for (u32 b = 0; b < 4; ++b)
                {
                    v |= static_cast<u32>(static_cast<u8>(e.message[b])) << (8 * b);
                }
                // Messages of one channel carry increasing numbers: v = index, channel = index % 4.
                if (v != seen->next_expected[e.channel] * kChannels + e.channel)
                {
                    ++seen->in_order_errors;
                }
                ++seen->next_expected[e.channel];
                ++seen->per_channel[e.channel];
            }
            else if (e.message.Size() == large && large != 0)
            {
                seen->large_bytes = e.message.Size();
                seen->large_ok = true;
                for (usize i = 0; i < large; ++i)
                {
                    if (static_cast<u8>(e.message[i]) != PatternByte(i))
                    {
                        seen->large_ok = false;
                        break;
                    }
                }
            }
            else if (e.message.Size() == 5)
            {
                ++seen->replies;
            }
        }
    }
}

// Where the time goes: the simulated network's clock, or the real one.
class World
{
public:
    virtual ~World() = default;
    virtual const Clock& GetClock() = 0;
    virtual void Step() = 0;  // lets a millisecond of simulated time pass, or yields briefly
};

class SimWorld final : public World
{
public:
    SimWorld() : net_(7) {}
    const Clock& GetClock() override { return net_.GetClock(); }
    void Step() override { net_.Advance(1'000'000); }
    SimNetwork& Net() { return net_; }

private:
    SimNetwork net_;
};

class RealWorld final : public World
{
public:
    const Clock& GetClock() override { return clock_; }
    void Step() override { ThisThread::Yield(); }

private:
    MonotonicClock clock_;
};

// The conversation. `server` listens, `client` connects to `to`.
bool Converse(const char* name, World* world, Transport* server, Transport* client, const Endpoint& to, usize large)
{
    ConnectionId cc = 0;
    if (client->Connect(to, &cc).Failed())
    {
        os::PrintError("net_demo: %s: connect failed\n", name);
        return false;
    }
    Seen server_seen;
    Seen client_seen;
    ConnectionId client_conn = cc;
    ConnectionId server_conn = 0;
    const u64 start = world->GetClock().NowNanos();
    const u32 messages = 100;
    bool sent = false;
    bool replied = false;
    bool closed = false;
    u32 steps = 0;
    while (world->GetClock().NowNanos() - start < 20'000'000'000ull && steps < 200000 && server_seen.closed == 0)
    {
        world->Step();
        ++steps;
        Drain(server, &server_seen, &server_conn, large);
        Drain(client, &client_seen, &client_conn, large);
        if (!sent && client_seen.opened != 0 && server_seen.opened != 0)
        {
            sent = true;
            for (u32 i = 0; i < messages; ++i)
            {
                byte m[4];
                for (u32 b = 0; b < 4; ++b)
                {
                    m[b] = static_cast<byte>(i >> (8 * b));
                }
                // Message i travels on channel i % 4 and carries i, so each channel counts up by 4.
                if (client
                        ->Send(client_conn, static_cast<u8>(i % kChannels), ByteSpan(m, 4),
                               Reliability::ReliableOrdered)
                        .Failed())
                {
                    os::PrintError("net_demo: %s: send failed\n", name);
                    return false;
                }
            }
            if (large != 0)
            {
                Vector<byte> big;
                big.Resize(large);
                for (usize i = 0; i < large; ++i)
                {
                    big[i] = static_cast<byte>(PatternByte(i));
                }
                if (client->Send(client_conn, 0, ByteSpan(big.Data(), big.Size()), Reliability::ReliableOrdered)
                        .Failed())
                {
                    os::PrintError("net_demo: %s: large send failed\n", name);
                    return false;
                }
            }
        }
        u32 got = 0;
        for (u32 c = 0; c < kChannels; ++c)
        {
            got += server_seen.per_channel[c];
        }
        if (sent && !replied && got == messages && (large == 0 || server_seen.large_ok))
        {
            replied = true;
            const byte r[5] = {static_cast<byte>('r'), static_cast<byte>('e'), static_cast<byte>('p'),
                               static_cast<byte>('l'), static_cast<byte>('y')};
            (void)server->Send(server_conn, 1, ByteSpan(r, 5), Reliability::ReliableOrdered);
        }
        if (replied && !closed && client_seen.replies == 1)
        {
            closed = true;
            (void)client->Close(client_conn, 42);
        }
    }
    // Let the close reach the server and settle.
    for (u32 i = 0; i < 300; ++i)
    {
        world->Step();
        Drain(server, &server_seen, &server_conn, large);
        Drain(client, &client_seen, &client_conn, large);
    }
    u32 got = 0;
    for (u32 c = 0; c < kChannels; ++c)
    {
        got += server_seen.per_channel[c];
    }
    const bool ok = server_seen.opened == 1 && client_seen.opened == 1 && got == messages &&
                    server_seen.in_order_errors == 0 && (large == 0 || server_seen.large_ok) &&
                    client_seen.replies == 1 && server_seen.closed == 1 &&
                    server_seen.reason == DisconnectReason::PeerClosed && server_seen.code == 42 &&
                    client_seen.closed == 1 && client_seen.reason == DisconnectReason::LocalClose;
    os::Print("net_demo: %-9s %s (%u messages in order, large %zu bytes, closed with code %u after %u steps)\n", name,
              ok ? "ok" : "FAILED", got, server_seen.large_bytes, server_seen.code, steps);
    return ok;
}

bool RunSimulated(const settings::Registry& registry, usize large)
{
    bool ok = true;
    {
        SimWorld world;
        world.Net().SetDefaultLink(LinkFromSettings(registry));
        const Endpoint server_ep = world.Net().AllocateEndpoint();
        SimTransportConfig cfg;
        SimulatedTransport server(world.Net(), server_ep, cfg);
        SimulatedTransport client(world.Net(), world.Net().AllocateEndpoint(), cfg);
        ok = server.Listen(server_ep).Ok() && Converse("simulated", &world, &server, &client, server_ep, 0) && ok;
    }
    {
        SimWorld world;
        world.Net().SetDefaultLink(LinkFromSettings(registry));
        const Endpoint server_ep = world.Net().AllocateEndpoint();
        SimDatagramPort server_port(world.Net(), server_ep);
        SimDatagramPort client_port(world.Net(), world.Net().AllocateEndpoint());
        const UdpTransportConfig cfg = UdpConfigFromSettings(registry);
        UdpTransport server(server_port, world.GetClock(), cfg);
        UdpTransport client(client_port, world.GetClock(), cfg);
        ok = server.Listen(server_ep).Ok() && Converse("udp", &world, &server, &client, server_ep, large) && ok;
    }
    {
        SimNetwork time(9);
        SimStreamNetwork streams(time.GetClock());
        class TimeWorld final : public World
        {
        public:
            explicit TimeWorld(SimNetwork* n) : n_(n) {}
            const Clock& GetClock() override { return n_->GetClock(); }
            void Step() override { n_->Advance(1'000'000); }
            SimNetwork* n_;
        } world(&time);
        const Endpoint server_ep = streams.AllocateEndpoint();
        UniquePtr<StreamProvider> server_provider = streams.MakeProvider(server_ep);
        UniquePtr<StreamProvider> client_provider = streams.MakeProvider(streams.AllocateEndpoint());
        const TcpTransportConfig cfg = TcpConfigFromSettings(registry);
        TcpTransport server(*server_provider, time.GetClock(), cfg);
        TcpTransport client(*client_provider, time.GetClock(), cfg);
        ok = server.Listen(server_ep).Ok() && Converse("tcp", &world, &server, &client, server_ep, large) && ok;
    }
    return ok;
}

bool RunLoopback(const settings::Registry& registry, usize large)
{
    bool ok = true;
    Endpoint any;
    if (Endpoint::Parse(StringView("127.0.0.1:0"), &any).Failed())
    {
        return false;
    }
    {
        RealWorld world;
        SocketDatagramPort server_port;
        SocketDatagramPort client_port;
        if (SocketDatagramPort::Open(any, &server_port).Failed() ||
            SocketDatagramPort::Open(any, &client_port).Failed())
        {
            os::PrintError("net_demo: no loopback UDP socket here\n");
            return false;
        }
        const UdpTransportConfig cfg = UdpConfigFromSettings(registry);
        UdpTransport server(server_port, world.GetClock(), cfg);
        UdpTransport client(client_port, world.GetClock(), cfg);
        ok = server.Listen(server_port.LocalEndpoint()).Ok() &&
             Converse("udp", &world, &server, &client, server_port.LocalEndpoint(), large) && ok;
    }
    {
        RealWorld world;
        SocketStreamProvider server_provider;
        SocketStreamProvider client_provider;
        const TcpTransportConfig cfg = TcpConfigFromSettings(registry);
        TcpTransport server(server_provider, world.GetClock(), cfg);
        TcpTransport client(client_provider, world.GetClock(), cfg);
        if (server.Listen(any).Failed())
        {
            os::PrintError("net_demo: no loopback TCP socket here\n");
            return false;
        }
        ok = Converse("tcp", &world, &server, &client, server_provider.LocalEndpoint(), large) && ok;
    }
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    const debug::ProgramDebugSetup debug_setup = debug::StartProgramDebugging(argc, argv);
    debug::RunCrashTest(debug_setup.crash_test);
    bool simulated = false;
    usize large = 262144;
    static const os::CommandLineOption kCliOptions[] = {
        {"simulated", false}, {"large", true}, debug::kCrashHandlerOption, debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), argc, argv,
                                              Span<const os::CommandLineOption>(kCliOptions, 4));
    const char* program = line.ProgramName().Data();
    if (!line.Ok() || line.ArgCount() != 0)
    {
        os::PrintError("usage: %s [--simulated] [--large N]\n", program);
        return 2;
    }
    simulated = line.HasFlag("simulated");
    StringView value;
    if (line.TryGetOption("large", value))
    {
        large = static_cast<usize>(CStrToLong(value.Data()));
    }
    settings::Registry registry;
    RegisterNetSettings(&registry);
    const bool ok = simulated ? RunSimulated(registry, large) : RunLoopback(registry, large);
    os::Print(ok ? "net_demo: ok\n" : "net_demo: FAILED\n");
    return ok ? 0 : 1;
}
