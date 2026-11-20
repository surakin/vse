// Phase 22 Day 332: the network backend (over simulated streams, one thread), the blob cache and the Changes request.

#include <catch2/catch_test_macros.hpp>

#include "asset/asset_settings.h"
#include "asset/blob_cache.h"
#include "asset/memory_backend.h"
#include "asset/network_backend.h"
#include "asset/resource_server.h"
#include "backend_conformance.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "io/directory.h"
#include "io/file.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "net/clock.h"
#include "net/sim_stream.h"
#include "net/tcp_transport.h"
#include "settings/registry.h"
#include "support.h"

namespace vse::asset
{
namespace
{

using test::AsBytes;
using test::Rid;

// A server and a client backend over simulated streams, driven by one thread: the backend's idle hook moves the clock
// and updates the server.
struct NetRig
{
    explicit NetRig(BlobCache* cache = nullptr, u32 max_retries = 2) : streams(clock), backend(types, u64{64} << 20)
    {
        REQUIRE(types.Register(test::kBackendTestType, 1, "backend test").Ok());
        server_endpoint = streams.AllocateEndpoint();
        server_provider = streams.MakeProvider(server_endpoint);
        server_transport = MakeUnique<net::TcpTransport>(*server_provider, clock, Config());
        REQUIRE(server_transport->Listen(server_endpoint).Ok());
        server = MakeUnique<ResourceServer>(backend, *server_transport, ResourceServerConfig());
        client_provider = streams.MakeProvider(streams.AllocateEndpoint());
        client_transport = MakeUnique<net::TcpTransport>(*client_provider, clock, Config());
        NetworkBackendOptions options;
        options.transport = client_transport.Get();
        options.server = server_endpoint;
        options.clock = &clock;
        options.cache = cache;
        options.background_thread = false;
        options.idle = &NetRig::Idle;
        options.idle_user = this;
        options.config.max_retries = max_retries;
        options.config.timeout_nanos = 500'000'000;
        options.config.backoff_nanos = 20'000'000;
        options.config.backoff_max_nanos = 100'000'000;
        options.config.reconnect_nanos = 200'000'000;
        client = MakeUnique<NetworkBackend>(options);
    }
    static net::TcpTransportConfig Config()
    {
        net::TcpTransportConfig c;
        c.accept_limits.total_per_second = 100000;
        c.accept_limits.total_burst = 100000;
        c.accept_limits.address_per_second = 100000;
        c.accept_limits.address_burst = 100000;
        return c;
    }
    static void Idle(void* user)
    {
        NetRig* rig = static_cast<NetRig*>(user);
        rig->clock.Advance(1'000'000);
        if (rig->server_up)
        {
            rig->server->Update();
        }
    }
    Vector<byte> Add(u64 id, usize bytes, const char* path = nullptr, u32 seed = 0)
    {
        Vector<byte> blob = test::MakeTestBlob(types, Rid(id), bytes, seed != 0 ? seed : static_cast<u32>(id));
        MemoryAddOptions options;
        StringView text;
        if (path != nullptr)
        {
            text = test::Vp(path);
            options.paths = Span<const StringView>(&text, 1);
        }
        REQUIRE(backend.Add(Rid(id), AsBytes(blob), options).Ok());
        return blob;
    }
    // Runs the clock until `done` or `millis` have passed.
    template <typename Done> bool RunUntil(Done done, u32 millis = 5000)
    {
        for (u32 i = 0; i < millis && !done(); ++i)
        {
            Idle(this);
            client->Poll();
        }
        return done();
    }

    net::ManualClock clock{1'000'000};
    TypeRegistry types;
    net::SimStreamNetwork streams;
    HostMemoryBackend backend;
    net::Endpoint server_endpoint;
    UniquePtr<net::StreamProvider> server_provider;
    UniquePtr<net::TcpTransport> server_transport;
    UniquePtr<ResourceServer> server;
    UniquePtr<net::StreamProvider> client_provider;
    UniquePtr<net::TcpTransport> client_transport;
    UniquePtr<NetworkBackend> client;
    bool server_up = true;
};

}  // namespace

TEST_CASE("network backend: Stat, Open, the table and List over simulated streams", "[asset][backend][network]")
{
    NetRig rig;
    const char* paths[] = {"/a/one.bk", "/a/two.bk", "/three.bk"};
    Vector<Vector<byte>> blobs;
    for (u32 i = 0; i < 3; ++i)
    {
        blobs.Push(rig.Add(0x10 + i, 100 + i * 400, paths[i]));
    }
    CHECK((rig.client->Capabilities() & kBackendRanged) != 0);
    for (u32 i = 0; i < 3; ++i)
    {
        ResourceInfo info;
        REQUIRE(rig.client->Stat(Rid(0x10 + i), &info).Ok());
        CHECK(info.size == blobs[i].Size());
        UniquePtr<ResourceReader> reader;
        REQUIRE(rig.client->Open(Rid(0x10 + i), &reader).Ok());
        Vector<byte> out;
        out.Resize(blobs[i].Size());
        REQUIRE(reader->ReadAt(0, out.Data(), out.Size()).Ok());
        CHECK(MemCompare(out.Data(), blobs[i].Data(), out.Size()) == 0);
        CHECK(reader->ReadAt(1, out.Data(), out.Size()).Failed());  // past the end
    }
    ResourceInfo info;
    UniquePtr<ResourceReader> missing;
    CHECK(rig.client->Stat(Rid(0xdeadbeef), &info).Is(ErrorCode::NotFound));
    CHECK(rig.client->Open(Rid(0xdeadbeef), &missing).Is(ErrorCode::NotFound));
    ResourceTable table;
    REQUIRE(rig.client->ReadTable(&table).Ok());
    for (u32 i = 0; i < 3; ++i)
    {
        ResourceId resolved;
        REQUIRE(table.Resolve(test::Vp(paths[i]), &resolved));
        CHECK(resolved == Rid(0x10 + i));
    }
    Vector<ResourceId> listed;
    REQUIRE(rig.client->List(StringView("/a/"), 100, &listed).Ok());
    CHECK(listed.Size() == 2);
}

TEST_CASE("network backend: a large resource is read by range", "[asset][backend][network]")
{
    NetRig rig;
    const Vector<byte> blob = rig.Add(0x20, 3000);
    UniquePtr<ResourceReader> reader;
    REQUIRE(rig.client->Open(Rid(0x20), &reader).Ok());
    CHECK(reader->Size() == blob.Size());
    Vector<byte> out;
    out.Resize(blob.Size());
    REQUIRE(reader->ReadAt(0, out.Data(), out.Size()).Ok());
    CHECK(MemCompare(out.Data(), blob.Data(), blob.Size()) == 0);
    CHECK(rig.client->Stats().requests > 0);
}

TEST_CASE("network backend: a cached blob is not fetched twice, and a changed one is",
          "[asset][backend][network][cache]")
{
    test::TempDir dir;
    UniquePtr<BlobCache> cache;
    REQUIRE(BlobCache::Open(dir.Path(), u64{1} << 20, &cache).Ok());
    NetRig rig(cache.Get());
    const Vector<byte> blob = rig.Add(0x30, 600);
    UniquePtr<ResourceReader> reader;
    REQUIRE(rig.client->Open(Rid(0x30), &reader).Ok());
    CHECK(cache->Stats().entries == 1);
    const u64 fetched = rig.client->Stats().fetched_bytes;
    CHECK(fetched >= blob.Size());
    REQUIRE(rig.client->Open(Rid(0x30), &reader).Ok());
    CHECK(rig.client->Stats().cache_serves == 1);
    CHECK(rig.client->Stats().fetched_bytes < fetched + blob.Size());  // only the header was read again
    // New bytes under the same id: the header's hash differs, so the blob is fetched.
    REQUIRE(rig.backend.Remove(Rid(0x30)).Ok());
    const Vector<byte> changed = rig.Add(0x30, 600, nullptr, 99);
    REQUIRE(rig.client->Open(Rid(0x30), &reader).Ok());
    Vector<byte> out;
    out.Resize(changed.Size());
    REQUIRE(reader->ReadAt(0, out.Data(), out.Size()).Ok());
    CHECK(MemCompare(out.Data(), changed.Data(), changed.Size()) == 0);
    CHECK(cache->Stats().entries == 2);
}

TEST_CASE("blob cache: least recently used blobs are evicted and a damaged file is dropped on open", "[asset][cache]")
{
    TypeRegistry types;
    REQUIRE(types.Register(test::kBackendTestType, 1, "backend test").Ok());
    test::TempDir dir;
    Vector<Vector<byte>> blobs;
    Vector<ContentHash> hashes;
    for (u32 i = 0; i < 3; ++i)
    {
        blobs.Push(test::MakeTestBlob(types, Rid(0x40 + i), 500, i + 1));
        BlobHeaderInfo info;
        REQUIRE(PeekBlobHeader(AsBytes(blobs[i]), &info).Ok());
        hashes.Push(info.content_hash);
    }
    {
        UniquePtr<BlobCache> cache;
        REQUIRE(BlobCache::Open(dir.Path(), blobs[0].Size() * 2 + 10, &cache).Ok());
        REQUIRE(cache->Put(Rid(0x40), hashes[0], AsBytes(blobs[0])).Ok());
        REQUIRE(cache->Put(Rid(0x41), hashes[1], AsBytes(blobs[1])).Ok());
        Vector<byte> read;
        REQUIRE(cache->Read(hashes[0], &read).Ok());  // 0 is now more recent than 1
        REQUIRE(cache->Put(Rid(0x42), hashes[2], AsBytes(blobs[2])).Ok());
        CHECK(cache->Has(hashes[0]));
        CHECK_FALSE(cache->Has(hashes[1]));
        CHECK(cache->Has(hashes[2]));
        CHECK(cache->Stats().evictions == 1);
        // A blob whose bytes do not match its hash is refused.
        CHECK(cache->Put(Rid(0x43), hashes[0], AsBytes(blobs[1])).Failed());
        // The name of an evicted blob is still known.
        ContentHash h;
        u64 size = 0;
        CHECK(cache->Find(Rid(0x41), &h, &size));
        CHECK(h == hashes[1]);
    }
    {
        UniquePtr<BlobCache> cache;
        REQUIRE(BlobCache::Open(dir.Path(), u64{1} << 20, &cache).Ok());
        CHECK(cache->Has(hashes[0]));
        CHECK(cache->Has(hashes[2]));
    }
}

TEST_CASE("network backend: the server disappearing makes it Offline, the cache serves, and it comes back",
          "[asset][backend][network][offline]")
{
    test::TempDir dir;
    UniquePtr<BlobCache> cache;
    REQUIRE(BlobCache::Open(dir.Path(), u64{1} << 20, &cache).Ok());
    NetRig rig(cache.Get());
    const Vector<byte> blob = rig.Add(0x50, 400, "/x/cached.bk");
    rig.Add(0x51, 400, "/x/other.bk");
    UniquePtr<ResourceReader> reader;
    REQUIRE(rig.client->Open(Rid(0x50), &reader).Ok());
    ResourceTable table;
    REQUIRE(rig.client->ReadTable(&table).Ok());
    CHECK(rig.client->State() == NetworkState::Online);

    rig.server_up = false;
    rig.server_transport.Reset();
    rig.server.Reset();
    ResourceInfo info;
    CHECK(rig.client->Stat(Rid(0x51), &info).Is(ErrorCode::Offline));
    CHECK(rig.client->State() == NetworkState::Offline);
    // Served from the cache: the blob it fetched, and the last table.
    REQUIRE(rig.client->Stat(Rid(0x50), &info).Ok());
    CHECK(info.size == blob.Size());
    REQUIRE(rig.client->Open(Rid(0x50), &reader).Ok());
    Vector<byte> out;
    out.Resize(blob.Size());
    REQUIRE(reader->ReadAt(0, out.Data(), out.Size()).Ok());
    CHECK(MemCompare(out.Data(), blob.Data(), blob.Size()) == 0);
    CHECK(rig.client->Open(Rid(0x51), &reader).Is(ErrorCode::Offline));
    ResourceTable stale;
    REQUIRE(rig.client->ReadTable(&stale).Ok());
    CHECK(stale.Size() == table.Size());
    CHECK(rig.client->Stats().offline_serves > 0);
}

TEST_CASE("network backend: Watch reports what the server's backend changed", "[asset][backend][network]")
{
    NetRig rig;
    rig.Add(0x60, 200, "/w/a.bk");
    Vector<ChangeRecord> changes;
    REQUIRE(rig.client->Watch(&changes).Ok());  // learns the epoch
    CHECK(changes.Size() == 0);
    rig.Add(0x61, 200, "/w/b.bk");
    REQUIRE(rig.client->Watch(&changes).Ok());
    REQUIRE(changes.Size() == 1);
    CHECK(changes[0].id == Rid(0x61));
    CHECK(changes[0].kind == ChangeKind::Added);
    REQUIRE(rig.backend.Remove(Rid(0x60)).Ok());
    REQUIRE(rig.client->Watch(&changes).Ok());
    REQUIRE(changes.Size() == 1);
    CHECK(changes[0].id == Rid(0x60));
    CHECK(changes[0].kind == ChangeKind::Removed);
}

TEST_CASE("network backend: the asynchronous table read ends with the whole table", "[asset][backend][network]")
{
    NetRig rig;
    for (u32 i = 0; i < 5; ++i)
    {
        char path[32];
        FormatTo(path, sizeof(path), "/t/%u.bk", i);
        rig.Add(0x70 + i, 100, path);
    }
    UniquePtr<TableTicket> ticket = rig.client->ReadTableAsync();
    REQUIRE(rig.RunUntil([&] { return ticket->IsDone(); }));
    ResourceTable all;
    REQUIRE(ticket->Finish(&all).Ok());
    CHECK(all.Size() == 5);
}

TEST_CASE("network backend: it reconnects in the background when the server returns",
          "[asset][backend][network][offline]")
{
    NetRig rig;
    rig.Add(0x90, 300, "/r/a.bk");
    ResourceInfo info;
    REQUIRE(rig.client->Stat(Rid(0x90), &info).Ok());
    rig.server_up = false;
    CHECK(rig.client->Stat(Rid(0x90), &info).Is(ErrorCode::Offline));
    CHECK(rig.client->State() == NetworkState::Offline);
    CHECK(rig.client->Stats().retries > 0);
    CHECK(rig.client->Stats().timeouts > 0);
    rig.server_up = true;
    REQUIRE(rig.RunUntil([&] { return rig.client->State() == NetworkState::Online; }, 20000));
    CHECK(rig.client->Stats().reconnects >= 1);
    REQUIRE(rig.client->Stat(Rid(0x90), &info).Ok());
}

TEST_CASE("blob cache: a file damaged on disk is dropped when the cache is opened", "[asset][cache]")
{
    TypeRegistry types;
    REQUIRE(types.Register(test::kBackendTestType, 1, "backend test").Ok());
    test::TempDir dir;
    const Vector<byte> blob = test::MakeTestBlob(types, Rid(0xa0), 500, 3);
    BlobHeaderInfo info;
    REQUIRE(PeekBlobHeader(AsBytes(blob), &info).Ok());
    {
        UniquePtr<BlobCache> cache;
        REQUIRE(BlobCache::Open(dir.Path(), u64{1} << 20, &cache).Ok());
        REQUIRE(cache->Put(Rid(0xa0), info.content_hash, AsBytes(blob)).Ok());
    }
    // Flip a byte of the stored file.
    Vector<io::DirectoryItem> entries;
    REQUIRE(io::ListDirectory(dir.Path(), true, &entries).Ok());
    bool damaged = false;
    for (usize i = 0; i < entries.Size(); ++i)
    {
        const io::Path file = dir.Path() / entries[i].name;
        Vector<u8> bytes;
        if (entries[i].type == io::FileType::Regular && io::ReadFile(file, &bytes).Ok() && bytes.Size() > 100)
        {
            bytes[bytes.Size() - 1] ^= 0x55;
            REQUIRE(io::WriteFile(file, bytes.Data(), bytes.Size()).Ok());
            damaged = true;
        }
    }
    REQUIRE(damaged);
    UniquePtr<BlobCache> cache;
    REQUIRE(BlobCache::Open(dir.Path(), u64{1} << 20, &cache).Ok());
    CHECK_FALSE(cache->Has(info.content_hash));
    CHECK(cache->Stats().dropped_corrupt == 1);
}

TEST_CASE("blob cache: a second index line for one hash is ignored", "[asset][cache]")
{
    TypeRegistry types;
    REQUIRE(types.Register(test::kBackendTestType, 1, "backend test").Ok());
    test::TempDir dir;
    const Vector<byte> blob = test::MakeTestBlob(types, Rid(0xb0), 400, 5);
    BlobHeaderInfo info;
    REQUIRE(PeekBlobHeader(AsBytes(blob), &info).Ok());
    {
        UniquePtr<BlobCache> cache;
        REQUIRE(BlobCache::Open(dir.Path(), u64{1} << 20, &cache).Ok());
        REQUIRE(cache->Put(Rid(0xb0), info.content_hash, AsBytes(blob)).Ok());
    }
    String<HeapAllocator> text(HeapAllocator::Default());
    REQUIRE(io::ReadTextFile(dir.Path() / StringView("index"), &text).Ok());
    String<HeapAllocator> doubled(HeapAllocator::Default());
    doubled.Append(text.View());
    doubled.Append(text.View());  // every line twice
    REQUIRE(io::WriteFile(dir.Path() / StringView("index"), doubled.CStr(), doubled.Length()).Ok());
    UniquePtr<BlobCache> cache;
    REQUIRE(BlobCache::Open(dir.Path(), u64{1} << 20, &cache).Ok());
    CHECK(cache->Stats().entries == 1);
    CHECK(cache->Stats().bytes == blob.Size());
}

TEST_CASE("network backend: it makes exactly max_retries retries before it is Offline", "[asset][backend][network]")
{
    NetRig rig(nullptr, 3);
    rig.Add(0xc0, 200, "/q/a.bk");
    ResourceInfo info;
    REQUIRE(rig.client->Stat(Rid(0xc0), &info).Ok());
    rig.server_up = false;
    CHECK(rig.client->Stat(Rid(0xc0), &info).Is(ErrorCode::Offline));
    CHECK(rig.client->Stats().retries == 3);
    CHECK(rig.client->State() == NetworkState::Offline);
}

namespace
{

struct TicketProbe
{
    NetworkBackend* backend = nullptr;
    bool called = false;
    NetworkState seen = NetworkState::Connecting;
};

void OnTicketDone(void* user)
{
    TicketProbe* probe = static_cast<TicketProbe*>(user);
    probe->seen = probe->backend->State();  // takes the backend's mutex: the ticket is completed outside it
    probe->called = true;
}

}  // namespace

TEST_CASE("network backend: a table ticket is completed outside the backend's mutex", "[asset][backend][network]")
{
    NetRig rig;
    rig.Add(0xd0, 100, "/d/a.bk");
    UniquePtr<TableTicket> ticket = rig.client->ReadTableAsync();
    TicketProbe probe;
    probe.backend = rig.client.Get();
    if (ticket->SetOnComplete(&OnTicketDone, &probe))
    {
        REQUIRE(rig.RunUntil([&] { return probe.called; }));
        CHECK(probe.seen == NetworkState::Online);
    }
    REQUIRE(rig.RunUntil([&] { return ticket->IsDone(); }));
    // A read started again while one is in flight cancels the first, from the same thread that holds a lock of its own.
    UniquePtr<TableTicket> first = rig.client->ReadTableAsync();
    UniquePtr<TableTicket> second = rig.client->ReadTableAsync();
    TicketProbe cancelled;
    cancelled.backend = rig.client.Get();
    if (first->SetOnComplete(&OnTicketDone, &cancelled))
    {
        REQUIRE(rig.RunUntil([&] { return cancelled.called; }));
    }
    REQUIRE(rig.RunUntil([&] { return second->IsDone(); }));
}

TEST_CASE("network settings: declared with their defaults", "[asset][network]")
{
    settings::Registry registry;
    RegisterNetworkSettings(&registry);
    const NetworkBackendConfig config = NetworkBackendConfigFromSettings(registry);
    CHECK(config.max_retries == 3);
    UniquePtr<BlobCache> cache;
    REQUIRE(NetworkCacheFromSettings(registry, &cache).Ok());
    CHECK_FALSE(static_cast<bool>(cache));
}

}  // namespace vse::asset
