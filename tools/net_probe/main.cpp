// net_probe: Phase 20 Day 281. A command-line tool for checking the transports by hand, over real sockets.
//
//   net_probe echo   --transport=udp|tcp --listen=ADDR:PORT [--seconds=N]   echoes every message back (N seconds,
//   default 60) net_probe ping   --transport=udp|tcp --connect=ADDR:PORT [--count=N] [--size=BYTES]
//                                            round trips of N messages (default 100, 64 bytes), p50/p99/p999 latency
//   net_probe send   --transport=udp|tcp --connect=ADDR:PORT --file=PATH   sends a file in messages and waits for the
//   echo net_probe resolve ADDR[:PORT]            parses an address or endpoint and prints its canonical form
//
// Host names are not resolved by this tool (the resolver needs a reactor; use an address). Its options are its own
// command line, not engine settings.
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"
#include "io/file.h"
#include "io/path.h"
#include "lang/utility.h"
#include "net/clock.h"
#include "net/endpoint.h"
#include "net/socket_stream.h"
#include "net/tcp_transport.h"
#include "net/udp_port.h"
#include "net/udp_transport.h"
#include "containers/span.h"
#include "os/command_line.h"
#include "os/console.h"
#include "algorithm.h"
#include "sync/thread.h"

using namespace engine_placeholder;
using namespace engine_placeholder::net;

namespace
{

int Usage()
{
    os::PrintError("usage: net_probe echo --transport=udp|tcp --listen=ADDR:PORT [--seconds=N]\n"
                   "       net_probe ping --transport=udp|tcp --connect=ADDR:PORT [--count=N] [--size=BYTES]\n"
                   "       net_probe send --transport=udp|tcp --connect=ADDR:PORT --file=PATH\n"
                   "       net_probe resolve ADDR[:PORT]\n");
    return 64;
}

constexpr os::CommandLineOption kOptions[] = {{"transport", true}, {"listen", true}, {"seconds", true},
                                              {"connect", true},   {"count", true},  {"size", true},
                                              {"file", true}};

using Line = os::CommandLine<HeapAllocator>;

// A transport on real sockets: the port or provider it needs lives beside it.
struct Rig
{
    bool tcp = false;
    SocketDatagramPort port;
    SocketStreamProvider provider;
    MonotonicClock clock;
    UniquePtr<UdpTransport> udp;
    UniquePtr<TcpTransport> stream;
    Endpoint local;

    Transport* Make(bool use_tcp, const Endpoint& bind)
    {
        tcp = use_tcp;
        if (tcp)
        {
            stream = MakeUnique<TcpTransport>(provider, clock);
            local = bind;
            return stream.Get();
        }
        if (SocketDatagramPort::Open(bind, &port).Failed())
        {
            return nullptr;
        }
        udp = MakeUnique<UdpTransport>(port, clock);
        local = port.LocalEndpoint();
        return udp.Get();
    }
};

bool ParseEndpoint(const char* text, Endpoint* out)
{
    if (text == nullptr || Endpoint::Parse(StringView(text), out).Failed())
    {
        os::PrintError("net_probe: not an address:port: %s\n", text == nullptr ? "(missing)" : text);
        return false;
    }
    return true;
}

int Echo(const Line& line)
{
    const char* transport = line.OptionOr("transport", nullptr);
    const char* listen = line.OptionOr("listen", nullptr);
    const char* seconds_text = line.OptionOr("seconds", nullptr);
    Endpoint at;
    if (transport == nullptr || !ParseEndpoint(listen, &at))
    {
        return Usage();
    }
    Rig rig;
    Transport* t = rig.Make(CStrCompare(transport, "tcp") == 0, at);
    if (t == nullptr || t->Listen(rig.local).Failed())
    {
        os::PrintError("net_probe: cannot listen on %s\n", listen);
        return 1;
    }
    os::Print("net_probe: echo listening on %s\n", rig.local.Format().chars.Data());
    const u64 until = rig.clock.NowNanos() +
                      (seconds_text != nullptr ? static_cast<u64>(CStrToLong(seconds_text)) : 60) * 1'000'000'000ull;
    u64 messages = 0;
    u64 bytes = 0;
    while (rig.clock.NowNanos() < until)
    {
        Event e;
        while (t->Poll(&e))
        {
            if (e.kind == EventKind::Message)
            {
                ++messages;
                bytes += e.message.Size();
                (void)t->Send(e.connection, e.channel, e.message, Reliability::ReliableOrdered);
            }
        }
        ThisThread::Yield();
    }
    os::Print("net_probe: echoed %llu messages, %llu bytes\n", static_cast<unsigned long long>(messages),
              static_cast<unsigned long long>(bytes));
    return 0;
}

// Connects and waits for the connection to open; null on failure.
Transport* ConnectTo(Rig* rig, const Line& line, ConnectionId* connection)
{
    const char* transport = line.OptionOr("transport", nullptr);
    Endpoint to;
    Endpoint any;
    if (transport == nullptr || !ParseEndpoint(line.OptionOr("connect", nullptr), &to) ||
        Endpoint::Parse(StringView(to.GetAddress().IsV6() ? "[::]:0" : "0.0.0.0:0"), &any).Failed())
    {
        return nullptr;
    }
    Transport* t = rig->Make(CStrCompare(transport, "tcp") == 0, any);
    if (t == nullptr || t->Connect(to, connection).Failed())
    {
        os::PrintError("net_probe: cannot connect\n");
        return nullptr;
    }
    const u64 until = rig->clock.NowNanos() + 5'000'000'000ull;
    while (rig->clock.NowNanos() < until)
    {
        Event e;
        while (t->Poll(&e))
        {
            if (e.kind == EventKind::Opened)
            {
                return t;
            }
            if (e.kind == EventKind::Closed)
            {
                os::PrintError("net_probe: connection closed (%s)\n", DisconnectReasonName(e.reason));
                return nullptr;
            }
        }
        ThisThread::Yield();
    }
    os::PrintError("net_probe: connect timed out\n");
    return nullptr;
}

int Ping(const Line& line)
{
    const char* count_text = line.OptionOr("count", nullptr);
    const char* size_text = line.OptionOr("size", nullptr);
    const u32 count = count_text != nullptr ? static_cast<u32>(CStrToLong(count_text)) : 100;
    usize size = size_text != nullptr ? static_cast<usize>(CStrToLong(size_text)) : 64;
    size = size < 8 ? 8 : size;
    Rig rig;
    ConnectionId c = 0;
    Transport* t = ConnectTo(&rig, line, &c);
    if (t == nullptr)
    {
        return 1;
    }
    Vector<u64> micros;
    Vector<byte> payload;
    payload.Resize(size);
    for (u32 i = 0; i < count; ++i)
    {
        const u64 start = rig.clock.NowNanos();
        for (u32 b = 0; b < 8; ++b)
        {
            payload[b] = static_cast<byte>(i >> (8 * b));
        }
        if (t->Send(c, 0, ByteSpan(payload.Data(), payload.Size()), Reliability::ReliableOrdered).Failed())
        {
            os::PrintError("net_probe: send failed\n");
            return 1;
        }
        bool got = false;
        while (!got && rig.clock.NowNanos() - start < 5'000'000'000ull)
        {
            Event e;
            while (t->Poll(&e))
            {
                got = got || e.kind == EventKind::Message;
            }
            ThisThread::Yield();
        }
        if (!got)
        {
            os::PrintError("net_probe: no answer to message %u\n", i);
            return 1;
        }
        micros.Push((rig.clock.NowNanos() - start) / 1000);
    }
    if (micros.Size() == 0)
    {
        os::PrintError("net_probe: --count must be at least 1\n");
        return 64;
    }
    Sort(Span<u64>(micros.Data(), micros.Size()));
    const auto at = [&](f64 p)
    {
        usize k = static_cast<usize>(p * static_cast<f64>(micros.Size()));
        k = k >= micros.Size() ? micros.Size() - 1 : k;
        return static_cast<unsigned long long>(micros[k]);
    };
    os::Print("net_probe: %u round trips of %zu bytes: p50 %llu us, p99 %llu us, p999 %llu us, max %llu us\n", count,
              size, at(0.5), at(0.99), at(0.999), static_cast<unsigned long long>(micros[micros.Size() - 1]));
    return 0;
}

int SendFile(const Line& line)
{
    const char* file = line.OptionOr("file", nullptr);
    if (file == nullptr)
    {
        return Usage();
    }
    Vector<u8> data;
    if (io::ReadFile(io::Path(StringView(file)), &data).Failed())
    {
        os::PrintError("net_probe: cannot read %s\n", file);
        return 1;
    }
    Rig rig;
    ConnectionId c = 0;
    Transport* t = ConnectTo(&rig, line, &c);
    if (t == nullptr)
    {
        return 1;
    }
    const usize chunk = t->MaxMessageBytes() < 16384 ? t->MaxMessageBytes() : 16384;
    const u64 start = rig.clock.NowNanos();
    usize sent = 0;
    usize echoed = 0;
    while (echoed < data.Size() && rig.clock.NowNanos() - start < 120'000'000'000ull)
    {
        while (sent < data.Size())
        {
            const usize n = data.Size() - sent < chunk ? data.Size() - sent : chunk;
            const Status s = t->Send(c, 0, ByteSpan(reinterpret_cast<const byte*>(data.Data()) + sent, n),
                                     Reliability::ReliableOrdered);
            if (s.Is(ErrorCode::LimitExceeded))
            {
                break;  // the queue is full: wait for echoes
            }
            if (s.Failed())
            {
                os::PrintError("net_probe: send failed\n");
                return 1;
            }
            sent += n;
        }
        Event e;
        while (t->Poll(&e))
        {
            if (e.kind == EventKind::Message)
            {
                echoed += e.message.Size();
            }
            else if (e.kind == EventKind::Closed)
            {
                os::PrintError("net_probe: closed (%s)\n", DisconnectReasonName(e.reason));
                return 1;
            }
        }
        ThisThread::Yield();
    }
    const f64 seconds = static_cast<f64>(rig.clock.NowNanos() - start) / 1e9;
    os::Print("net_probe: sent %zu bytes, %zu echoed back in %.3f s (%.1f MiB/s)\n", sent, echoed, seconds,
              static_cast<f64>(echoed) / (1024.0 * 1024.0) / (seconds > 0 ? seconds : 1e-9));
    return echoed == data.Size() ? 0 : 1;
}

int ResolveCommand(const Line& line)
{
    if (line.ArgCount() != 2)
    {
        return Usage();
    }
    const char* text = line.Arg(1).Data();
    Endpoint e;
    if (Endpoint::Parse(StringView(text), &e).Ok())
    {
        os::Print("%s\n", e.Format().chars.Data());
        return 0;
    }
    Address a;
    if (Address::Parse(StringView(text), &a).Ok())
    {
        os::Print("%s\n", a.Format().chars.Data());
        return 0;
    }
    os::PrintError("net_probe: not an address: %s (host names are not resolved by this tool)\n", text);
    return 1;
}

}  // namespace

int main(int argc, char** argv)
{
    const Line line(HeapAllocator::Default(), argc, argv, Span<const os::CommandLineOption>(kOptions, 7));
    if (!line.Ok() || line.ArgCount() < 1)
    {
        if (!line.Ok())
        {
            os::PrintError("net_probe: %s\n", line.Error().Data());
        }
        return Usage();
    }
    const StringView command = line.Arg(0);
    if (command == StringView("echo"))
    {
        return Echo(line);
    }
    if (command == StringView("ping"))
    {
        return Ping(line);
    }
    if (command == StringView("send"))
    {
        return SendFile(line);
    }
    if (command == StringView("resolve"))
    {
        return ResolveCommand(line);
    }
    return Usage();
}
