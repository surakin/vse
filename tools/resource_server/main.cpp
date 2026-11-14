// resource_server: Phase 22 Day 331. Serves a directory of loose blobs or a pak file to resource clients over TCP, with
// the protocol of asset/resource_protocol.h. It is read-only and has no encryption: use it on a network you trust.
//
//   resource_server --root DIR  --listen ADDR:PORT [--seconds N]
//   resource_server --pak FILE  --listen ADDR:PORT [--seconds N]
//
// --seconds N stops the server after N seconds (the default is to run until it is killed). Its options are its own
// command line, not engine settings (as net_probe's are); the limits are the defaults of asset::ResourceServerConfig.
#include "asset/filesystem_backend.h"
#include "asset/pak_backend.h"
#include "asset/resource_server.h"
#include "cstr/cstr.h"
#include "cstr/number.h"
#include "io/path.h"
#include "net/clock.h"
#include "net/endpoint.h"
#include "net/socket_stream.h"
#include "net/tcp_transport.h"
#include "os/console.h"
#include "sync/thread.h"

using namespace engine_placeholder;

namespace
{

int Usage()
{
    os::PrintError("usage: resource_server (--root DIR | --pak FILE) --listen ADDR:PORT [--seconds N]\n");
    return 64;
}

// The value of --name=value or of --name value, or null.
const char* Option(int argc, char** argv, const char* name)
{
    const usize n = CStrLength(name);
    for (int i = 1; i < argc; ++i)
    {
        if (CStrCompareN(argv[i], name, n) == 0)
        {
            if (argv[i][n] == '=')
            {
                return argv[i] + n + 1;
            }
            if (argv[i][n] == '\0' && i + 1 < argc)
            {
                return argv[i + 1];
            }
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv)
{
    const char* root = Option(argc, argv, "--root");
    const char* pak = Option(argc, argv, "--pak");
    const char* listen = Option(argc, argv, "--listen");
    const char* seconds = Option(argc, argv, "--seconds");
    if ((root == nullptr) == (pak == nullptr) || listen == nullptr)
    {
        return Usage();
    }
    long seconds_to_run = 0;  // 0: until the process is killed
    if (seconds != nullptr)
    {
        seconds_to_run = CStrToLong(seconds);
        if (seconds_to_run <= 0)
        {
            return Usage();  // 0 is not "no limit": leave --seconds out for that
        }
    }
    net::Endpoint at;
    if (net::Endpoint::Parse(StringView(listen, CStrLength(listen)), &at).Failed())
    {
        os::PrintError("resource_server: not an address:port: %s\n", listen);
        return 64;
    }
    const char* source = root != nullptr ? root : pak;
    io::Path path;
    if (!io::Path::Parse(StringView(source, CStrLength(source)), io::NativePathStyle(), &path))
    {
        os::PrintError("resource_server: not a path: %s\n", source);
        return 64;
    }

    asset::TypeRegistry types;
    UniquePtr<asset::ResourceBackend> backend;
    if (root != nullptr)
    {
        UniquePtr<asset::FilesystemBackend> directory;
        const asset::Status status = asset::FilesystemBackend::Create(path, types, &directory);
        if (status.Failed())
        {
            os::PrintError("resource_server: %s: %s: %s\n", source, asset::ErrorCodeName(status.Code()),
                           status.Message());
            return 1;
        }
        backend = UniquePtr<asset::ResourceBackend>(directory.Release());
    }
    else
    {
        UniquePtr<asset::PakBackend> file;
        const asset::Status status = asset::PakBackend::Open(path, types, 0, &file);
        if (status.Failed())
        {
            os::PrintError("resource_server: %s: %s: %s\n", source, asset::ErrorCodeName(status.Code()),
                           status.Message());
            return 1;
        }
        backend = UniquePtr<asset::ResourceBackend>(file.Release());
    }

    net::SocketStreamProvider provider;
    net::MonotonicClock clock;
    net::TcpTransport transport(provider, clock);
    if (transport.Listen(at).Failed())
    {
        os::PrintError("resource_server: cannot listen on %s\n", listen);
        return 1;
    }
    asset::ResourceServer server(*backend, transport);
    os::Print("resource_server: serving %s on %s\n", source, provider.LocalEndpoint().Format().chars.Data());
    const u64 until = seconds_to_run == 0 ? 0 : clock.NowNanos() + static_cast<u64>(seconds_to_run) * 1'000'000'000ull;
    while (until == 0 || clock.NowNanos() < until)
    {
        server.Update();
        ThisThread::SleepForMilliseconds(1);
    }
    const asset::ResourceServerStats stats = server.Stats();
    os::Print("resource_server: %llu requests, %llu errors, %llu bytes served\n",
              static_cast<unsigned long long>(stats.requests), static_cast<unsigned long long>(stats.errors),
              static_cast<unsigned long long>(stats.bytes_served));
    return 0;
}
