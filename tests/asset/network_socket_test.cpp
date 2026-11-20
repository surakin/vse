// Phase 22 Day 332: the network backend against a resource server over real loopback sockets (labelled `network`).

#include <catch2/catch_test_macros.hpp>

#include "asset/memory_backend.h"
#include "asset/network_backend.h"
#include "asset/resource_server.h"
#include "backend_conformance.h"
#include "net/clock.h"
#include "net/socket_stream.h"
#include "net/tcp_transport.h"
#include "sync/atomic.h"
#include "sync/thread.h"
#include "support.h"

namespace vse::asset
{
namespace
{

using test::Rid;

// A server on its own thread, serving a memory backend through a listening loopback socket.
struct LoopbackServer
{
    LoopbackServer() : backend(types, u64{64} << 20), transport(provider, clock)
    {
        REQUIRE(types.Register(test::kBackendTestType, 1, "backend test").Ok());
    }
    bool Start()
    {
        net::Endpoint any;
        REQUIRE(net::Endpoint::Parse(StringView("127.0.0.1:0"), &any).Ok());
        if (transport.Listen(any).Failed())
        {
            return false;
        }
        server = MakeUnique<ResourceServer>(backend, transport);
        thread = Thread(&LoopbackServer::Run, this);
        return true;
    }
    void Stop()
    {
        stop.Store(true);
        if (thread.Joinable())
        {
            thread.Join();
        }
    }
    ~LoopbackServer() { Stop(); }
    void Run()
    {
        while (!stop.Load())
        {
            server->Update();
            ThisThread::SleepForMicroseconds(200);
        }
    }

    TypeRegistry types;
    HostMemoryBackend backend;
    net::SocketStreamProvider provider;
    net::MonotonicClock clock;
    net::TcpTransport transport;
    UniquePtr<ResourceServer> server;
    Atomic<bool> stop{false};
    Thread thread;
};

}  // namespace

TEST_CASE("network backend: passes the conformance suite over loopback sockets", "[asset][backend][network]")
{
    LoopbackServer server;
    test::ConformanceFixture fx;
    const char* paths[] = {"/a/one.bk", "/a/two.bk", "/three.bk"};
    for (u32 i = 0; i < 3; ++i)
    {
        fx.ids.Push(Rid(0x10 + i));
        fx.blobs.Push(test::MakeTestBlob(server.types, Rid(0x10 + i), 100 + i * 400, i));
        fx.paths.Push(test::Vp(paths[i]));
        const StringView path = test::Vp(paths[i]);
        MemoryAddOptions options;
        options.paths = Span<const StringView>(&path, 1);
        REQUIRE(server.backend.Add(fx.ids[i], test::AsBytes(fx.blobs[i]), options).Ok());
    }
    if (!server.Start())
    {
        SKIP("cannot bind a loopback TCP socket here");
    }
    net::SocketStreamProvider provider;
    net::MonotonicClock clock;
    net::TcpTransport transport(provider, clock);
    NetworkBackendOptions options;
    options.transport = &transport;
    options.server = server.provider.LocalEndpoint();
    options.clock = &clock;
    NetworkBackend client(options);
    test::RunBackendConformance(client, fx);
    CHECK(client.State() == NetworkState::Online);
}

TEST_CASE("network backend: a server that stops makes it Offline over loopback sockets", "[asset][backend][network]")
{
    LoopbackServer server;
    const char* path = "/x/a.bk";
    const Vector<byte> blob = test::MakeTestBlob(server.types, Rid(0x80), 300, 1);
    const StringView view = test::Vp(path);
    MemoryAddOptions add;
    add.paths = Span<const StringView>(&view, 1);
    REQUIRE(server.backend.Add(Rid(0x80), test::AsBytes(blob), add).Ok());
    if (!server.Start())
    {
        SKIP("cannot bind a loopback TCP socket here");
    }
    net::SocketStreamProvider provider;
    net::MonotonicClock clock;
    net::TcpTransport transport(provider, clock);
    NetworkBackendOptions options;
    options.transport = &transport;
    options.server = server.provider.LocalEndpoint();
    options.clock = &clock;
    options.config.max_retries = 1;
    options.config.timeout_nanos = 300'000'000;
    options.config.backoff_nanos = 10'000'000;
    NetworkBackend client(options);
    ResourceInfo info;
    REQUIRE(client.Stat(Rid(0x80), &info).Ok());
    server.Stop();
    // The listener is still bound but nobody answers: the requests time out.
    CHECK(client.Stat(Rid(0x80), &info).Is(ErrorCode::Offline));
    CHECK(client.State() == NetworkState::Offline);
}

}  // namespace vse::asset
