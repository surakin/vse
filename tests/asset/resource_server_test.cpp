// The resource server and its protocol (Phase 22 Day 331): a server over a host-memory backend and raw protocol clients
// over simulated TCP streams: stat, tables and lists in pages, ranged reads, the limits, malformed and abusive clients,
// 200 clients at once, and a fuzz of random and mutated requests against a live server.

#include <catch2/catch_test_macros.hpp>

#include "asset/asset_settings.h"
#include "asset/memory_backend.h"
#include "asset/resource_protocol.h"
#include "asset/resource_server.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "net/clock.h"
#include "net/sim_stream.h"
#include "net/tcp_transport.h"
#include "settings/registry.h"
#include "support.h"

namespace engine_placeholder::asset
{

namespace
{

using test::AsBytes;
using test::Rid;

// A raw protocol client: a transport, a connection to the server and the messages that came back.
struct RsClient
{
    RsClient(net::SimStreamNetwork& streams, const net::Clock& clock, const net::TcpTransportConfig& config,
             const net::Endpoint& server)
        : provider(streams.MakeProvider(streams.AllocateEndpoint())), transport(*provider, clock, config)
    {
        REQUIRE(transport.Connect(server, &connection).Ok());
    }
    void Poll()
    {
        net::Event event;
        while (transport.Poll(&event))
        {
            if (event.kind == net::EventKind::Opened)
            {
                open = true;
            }
            else if (event.kind == net::EventKind::Closed)
            {
                closed = true;
                open = false;
            }
            else if (event.kind == net::EventKind::Message)
            {
                ProtoData data;
                if (DecodeProto(event.message, 1u << 20, &data) == ProtoError::None)
                {
                    responses.Push(static_cast<ProtoData&&>(data));
                }
                else
                {
                    undecodable += 1;
                }
            }
        }
    }
    void Send(const ProtoData& message)
    {
        byte buffer[4096];
        usize size = 0;
        REQUIRE(EncodeProto(message, MutableByteSpan(buffer, sizeof(buffer)), &size) == ProtoError::None);
        (void)transport.Send(connection, 0, ByteSpan(buffer, size), net::Reliability::ReliableOrdered);
    }
    void SendRaw(ByteSpan bytes)
    {
        if (bytes.Size() != 0)
        {
            (void)transport.Send(connection, 0, bytes, net::Reliability::ReliableOrdered);
        }
    }
    // The response to `request`, or null.
    const ProtoData* ResponseTo(u32 request) const
    {
        for (usize i = 0; i < responses.Size(); ++i)
        {
            if (responses[i].request == request)
            {
                return &responses[i];
            }
        }
        return nullptr;
    }

    UniquePtr<net::StreamProvider> provider;
    net::TcpTransport transport;
    net::ConnectionId connection = 0;
    bool open = false;
    bool closed = false;
    u32 undecodable = 0;
    Vector<ProtoData> responses;
};

struct RsRig
{
    explicit RsRig(const ResourceServerConfig& config = ResourceServerConfig(), usize message_bytes = 1u << 20)
        : message_bytes_(message_bytes), streams(clock), backend(types, u64{64} << 20),
          server_provider(streams.MakeProvider(streams.AllocateEndpoint()))
    {
        REQUIRE(types.Register(test::kBackendTestType, 1, "backend test").Ok());
        endpoint = streams.AllocateEndpoint();
        server_provider = streams.MakeProvider(endpoint);
        transport = MakeUnique<net::TcpTransport>(*server_provider, clock, TransportConfig());
        REQUIRE(transport->Listen(endpoint).Ok());
        server = MakeUnique<ResourceServer>(backend, *transport, config);
    }
    net::TcpTransportConfig TransportConfig() const
    {
        net::TcpTransportConfig c;
        c.max_message_bytes = message_bytes_;
        c.accept_limits.total_per_second = 100000;
        c.accept_limits.total_burst = 100000;
        c.accept_limits.address_per_second = 100000;
        c.accept_limits.address_burst = 100000;
        return c;
    }
    void Add(u64 id, usize bytes, const char* path = nullptr)
    {
        const Vector<byte> blob = test::MakeTestBlob(types, Rid(id), bytes, static_cast<u32>(id));
        MemoryAddOptions options;
        StringView text;
        if (path != nullptr)
        {
            text = test::Vp(path);
            options.paths = Span<const StringView>(&text, 1);
        }
        REQUIRE(backend.Add(Rid(id), AsBytes(blob), options).Ok());
        blobs.Push(blob.Size());
    }
    void Pump(const Vector<RsClient*>& clients, u32 steps = 40)
    {
        for (u32 i = 0; i < steps; ++i)
        {
            clock.Advance(1'000'000);
            server->Update();
            for (usize c = 0; c < clients.Size(); ++c)
            {
                clients[c]->Poll();
            }
        }
    }
    void Pump(RsClient& client, u32 steps = 40)
    {
        Vector<RsClient*> one;
        one.Push(&client);
        Pump(one, steps);
    }
    UniquePtr<RsClient> Client() { return MakeUnique<RsClient>(streams, clock, TransportConfig(), endpoint); }
    // Runs the transports until the client's connection is open (the handshake takes a few round trips).
    void Open(RsClient& client)
    {
        for (u32 i = 0; i < 300 && !client.open; ++i)
        {
            Pump(client, 1);
        }
        REQUIRE(client.open);
    }
    // A client that has said Hello and been answered.
    UniquePtr<RsClient> Greeted(u32 takes = 1u << 20)
    {
        UniquePtr<RsClient> client = Client();
        Open(*client);
        ProtoData hello;
        hello.type = ProtoMessage::Hello;
        hello.request = 1;
        hello.version = kProtoVersion;
        hello.max_read_bytes = takes;
        client->Send(hello);
        Pump(*client, 40);
        REQUIRE(client->ResponseTo(1) != nullptr);
        REQUIRE(client->ResponseTo(1)->type == ProtoMessage::HelloAck);
        return client;
    }

    usize message_bytes_;
    net::ManualClock clock{1'000'000};
    TypeRegistry types;
    net::SimStreamNetwork streams;
    HostMemoryBackend backend;
    net::Endpoint endpoint;
    UniquePtr<net::StreamProvider> server_provider;
    UniquePtr<net::TcpTransport> transport;
    UniquePtr<ResourceServer> server;
    Vector<usize> blobs;
};

ProtoData Request(ProtoMessage type, u32 id)
{
    ProtoData d;
    d.type = type;
    d.request = id;
    return d;
}

}  // namespace

TEST_CASE("resource server: Hello, Stat and ranged reads, the last range short and one past the end refused",
          "[asset][server]")
{
    RsRig rig;
    rig.Add(1, 5000, "/a/one.bk");
    UniquePtr<RsClient> client = rig.Greeted();
    const ProtoData* ack = client->ResponseTo(1);
    CHECK(ack->version == kProtoVersion);
    CHECK(ack->max_read_bytes == rig.server->MaxReadBytes());

    ProtoData stat = Request(ProtoMessage::Stat, 2);
    stat.id = 1;
    client->Send(stat);
    ProtoData missing = Request(ProtoMessage::Stat, 3);
    missing.id = 99;
    client->Send(missing);
    rig.Pump(*client);
    REQUIRE(client->ResponseTo(2) != nullptr);
    CHECK(client->ResponseTo(2)->type == ProtoMessage::StatOk);
    const u64 size = client->ResponseTo(2)->size;
    CHECK(size == rig.blobs[0]);
    CHECK(client->ResponseTo(3)->error == ProtoError::NotFound);

    const Vector<byte> blob = test::MakeTestBlob(rig.types, Rid(1), 5000, 1);
    auto read = [&](u32 request, u64 offset, u32 length)
    {
        ProtoData r = Request(ProtoMessage::Read, request);
        r.id = 1;
        r.offset = offset;
        r.length = length;
        client->Send(r);
        rig.Pump(*client);
        return client->ResponseTo(request);
    };
    const ProtoData* middle = read(10, 100, 200);
    REQUIRE(middle != nullptr);
    CHECK(middle->type == ProtoMessage::ReadOk);
    REQUIRE(middle->data.Size() == 200);
    CHECK(MemCompare(middle->data.Data(), blob.Data() + 100, 200) == 0);
    const ProtoData* last = read(11, size - 50, 4000);  // the range runs past the end: the bytes that are there
    REQUIRE(last != nullptr);
    CHECK(last->type == ProtoMessage::ReadOk);
    CHECK(last->data.Size() == 50);
    const ProtoData* at_end = read(12, size, 10);
    REQUIRE(at_end != nullptr);
    CHECK(at_end->type == ProtoMessage::ReadOk);
    CHECK(at_end->data.Size() == 0);
    CHECK(read(13, size + 1, 10)->error == ProtoError::Range);
    CHECK(read(14, 0, rig.server->MaxReadBytes() + 1)->error == ProtoError::TooLarge);
    ProtoData unknown = Request(ProtoMessage::Read, 15);
    unknown.id = 12345;
    unknown.length = 10;
    client->Send(unknown);
    rig.Pump(*client);
    CHECK(client->ResponseTo(15)->error == ProtoError::NotFound);
    CHECK_FALSE(client->closed);
    CHECK(rig.server->Stats().bytes_served == 250);
}

TEST_CASE("resource server: a client that takes less is served less, and a read over its limit is TooLarge",
          "[asset][server]")
{
    RsRig rig;
    rig.Add(1, 5000);
    UniquePtr<RsClient> client = rig.Greeted(1000);  // the client takes 1000 bytes at most
    CHECK(client->ResponseTo(1)->max_read_bytes == 1000);
    ProtoData r = Request(ProtoMessage::Read, 2);
    r.id = 1;
    r.length = 1001;
    client->Send(r);
    r.request = 3;
    r.length = 1000;
    client->Send(r);
    rig.Pump(*client);
    CHECK(client->ResponseTo(2)->error == ProtoError::TooLarge);
    CHECK(client->ResponseTo(3)->type == ProtoMessage::ReadOk);
}

TEST_CASE("resource server: the table comes in pages with a generation, and an unchanged table is not sent again",
          "[asset][server]")
{
    ResourceServerConfig config;
    config.page_entries = 4;
    RsRig rig(config);
    for (u64 id = 1; id <= 10; ++id)
    {
        char path[32];
        FormatTo(path, sizeof(path), "/t/%02u.bk", static_cast<unsigned>(id));
        rig.Add(id, 100, path);
    }
    UniquePtr<RsClient> client = rig.Greeted();
    u64 generation = 0;
    u32 seen = 0;
    for (u32 page = 0; page < 3; ++page)
    {
        ProtoData t = Request(ProtoMessage::Table, 10 + page);
        t.page = page;
        client->Send(t);
        rig.Pump(*client);
        const ProtoData* r = client->ResponseTo(10 + page);
        REQUIRE(r != nullptr);
        REQUIRE(r->type == ProtoMessage::TableOk);
        CHECK(r->page_count == 3);
        CHECK_FALSE(r->unchanged);
        CHECK(r->entries.Size() == (page < 2 ? 4u : 2u));
        generation = r->generation;
        seen += static_cast<u32>(r->entries.Size());
        CHECK(r->entries[0].path.Length() > 0);
        CHECK(r->entries[0].type_tag == test::kBackendTestType.Tag());
    }
    CHECK(seen == 10);
    CHECK(generation != 0);
    ProtoData again = Request(ProtoMessage::Table, 20);
    again.generation = generation;  // the client has it
    client->Send(again);
    ProtoData past = Request(ProtoMessage::Table, 21);
    past.page = 3;
    client->Send(past);
    rig.Pump(*client);
    CHECK(client->ResponseTo(20)->unchanged);
    CHECK(client->ResponseTo(20)->entries.Size() == 0);
    CHECK(client->ResponseTo(21)->error == ProtoError::Invalid);
    // The backend changes: the next request carries the new generation and the entries.
    rig.Add(11, 100, "/t/11.bk");
    client->Send(again);
    rig.Pump(*client);
    const ProtoData* changed = client->responses.Size() != 0 ? &client->responses[client->responses.Size() - 1]
                                                             : nullptr;
    REQUIRE(changed != nullptr);
    CHECK(changed->type == ProtoMessage::TableOk);
    CHECK(changed->generation != generation);
    CHECK_FALSE(changed->unchanged);
}

TEST_CASE("resource server: a list of a prefix comes in pages", "[asset][server]")
{
    ResourceServerConfig config;
    config.page_entries = 3;
    RsRig rig(config);
    for (u64 id = 1; id <= 7; ++id)
    {
        char path[32];
        FormatTo(path, sizeof(path), "/dir/%u.bk", static_cast<unsigned>(id));
        rig.Add(id, 100, path);
    }
    rig.Add(50, 100, "/other/x.bk");
    UniquePtr<RsClient> client = rig.Greeted();
    u32 total = 0;
    for (u32 page = 0; page < 3; ++page)
    {
        ProtoData l = Request(ProtoMessage::List, 10 + page);
        l.prefix = String<HeapAllocator>(HeapAllocator::Default(), "/dir");
        l.page = page;
        client->Send(l);
        rig.Pump(*client);
        const ProtoData* r = client->ResponseTo(10 + page);
        REQUIRE(r != nullptr);
        REQUIRE(r->type == ProtoMessage::ListOk);
        CHECK(r->more == (page < 2));
        total += static_cast<u32>(r->ids.Size());
    }
    CHECK(total == 7);
    ProtoData none = Request(ProtoMessage::List, 20);
    none.prefix = String<HeapAllocator>(HeapAllocator::Default(), "/nothing");
    client->Send(none);
    rig.Pump(*client);
    CHECK(client->ResponseTo(20)->type == ProtoMessage::ListOk);
    CHECK(client->ResponseTo(20)->ids.Size() == 0);
}

TEST_CASE("resource server: requests over the per-poll limit are answered Busy", "[asset][server]")
{
    ResourceServerConfig config;
    config.max_inflight = 4;
    RsRig rig(config);
    rig.Add(1, 100);
    UniquePtr<RsClient> client = rig.Greeted();
    for (u32 i = 0; i < 12; ++i)
    {
        ProtoData stat = Request(ProtoMessage::Stat, 100 + i);
        stat.id = 1;
        client->Send(stat);
    }
    rig.Pump(*client);
    u32 busy = 0;
    u32 answered = 0;
    for (u32 i = 0; i < 12; ++i)
    {
        const ProtoData* r = client->ResponseTo(100 + i);
        if (r != nullptr)
        {
            ++answered;
            busy += r->type == ProtoMessage::Error && r->error == ProtoError::Busy ? 1u : 0u;
        }
    }
    CHECK(answered == 12);  // every request is answered: with data or with Busy
    CHECK(busy >= 1);
    CHECK(rig.server->Stats().busy == busy);
}

TEST_CASE("resource server: a client that breaks the protocol is closed, and the others are not touched",
          "[asset][server]")
{
    RsRig rig;
    rig.Add(1, 100);
    UniquePtr<RsClient> good = rig.Greeted();

    auto closed_by = [&](auto&& act)
    {
        UniquePtr<RsClient> bad = rig.Client();
        rig.Open(*bad);
        act(*bad);
        rig.Pump(*bad, 20);
        return bad->closed;
    };
    CHECK(closed_by([](RsClient&) {}) == false);  // saying nothing is not an offence
    CHECK(closed_by(
        [](RsClient& c)
        {
            const byte garbage[5] = {0xff, 0xff, 0xff, 0xff, 0xff};
            c.SendRaw(ByteSpan(garbage, 5));
        }));
    CHECK(closed_by([](RsClient& c) { c.Send(Request(ProtoMessage::Stat, 1)); }));  // a request before Hello
    CHECK(closed_by(
        [&](RsClient& c)
        {
            ProtoData hello = Request(ProtoMessage::Hello, 1);
            hello.version = kProtoVersion;
            hello.max_read_bytes = 4096;
            c.Send(hello);
            rig.Pump(c, 10);
            c.Send(hello);  // a second Hello
        }));
    CHECK(closed_by(
        [&](RsClient& c)
        {
            ProtoData hello = Request(ProtoMessage::Hello, 1);
            hello.version = kProtoVersion;
            hello.max_read_bytes = 4096;
            c.Send(hello);
            rig.Pump(c, 10);
            c.Send(Request(ProtoMessage::StatOk, 2));  // a response is not a request
        }));
    CHECK(closed_by(
        [&](RsClient& c)
        {
            ProtoData hello = Request(ProtoMessage::Hello, 1);
            hello.version = kProtoVersion;
            c.Send(hello);
            rig.Pump(c, 10);
            const byte truncated[3] = {static_cast<byte>(ProtoMessage::Stat), 1, 0};  // a Stat cut short
            c.SendRaw(ByteSpan(truncated, 3));
        }));
    CHECK(rig.server->Stats().closed_protocol == 5);
    // The good client still works.
    ProtoData stat = Request(ProtoMessage::Stat, 50);
    stat.id = 1;
    good->Send(stat);
    rig.Pump(*good);
    CHECK(good->ResponseTo(50)->type == ProtoMessage::StatOk);
    CHECK(rig.server->Stats().connections == 1);
}

TEST_CASE("resource server: 200 clients at once each read their own range", "[asset][server][stress]")
{
    RsRig rig;
    rig.Add(1, 20000);
    const Vector<byte> blob = test::MakeTestBlob(rig.types, Rid(1), 20000, 1);
    Vector<UniquePtr<RsClient>> clients;
    Vector<RsClient*> raw;
    for (u32 i = 0; i < 200; ++i)
    {
        clients.Push(rig.Client());
        raw.Push(clients[i].Get());
    }
    rig.Pump(raw, 200);  // the handshakes
    for (u32 i = 0; i < 200; ++i)
    {
        ProtoData hello = Request(ProtoMessage::Hello, 1);
        hello.version = kProtoVersion;
        hello.max_read_bytes = 1u << 20;
        clients[i]->Send(hello);
        ProtoData read = Request(ProtoMessage::Read, 2);
        read.id = 1;
        read.offset = i * 50u;
        read.length = 100;
        clients[i]->Send(read);
    }
    rig.Pump(raw, 60);
    for (u32 i = 0; i < 200; ++i)
    {
        REQUIRE(clients[i]->ResponseTo(2) != nullptr);
        REQUIRE(clients[i]->ResponseTo(2)->type == ProtoMessage::ReadOk);
        REQUIRE(clients[i]->ResponseTo(2)->data.Size() == 100);
        CHECK(MemCompare(clients[i]->ResponseTo(2)->data.Data(), blob.Data() + i * 50u, 100) == 0);
    }
    CHECK(rig.server->Stats().connections == 200);
}

TEST_CASE("resource server: random and mutated requests never hurt a live server", "[asset][server][fuzz]")
{
    RsRig rig;
    rig.Add(1, 3000, "/f/one.bk");
    rig.Add(2, 3000, "/f/two.bk");
    UniquePtr<RsClient> good = rig.Greeted();
    test::Rng rng(0x5eed);
    // Valid messages of every kind, to mutate.
    Vector<Vector<byte>> seeds;
    auto seed = [&](const ProtoData& m)
    {
        byte buffer[256];
        usize size = 0;
        REQUIRE(EncodeProto(m, MutableByteSpan(buffer, sizeof(buffer)), &size) == ProtoError::None);
        Vector<byte> v;
        for (usize i = 0; i < size; ++i)
        {
            v.Push(buffer[i]);
        }
        seeds.Push(static_cast<Vector<byte>&&>(v));
    };
    ProtoData hello = Request(ProtoMessage::Hello, 1);
    hello.version = kProtoVersion;
    hello.max_read_bytes = 4096;
    seed(hello);
    ProtoData stat = Request(ProtoMessage::Stat, 2);
    stat.id = 1;
    seed(stat);
    ProtoData table = Request(ProtoMessage::Table, 3);
    seed(table);
    ProtoData list = Request(ProtoMessage::List, 4);
    list.prefix = String<HeapAllocator>(HeapAllocator::Default(), "/f");
    seed(list);
    ProtoData read = Request(ProtoMessage::Read, 5);
    read.id = 2;
    read.length = 100;
    seed(read);

    u32 clients_made = 0;
    for (u32 round = 0; round < 300; ++round)
    {
        UniquePtr<RsClient> fuzz = rig.Client();
        ++clients_made;
        rig.Open(*fuzz);
        const u32 messages = 1 + rng.Below(4);
        for (u32 m = 0; m < messages; ++m)
        {
            Vector<byte> bytes;
            if (rng.Below(3) == 0)
            {
                const u32 length = 1 + rng.Below(64);
                for (u32 i = 0; i < length; ++i)
                {
                    bytes.Push(static_cast<byte>(rng.Below(256)));
                }
            }
            else
            {
                const Vector<byte>& source = seeds[rng.Below(static_cast<u32>(seeds.Size()))];
                for (usize i = 0; i < source.Size(); ++i)
                {
                    bytes.Push(source[i]);
                }
                const u32 flips = 1 + rng.Below(3);
                for (u32 f = 0; f < flips; ++f)
                {
                    bytes[rng.Below(static_cast<u32>(bytes.Size()))] ^= static_cast<byte>(1u << rng.Below(8));
                }
                if (rng.Below(4) == 0 && bytes.Size() > 1)
                {
                    bytes.Resize(1 + rng.Below(static_cast<u32>(bytes.Size())));  // cut short
                }
            }
            fuzz->SendRaw(ByteSpan(bytes.Data(), bytes.Size()));
        }
        rig.Pump(*fuzz, 5);
        const ResourceServerStats stats = rig.server->Stats();
        CHECK(stats.connections <= clients_made + 1);  // never more connections than clients that exist
    }
    // The good client was not hurt.
    ProtoData ok = Request(ProtoMessage::Read, 60);
    ok.id = 2;
    ok.offset = 10;
    ok.length = 20;
    good->Send(ok);
    rig.Pump(*good, 30);
    REQUIRE(good->ResponseTo(60) != nullptr);
    CHECK(good->ResponseTo(60)->type == ProtoMessage::ReadOk);
    CHECK(good->ResponseTo(60)->data.Size() == 20);
    CHECK(rig.server->Stats().bytes_served < 300u * 4u * 3000u);  // the bytes served are bounded by the requests
}

TEST_CASE("resource server: the generation is a hash of the table, and new bytes under the same names keep it",
          "[asset][server]")
{
    auto generation_of = [](RsRig& rig, UniquePtr<RsClient>& client)
    {
        ProtoData t = Request(ProtoMessage::Table, 500);
        client->Send(t);
        rig.Pump(*client);
        const ProtoData* r = client->ResponseTo(500);
        REQUIRE(r != nullptr);
        REQUIRE(r->type == ProtoMessage::TableOk);
        return r->generation;
    };
    RsRig first;
    first.Add(1, 100, "/g/one.bk");
    first.Add(2, 100, "/g/two.bk");
    UniquePtr<RsClient> a = first.Greeted();
    const u64 generation = generation_of(first, a);
    CHECK(generation != 0);
    // A restart with the same content: the same generation (a client's cached table is still good).
    RsRig second;
    second.Add(1, 100, "/g/one.bk");
    second.Add(2, 100, "/g/two.bk");
    UniquePtr<RsClient> b = second.Greeted();
    CHECK(generation_of(second, b) == generation);
    // Another table: another generation.
    RsRig third;
    third.Add(1, 100, "/g/one.bk");
    UniquePtr<RsClient> c = third.Greeted();
    CHECK(generation_of(third, c) != generation);
    // The bytes of a resource change and its names do not: the table is the same.
    const Vector<byte> other = test::MakeTestBlob(first.types, Rid(1), 300, 9);
    REQUIRE(first.backend.Replace(Rid(1), AsBytes(other)).Ok());
    a->responses.Clear();
    CHECK(generation_of(first, a) == generation);
    // A list reply carries the generation, so a change between two pages shows.
    ProtoData l = Request(ProtoMessage::List, 501);
    l.prefix = String<HeapAllocator>(HeapAllocator::Default(), "/g");
    a->Send(l);
    first.Pump(*a);
    CHECK(a->ResponseTo(501)->generation == generation);
}

TEST_CASE("resource server: a Hello of another version is answered Unsupported and the connection ends",
          "[asset][server]")
{
    RsRig rig;
    UniquePtr<RsClient> client = rig.Client();
    rig.Open(*client);
    ProtoData hello = Request(ProtoMessage::Hello, 1);
    hello.version = 2;
    hello.max_read_bytes = 4096;
    client->Send(hello);
    rig.Pump(*client, 60);
    REQUIRE(client->ResponseTo(1) != nullptr);
    CHECK(client->ResponseTo(1)->error == ProtoError::Unsupported);
    CHECK(client->closed);
}

TEST_CASE("resource server: a page is sized to fit a message of a small transport", "[asset][server]")
{
    RsRig rig(ResourceServerConfig(), 4096);  // a transport whose messages hold 4 KiB
    for (u64 id = 1; id <= 30; ++id)
    {
        char path[96];
        FormatTo(path, sizeof(path), "/long/directory/name/%s%02u.bk",
                 "0123456789012345678901234567890123456789012345678901234567890123456789", static_cast<unsigned>(id));
        rig.Add(id, 100, path);
    }
    UniquePtr<RsClient> client = rig.Greeted();
    const u32 per_page = client->ResponseTo(1)->page_entries;
    CHECK(per_page >= 1);
    CHECK(per_page < 256);  // the default would not fit
    u32 seen = 0;
    u32 pages = 1;
    for (u32 page = 0; page < pages; ++page)
    {
        ProtoData t = Request(ProtoMessage::Table, 10 + page);
        t.page = page;
        client->Send(t);
        rig.Pump(*client);
        const ProtoData* r = client->ResponseTo(10 + page);
        REQUIRE(r != nullptr);
        REQUIRE(r->type == ProtoMessage::TableOk);  // not TooLarge
        pages = r->page_count;
        seen += static_cast<u32>(r->entries.Size());
    }
    CHECK(seen == 30);
}

TEST_CASE("resource server: the settings give the limits", "[asset][server]")
{
    settings::Registry registry;
    RegisterServerSettings(&registry);
    const ResourceServerConfig config = ResourceServerConfigFromSettings(registry);
    CHECK(config.max_read_bytes == 256 * 1024);
    CHECK(config.max_inflight == 16);
    CHECK(config.page_entries == 256);
}

TEST_CASE("resource protocol: every message round trips and bytes after the end are refused", "[asset][server]")
{
    byte buffer[512];
    auto round = [&](const ProtoData& message)
    {
        usize size = 0;
        REQUIRE(EncodeProto(message, MutableByteSpan(buffer, sizeof(buffer)), &size) == ProtoError::None);
        ProtoData out;
        CHECK(DecodeProto(ByteSpan(buffer, size), 4096, &out) == ProtoError::None);
        CHECK(out.type == message.type);
        CHECK(out.request == message.request);
        CHECK(DecodeProto(ByteSpan(buffer, size + 1), 4096, &out) == ProtoError::Malformed);  // an extra byte
        if (size > 1)
        {
            CHECK(DecodeProto(ByteSpan(buffer, size - 1), 4096, &out) == ProtoError::Malformed);  // one byte short
        }
    };
    ProtoData d = Request(ProtoMessage::Hello, 7);
    d.version = 1;
    d.max_read_bytes = 100;
    round(d);
    d = Request(ProtoMessage::Read, 8);
    d.id = 5;
    d.offset = 6;
    d.length = 7;
    round(d);
    d = Request(ProtoMessage::Error, 9);
    d.error = ProtoError::Busy;
    round(d);
    d = Request(ProtoMessage::ListOk, 10);
    d.ids.Push(1);
    d.ids.Push(2);
    round(d);
    byte raw[8] = {0};
    raw[0] = 99;  // a type nobody has
    ProtoData out;
    CHECK(DecodeProto(ByteSpan(raw, 8), 4096, &out) == ProtoError::Malformed);
}

}  // namespace engine_placeholder::asset
