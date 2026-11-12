// Deterministic fuzzing of everything that reads bytes from the network (Phase 20 Day 279): a seeded generator makes
// random and mutated inputs, a million cases for each of the pure parsers. No dependency, no corpus on disk. Nothing
// may crash, read out of bounds (the asan flavor watches), accept what is invalid, or keep more than its limits.

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "lang/utility.h"
#include "mem/heap_allocator.h"
#include "net/address.h"
#include "net/codec.h"
#include "net/endpoint.h"
#include "net/frame.h"
#include "net/sim_datagram.h"
#include "net/sim_network.h"
#include "net/sim_stream.h"
#include "net/tcp_transport.h"
#include "net/udp_protocol.h"
#include "net/udp_transport.h"
#include "transport_conformance.h"

namespace engine_placeholder::net
{
namespace
{

constexpr u32 kFuzzCases = 1'000'000;

struct FzRng
{
    explicit FzRng(u64 seed) : s(seed * 2685821657736338717ull + 1442695040888963407ull) {}
    u64 Next()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ull;
    }
    u32 Below(u32 n) { return n == 0 ? 0 : static_cast<u32>(Next() % n); }
    u8 Byte() { return static_cast<u8>(Next() >> 32); }
    u64 s;
};

void FzCopy(const Vector<byte>& from, Vector<byte>* to)
{
    to->Clear();
    for (usize i = 0; i < from.Size(); ++i)
    {
        to->Push(from[i]);
    }
}

// Changes a few bytes, inserts or removes some: the way a valid input becomes a nearly valid one.
void Mutate(FzRng& rng, Vector<byte>* v)
{
    const u32 edits = 1 + rng.Below(4);
    for (u32 e = 0; e < edits; ++e)
    {
        switch (rng.Below(5))
        {
        case 0:
            if (v->Size() != 0)
            {
                (*v)[rng.Below(static_cast<u32>(v->Size()))] = static_cast<byte>(rng.Byte());
            }
            break;
        case 1:
            if (v->Size() != 0)
            {
                (*v)[rng.Below(static_cast<u32>(v->Size()))] ^= static_cast<byte>(1u << rng.Below(8));
            }
            break;
        case 2:
            if (v->Size() != 0)
            {
                v->Resize(rng.Below(static_cast<u32>(v->Size())));
            }
            break;
        case 3: v->Push(static_cast<byte>(rng.Byte())); break;
        default:
            if (v->Size() != 0)
            {
                (*v)[rng.Below(static_cast<u32>(v->Size()))] = static_cast<byte>(rng.Below(2) == 0 ? 0x00 : 0xff);
            }
            break;
        }
    }
}

}  // namespace

TEST_CASE("Fuzz: the address and endpoint parsers accept only what they can print back", "[net][fuzz]")
{
    static const char* const kSeeds[] = {
        "127.0.0.1", "10.255.0.1",    "::1",           "fe80::1%eth0", "2001:db8::ff00:42:8329",
        "[::1]:80",  "1.2.3.4:65535", "[fe80::1%1]:9", "0.0.0.0:0",    "::ffff:1.2.3.4"};
    static const char kAlphabet[] = "0123456789abcdefABCDEF:.[]%/-_ xg";
    FzRng rng(1);
    u32 accepted = 0;
    for (u32 i = 0; i < kFuzzCases; ++i)
    {
        char text[80];
        usize n = 0;
        if (rng.Below(2) == 0)
        {
            const char* seed = kSeeds[rng.Below(10)];
            while (seed[n] != 0)
            {
                text[n] = seed[n];
                ++n;
            }
            const u32 edits = rng.Below(4);
            for (u32 e = 0; e < edits && n != 0; ++e)
            {
                const usize at = rng.Below(static_cast<u32>(n));
                if (rng.Below(3) == 0)
                {
                    n = at;
                }
                else
                {
                    text[at] = kAlphabet[rng.Below(sizeof(kAlphabet) - 1)];
                }
            }
        }
        else
        {
            n = rng.Below(60);
            for (usize k = 0; k < n; ++k)
            {
                text[k] = rng.Below(8) == 0 ? static_cast<char>(rng.Byte())
                                            : kAlphabet[rng.Below(sizeof(kAlphabet) - 1)];
            }
        }
        Address a;
        if (Address::Parse(StringView(text, n), &a).Ok())
        {
            ++accepted;
            const AddressText printed = a.Format();
            Address again;
            REQUIRE(Address::Parse(StringView(printed.chars.Data(), printed.length), &again).Ok());
            REQUIRE(again == a);
        }
        Endpoint e;
        if (Endpoint::Parse(StringView(text, n), &e).Ok())
        {
            const EndpointText printed = e.Format();
            Endpoint again;
            REQUIRE(Endpoint::Parse(StringView(printed.chars.Data(), printed.length), &again).Ok());
            REQUIRE(again == e);
        }
    }
    CHECK(accepted > 1000);  // the mutations of valid seeds reach the accepting paths too
}

TEST_CASE("Fuzz: the frame reader never yields a frame above its limit, however the bytes are cut", "[net][fuzz]")
{
    FzRng rng(2);
    constexpr usize kMax = 300;
    u64 frames = 0;
    for (u32 i = 0; i < kFuzzCases / 10; ++i)
    {
        FrameReader reader(kMax);
        FrameWriter writer(kMax);
        Vector<byte> stream;
        const u32 count = rng.Below(4);
        for (u32 f = 0; f < count; ++f)
        {
            Vector<byte> payload;
            const u32 len = 1 + rng.Below(kMax);
            for (u32 k = 0; k < len; ++k)
            {
                payload.Push(static_cast<byte>(rng.Byte()));
            }
            REQUIRE(writer.Encode(ByteSpan(payload.Data(), payload.Size()), &stream).Ok());
        }
        if (rng.Below(2) == 0)
        {
            Mutate(rng, &stream);
        }
        if (rng.Below(4) == 0)
        {
            for (u32 k = 0; k < 8; ++k)
            {
                stream.Push(static_cast<byte>(rng.Byte()));
            }
        }
        usize at = 0;
        bool broken = false;
        while (at < stream.Size() && !broken)
        {
            const usize chunk = 1 + rng.Below(40);
            const usize take = chunk < stream.Size() - at ? chunk : stream.Size() - at;
            usize used = 0;
            if (reader.Consume(ByteSpan(stream.Data() + at, take), &used).Failed())
            {
                broken = true;
                break;
            }
            REQUIRE(used <= take);
            at += used;
            if (reader.HasFrame())
            {
                REQUIRE(reader.Frame().Size() <= kMax);
                REQUIRE(reader.Frame().Size() >= 1);
                ++frames;
                reader.Pop();
            }
            else if (used == 0)
            {
                break;
            }
        }
    }
    CHECK(frames > 1000);
}

TEST_CASE("Fuzz: the packet header readers and the sequence windows hold their invariants", "[net][fuzz]")
{
    FzRng rng(3);
    udp::ReceiveWindow window;
    udp::SendLog log;
    u16 sent = 0;
    u32 sent_count = 0;
    u64 newly_total = 0;
    for (u32 i = 0; i < kFuzzCases; ++i)
    {
        Vector<byte> packet;
        const u32 n = rng.Below(40);
        for (u32 k = 0; k < n; ++k)
        {
            packet.Push(static_cast<byte>(rng.Byte()));
        }
        udp::PacketKind kind = udp::PacketKind::Payload;
        const bool known = n != 0 && udp::ParseFirstByte(static_cast<u8>(packet[0]), &kind);
        if (known && kind == udp::PacketKind::Payload)
        {
            udp::PayloadHeader h;
            ByteSpan body;
            if (udp::ReadPayloadHeader(ByteSpan(packet.Data(), packet.Size()), &h, &body))
            {
                REQUIRE(body.Size() <= packet.Size());
                window.OnReceive(h.sequence);
                // A lying acknowledgement may name packets never sent: it may not acknowledge more than were sent.
                Vector<u16> newly;
                const u32 count = log.OnAck(h.ack, h.ack_bits, &newly);
                REQUIRE(count == newly.Size());
                newly_total += count;
                REQUIRE(newly_total <= sent_count);
            }
        }
        if (rng.Below(8) == 0)
        {
            log.OnSent(sent++);
            ++sent_count;
        }
    }
    CHECK(window.Any());
}

TEST_CASE("Fuzz: NetReader over random operation lists and random bytes stays inside its buffer", "[net][fuzz]")
{
    FzRng rng(4);
    u64 ok_reads = 0;
    for (u32 i = 0; i < kFuzzCases / 4; ++i)
    {
        Vector<byte> data;
        const u32 n = rng.Below(48);
        for (u32 k = 0; k < n; ++k)
        {
            data.Push(static_cast<byte>(rng.Byte()));
        }
        NetReader r{ByteSpan(data.Data(), data.Size())};
        const u32 ops = 1 + rng.Below(12);
        for (u32 o = 0; o < ops; ++o)
        {
            CodecError e = CodecError::Ok;
            switch (rng.Below(14))
            {
            case 0:
            {
                u64 v;
                e = r.ReadBits(1 + rng.Below(64), &v);
                break;
            }
            case 1:
            {
                bool v;
                e = r.ReadBool(&v);
                break;
            }
            case 2:
            {
                i64 v;
                const i64 lo = static_cast<i64>(rng.Next()) >> rng.Below(63);
                e = r.ReadRange(lo, lo + static_cast<i64>(rng.Below(1000)), &v);
                break;
            }
            case 3:
            {
                u32 v;
                e = r.ReadU32(&v);
                break;
            }
            case 4:
            {
                u64 v;
                e = r.ReadVarint(&v);
                break;
            }
            case 5:
            {
                i64 v;
                e = r.ReadSignedVarint(&v);
                break;
            }
            case 6:
            {
                Vector<byte> v;
                e = r.ReadBytes(rng.Below(100), &v);
                break;
            }
            case 7:
            {
                String<HeapAllocator> v(HeapAllocator::Default());
                e = r.ReadString(rng.Below(100), &v);
                break;
            }
            case 8:
            {
                f32 v;
                e = r.ReadF32(&v);
                break;
            }
            case 9:
            {
                f32 v;
                e = r.ReadQuantized(-10.0f, 10.0f, 1 + rng.Below(32), &v);
                break;
            }
            case 10:
            {
                Vec3 v;
                e = r.ReadVec3(-1.0f, 1.0f, 1 + rng.Below(16), &v);
                break;
            }
            case 11:
            {
                Quaternion v;
                e = r.ReadQuaternion(2 + rng.Below(14), &v);
                break;
            }
            case 12:
            {
                i64 v;
                e = r.ReadDeltaInt(static_cast<i64>(rng.Next()), &v);
                break;
            }
            default: e = r.AlignToByte(); break;
            }
            ok_reads += e == CodecError::Ok ? 1 : 0;
            REQUIRE(r.BitPosition() <= data.Size() * 8);
            if (e != CodecError::Ok)
            {
                break;
            }
        }
    }
    CHECK(ok_reads > 1000);
}

namespace
{

Endpoint FzEp(const char* text)
{
    Endpoint e;
    REQUIRE(Endpoint::Parse(StringView(text), &e).Ok());
    return e;
}

}  // namespace

TEST_CASE("Fuzz: random and mutated datagrams cannot make a UDP server hold more than its limits", "[net][fuzz][udp]")
{
    SimNetwork net(77);
    const Endpoint server_ep = FzEp("127.0.0.1:9000");
    SimDatagramPort server_port(net, server_ep);
    UdpTransportConfig cfg;
    cfg.seed = 5;
    cfg.max_connections = 64;
    cfg.max_receive_buffer_bytes = 1u << 20;
    cfg.max_total_receive_buffer_bytes = 4u << 20;
    cfg.handshake_limits = RateLimiterConfig{};
    cfg.handshake_limits.strikes_to_ban = 0;
    UdpTransport server(server_port, net.GetClock(), cfg);
    REQUIRE(server.Listen(server_ep).Ok());
    SimDatagramPort attacker_port(net, FzEp("127.0.0.1:9001"));
    // One genuine client, to give the fuzz valid handshakes and payloads to mutate.
    SimDatagramPort client_port(net, FzEp("127.0.0.1:9002"));
    UdpTransport client(client_port, net.GetClock(), cfg);
    ConnectionId cc = 0;
    REQUIRE(client.Connect(server_ep, &cc).Ok());
    Vector<Vector<byte>> seeds;
    FzRng rng(5);
    for (u32 t = 0; t < 400; ++t)
    {
        net.Advance(1'000'000);
        Event e;
        while (server.Poll(&e))
        {
        }
        while (client.Poll(&e))
        {
            if (e.kind == EventKind::Opened)
            {
                byte m[300];
                for (usize k = 0; k < sizeof(m); ++k)
                {
                    m[k] = static_cast<byte>(k);
                }
                REQUIRE(client.Send(cc, 0, ByteSpan(m, sizeof(m)), Reliability::ReliableOrdered).Ok());
            }
        }
    }
    // Capture valid packets by sending a few handshake-shaped and payload-shaped packets by hand.
    for (u32 kind = 0; kind < 8; ++kind)
    {
        Vector<byte> p;
        p.Push(static_cast<byte>(udp::FirstByte(static_cast<udp::PacketKind>(kind))));
        for (u32 k = 0; k < 40; ++k)
        {
            p.Push(static_cast<byte>(rng.Byte()));
        }
        seeds.Push(Move(p));
    }
    Vector<byte> full;
    full.Resize(udp::kHandshakeBytes);
    full[0] = static_cast<byte>(udp::FirstByte(udp::PacketKind::ConnectRequest));
    seeds.Push(Move(full));
    u64 events = 0;
    for (u32 i = 0; i < 100000; ++i)
    {
        Vector<byte> p;
        if (rng.Below(3) == 0)
        {
            const u32 n = rng.Below(udp::kMtu + 20);
            for (u32 k = 0; k < n; ++k)
            {
                p.Push(static_cast<byte>(rng.Byte()));
            }
        }
        else
        {
            FzCopy(seeds[rng.Below(static_cast<u32>(seeds.Size()))], &p);
            Mutate(rng, &p);
        }
        (void)attacker_port.SendTo(ByteSpan(p.Data(), p.Size()), server_ep);
        if (i % 16 == 0)
        {
            net.Advance(1'000'000);
            Event e;
            while (server.Poll(&e))
            {
                ++events;
            }
            REQUIRE(server.ConnectionCount() <= cfg.max_connections);
        }
    }
    CHECK(server.ConnectionCount() <= cfg.max_connections);
    CHECK(server.Counters().datagrams_received > 1000);
    CHECK(events < 1000000);
}

TEST_CASE("Fuzz: random and mutated streams cannot make a TCP server hold more than its limits", "[net][fuzz][tcp]")
{
    SimNetwork time(9);
    SimStreamNetwork streams(time.GetClock());
    const Endpoint server_ep = streams.AllocateEndpoint();
    UniquePtr<StreamProvider> server_provider = streams.MakeProvider(server_ep);
    TcpTransportConfig cfg;
    cfg.max_message_bytes = 2048;
    cfg.max_connections = 32;
    cfg.handshake_timeout_nanos = 50'000'000;
    cfg.accept_limits = RateLimiterConfig{};
    cfg.accept_limits.strikes_to_ban = 0;
    cfg.max_connections_per_address = 1000;
    TcpTransport server(*server_provider, time.GetClock(), cfg);
    REQUIRE(server.Listen(server_ep).Ok());
    UniquePtr<StreamProvider> raw = streams.MakeProvider(streams.AllocateEndpoint());
    // A valid Hello and a valid Data frame to mutate.
    const FrameWriter writer(cfg.max_message_bytes + 16);
    Vector<byte> hello_payload;
    hello_payload.Push(static_cast<byte>(1));
    hello_payload.Push(static_cast<byte>(1));
    hello_payload.Push(static_cast<byte>(4));
    for (u32 k = 0; k < 4; ++k)
    {
        hello_payload.Push(static_cast<byte>(k == 1 ? 8 : 0));
    }
    Vector<byte> good;
    REQUIRE(writer.Encode(ByteSpan(hello_payload.Data(), hello_payload.Size()), &good).Ok());
    Vector<byte> data_payload;
    data_payload.Push(static_cast<byte>(2));
    data_payload.Push(static_cast<byte>(0));
    data_payload.Push(static_cast<byte>(1));
    for (u32 k = 0; k < 20; ++k)
    {
        data_payload.Push(static_cast<byte>(k));
    }
    REQUIRE(writer.Encode(ByteSpan(data_payload.Data(), data_payload.Size()), &good).Ok());
    FzRng rng(6);
    for (u32 i = 0; i < 20000; ++i)
    {
        UniquePtr<StreamIo> io;
        if (!raw->Connect(server_ep, &io).Ok())
        {
            continue;
        }
        Vector<byte> bytes;
        if (rng.Below(3) == 0)
        {
            const u32 n = rng.Below(200);
            for (u32 k = 0; k < n; ++k)
            {
                bytes.Push(static_cast<byte>(rng.Byte()));
            }
        }
        else
        {
            FzCopy(good, &bytes);
            Mutate(rng, &bytes);
        }
        time.Advance(11'000'000);  // the connection completes after two latencies
        usize at = 0;
        while (at < bytes.Size())
        {
            usize sent = 0;
            if (io->Send(ByteSpan(bytes.Data() + at, bytes.Size() - at), &sent).Failed() || sent == 0)
            {
                break;
            }
            at += sent;
        }
        time.Advance(11'000'000);
        Event e;
        while (server.Poll(&e))
        {
        }
        REQUIRE(server.ConnectionCount() <= cfg.max_connections);
    }
    CHECK(server.ConnectionCount() <= cfg.max_connections);
}

}  // namespace engine_placeholder::net
