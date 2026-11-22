#pragma once

//! [[asset::ResourceServer]]: serves any [[asset::ResourceBackend]] (a directory, a pak, host
//! memory) to many clients over a [[net::Transport]], speaking the protocol of `resource_protocol.h`. It is read-only
//! (the protocol has no request that writes), bounded (a read, a page, the requests per connection and per poll have
//! limits the settings give) and polled: [[asset::ResourceServer::Update]] takes the transport's events, answers the
//! requests and returns. The transport's own limits (connections, rate limits, queues) apply as they are. There is no
//! encryption: use it on a network you trust.

#include "asset/backend.h"
#include "asset/resource_protocol.h"
#include "asset/resource_table.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "net/transport.h"
#include "settings/registry.h"
#include "types.h"

namespace vse::asset
{

/// What a [[asset::ResourceServer]] limits.
///
/// - Thread-safety: thread-compatible plain data.
struct ResourceServerConfig
{
    u32 max_read_bytes = 256 *
                         1024;  ///< The most bytes a `Read` returns (clamped to what the transport's messages hold).
    u32 max_inflight = 16;      ///< Requests per connection answered in one `Update`; later ones get `Busy`.
    u32 page_entries = 256;     ///< Entries of a table or list page (at most [[asset::kProtoMaxPageEntries]]).
};

/// Reads a [[asset::ResourceServerConfig]] from the settings.
///
/// - Param registry: the settings; `RegisterServerSettings` must have been called.
/// - Aborts: a setting was not declared.
/// - Thread-safety: thread-safe.
ResourceServerConfig ResourceServerConfigFromSettings(const settings::Registry& registry);

/// What the server counted.
///
/// - Thread-safety: immutable value (a copy).
struct ResourceServerStats
{
    u32 connections = 0;      ///< Connections open now.
    u64 requests = 0;         ///< Requests answered (with data or with an `Error`).
    u64 errors = 0;           ///< Requests answered with an `Error`.
    u64 busy = 0;             ///< Requests answered `Busy` because the connection was over its limit.
    u64 closed_protocol = 0;  ///< Connections closed for a message the server could not take.
    u64 bytes_served = 0;     ///< Bytes of resources returned by `Read`.
};

/// The resource server.
///
/// - Thread-safety: thread-compatible; the thread that polls the transport calls `Update`.
/// - Ownership: borrows the backend and the transport, which must outlive it; the transport is polled only by `Update`.
class ResourceServer
{
public:
    /// Makes a server.
    ///
    /// - Param backend: what is served.
    /// - Param transport: the transport clients connect to; the caller makes it listen.
    /// - Param config: the limits.
    /// - Ownership: borrows `backend` and `transport`.
    ResourceServer(ResourceBackend& backend, net::Transport& transport,
                   const ResourceServerConfig& config = ResourceServerConfig());
    ResourceServer(const ResourceServer&) = delete;
    ResourceServer& operator=(const ResourceServer&) = delete;

    /// Takes the transport's events, answers the requests that arrived and notes the backend's changes.
    ///
    /// - Fails: never; a request that fails is answered with an `Error`, a client that breaks the protocol is closed.
    void Update();
    /// A copy of the counters.
    ///
    /// - Fails: never.
    ResourceServerStats Stats() const { return stats_; }
    /// The largest read the server serves (the configured one, clamped to the transport's message size).
    ///
    /// - Fails: never.
    u32 MaxReadBytes() const { return max_read_; }

private:
    struct Connection
    {
        net::ConnectionId id = 0;
        bool hello = false;
        u32 max_read = 0;  // what the client takes
        u32 served = 0;    // requests answered since the last Update began
    };

    Connection* Find(net::ConnectionId id);
    void Drop(net::ConnectionId id);
    void Violation(net::ConnectionId id);
    void Handle(Connection& connection, ByteSpan message);
    void Reply(Connection& connection, const ProtoData& response, ByteSpan payload = ByteSpan());
    void Fail(Connection& connection, u32 request, ProtoError error);
    bool EnsureTable();
    void PollBackend();
    void Serve(Connection& connection, const ProtoData& request);

    ResourceBackend* backend_;
    net::Transport* transport_;
    ResourceServerConfig config_;
    u32 max_read_;
    Vector<byte> buffer_;  // the response being built
    Vector<byte> data_;    // the bytes of a read
    Vector<Connection> connections_;
    UniquePtr<ResourceTable> table_;
    bool table_valid_ = false;
    u64 generation_ = 0;
    Vector<ChangeRecord> log_;  // the last changes the backend reported, oldest first
    u64 epoch_;  // the changes reported so far, counted from a value of this run; the log holds the last `log_.Size()`
                 // of them
    ResourceServerStats stats_;
};

}  // namespace vse::asset
