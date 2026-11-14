#pragma once

//! [[asset::NetworkBackend]] (Phase 22 Day 332): a [[asset::ResourceBackend]] whose resources live on a
//! [[asset::ResourceServer]]. It talks the protocol of `resource_protocol.h` over a [[net::Transport]] it is given (the
//! TCP transport over sockets, or over the simulated streams in tests), keeps the blobs it fetched in a
//! [[asset::BlobCache]] so a blob is not fetched twice and is served when the server cannot be reached, retries a
//! failed request with a bounded exponential back-off, and reports `Offline` when it gives up, reconnecting in the
//! background.
//!
//! **Reading.** A resource of at most `assets.net.prefetch_bytes` is fetched whole (and verified against the hash its
//! header records, then cached), larger ones are read by range, a request each. Before it fetches a small blob it
//! reads its header and asks the cache whether the hash is held: the cache is keyed by content, so a blob that changed
//! on the server is fetched and one that did not is not.
//!
//! **Offline.** When the retries run out the state is `Offline`: `Stat` and `Open` are served from the cache for an id
//! the cache knows (the hash it last saw, with the blob held), the table is the last one read, and everything else
//! returns `Offline`. A background reconnect (every `reconnect_nanos`) brings it back `Online`. The resource manager
//! treats `Offline` like `NotFound` and does not remember the failure.
//!
//! **Driving.** The transport is polled by a thread of the backend's own by default. A test (or a program with its own
//! loop) can turn that off and call [[asset::NetworkBackend::Poll]]; the blocking calls then poll while they wait and
//! call the `idle` function between polls (a test moves its clock and updates its server there).

#include "asset/backend.h"
#include "asset/blob_cache.h"
#include "asset/resource_protocol.h"
#include "asset/resource_table.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "net/clock.h"
#include "net/transport.h"
#include "settings/registry.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// Declares the settings of the network backend: `assets.net.timeout_ms` (how long a request, a connection or a
/// handshake may take), `assets.net.max_retries` (attempts after the first before the backend is `Offline`),
/// `assets.net.backoff_ms` (the first delay between attempts, doubled each time up to 32 times),
/// `assets.net.prefetch_bytes` (resources up to this size are fetched whole), `assets.net.cache_bytes` (the most bytes
/// of blobs the disk cache holds) and `assets.cache_dir` (the cache directory; empty for no disk cache). A key that is
/// already declared is left as it is.
///
/// - Thread-safety: thread-compatible; call it while the registry is being set up.
/// - Ownership: borrows its pointer arguments for the call only; nothing is retained.
void RegisterNetworkSettings(settings::Registry* registry);

/// What a [[asset::NetworkBackend]] waits and tries.
///
/// - Thread-safety: thread-compatible plain data.
struct NetworkBackendConfig
{
    u64 timeout_nanos = 5'000'000'000ull;      ///< One request, a connection or a handshake.
    u32 max_retries = 3;                       ///< Attempts after the first; then the backend is `Offline`.
    u64 backoff_nanos = 100'000'000ull;        ///< The delay after the first failed attempt; doubled each time.
    u64 backoff_max_nanos = 2'000'000'000ull;  ///< The longest delay.
    u64 reconnect_nanos = 1'000'000'000ull;    ///< The time between connection attempts while `Offline`.
    u64 prefetch_bytes = 1u << 20;             ///< Resources up to this size are fetched whole.
    u64 jitter_seed = 1;                       ///< The seed of the generator that spreads the back-off.
};

/// Reads a [[asset::NetworkBackendConfig]] from the settings (the cache's size and directory are
/// [[asset::NetworkCacheFromSettings]]).
///
/// - Param registry: the settings; `RegisterNetworkSettings` must have been called.
/// - Aborts: a setting was not declared.
/// - Thread-safety: thread-safe.
NetworkBackendConfig NetworkBackendConfigFromSettings(const settings::Registry& registry);

/// Opens the disk cache the settings name.
///
/// - Param out: receives the cache, or an empty pointer when `assets.cache_dir` is empty.
/// - Fails: the failures of [[asset::BlobCache::Open]], `Invalid` for a directory that is not a path.
/// - Aborts: a setting was not declared.
/// - Ownership: `*out` receives the cache.
Status NetworkCacheFromSettings(const settings::Registry& registry, UniquePtr<BlobCache>* out);

/// Where the connection to the server is.
enum class NetworkState : u8
{
    Connecting,  ///< Not connected yet (the first connection, or a lost one being replaced).
    Online,      ///< Connected and greeted.
    Offline,     ///< The retries ran out: the cache answers what it can.
};

/// What a backend is made of.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows the transport, the clock and the cache, which must outlive the backend.
struct NetworkBackendOptions
{
    net::Transport* transport = nullptr;  ///< Where messages go; polled only by the backend.
    net::Endpoint server;                 ///< The server.
    const net::Clock* clock = nullptr;    ///< The time source of every timeout.
    BlobCache* cache = nullptr;           ///< The disk cache, or null for none.
    NetworkBackendConfig config;          ///< The times and limits.
    bool background_thread = true;        ///< Poll the transport on a thread of the backend's own.
    void (*idle)(void* user) = nullptr;   ///< Without the thread: called between polls while a call waits.
    void* idle_user = nullptr;            ///< Passed to `idle`.
};

/// What the backend counted.
///
/// - Thread-safety: immutable value (a copy).
struct NetworkBackendStats
{
    u64 requests = 0;        ///< Requests sent (retries included).
    u64 retries = 0;         ///< Attempts after the first.
    u64 timeouts = 0;        ///< Requests that timed out.
    u64 reconnects = 0;      ///< Connections made after the first.
    u64 cache_serves = 0;    ///< Blobs served from the cache.
    u64 offline_serves = 0;  ///< Of those, the ones served while `Offline`.
    u64 fetched_bytes = 0;   ///< Bytes of resources read from the server.
};

/// The network backend.
///
/// - Thread-safety: thread-safe; every member may be called from any thread. Calls block (the manager's workers and
///   jobs may call them; the frame thread should not).
/// - Ownership: borrows what the options name, which must outlive it; the readers it hands out must not outlive it.
class NetworkBackend final : public ResourceBackend
{
public:
    /// Makes a backend and starts connecting.
    ///
    /// - Ownership: borrows the options' transport, clock and cache.
    explicit NetworkBackend(const NetworkBackendOptions& options);
    /// Stops the thread and closes the connection.
    ~NetworkBackend() override;

    /// Where the connection is.
    ///
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    NetworkState State() const;
    /// A copy of the counters.
    ///
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    NetworkBackendStats Stats() const;
    /// Polls the transport once and runs the timers: for a backend without a thread of its own.
    ///
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    void Poll();

    const char* Name() const override { return "network"; }
    u32 Capabilities() const override { return kBackendRanged | kBackendWatch | kBackendAsyncTable; }
    Status Stat(ResourceId id, ResourceInfo* out) override;
    Status Open(ResourceId id, UniquePtr<ResourceReader>* out) override;
    Status ReadTable(ResourceTable* out) override;
    UniquePtr<TableTicket> ReadTableAsync() override;
    Status Watch(Vector<ChangeRecord>* out) override;

    /// A range of a resource from the server (used by the readers of large resources).
    ///
    /// - Ownership: borrows its pointer arguments for the call.
    Status ReadRange(ResourceId id, u64 offset, void* buffer, usize length);

private:
    struct Slot;
    struct Fetch;
    struct Pending  // a table read that ended: its ticket is completed outside the mutex
    {
        TableTicket* ticket;
        Status status;
    };

    void PumpLocked();
    void LostLocked(bool counts);
    void StepFetchLocked();
    void FinishFetchLocked(const Status& status);
    Status Call(const ProtoData& request, ProtoData* response);
    Status Attempt(const ProtoData& request, ProtoData* response);
    void Idle();
    Status ToStatus(ProtoError error) const;
    u64 Jitter(u64 limit);
    Status FetchTable(ResourceTable* out);
    Status FetchBlob(ResourceId id, u64 size, Vector<byte>* bytes, ContentHash* hash);
    void RunThread();
    void Drain();
    void SetOfflineLocked();

    NetworkBackendOptions options_;
    mutable Mutex mutex_;
    NetworkState state_ = NetworkState::Connecting;
    net::ConnectionId connection_ = 0;
    bool open_ = false;     // the transport reported the connection open
    bool greeted_ = false;  // the server answered Hello
    u64 connect_started_ = 0;
    u64 next_connect_ = 0;
    u32 failures_ = 0;  // consecutive failed attempts and connections
    bool connected_once_ = false;
    u32 max_read_ = 0;  // what the server serves per Read
    u32 page_entries_ = 0;
    u32 next_request_ = 1;
    Vector<UniquePtr<Slot>> slots_;
    UniquePtr<Fetch> fetch_;
    Vector<Pending> pending_;
    UniquePtr<ResourceTable> table_;  // the last table read
    u64 generation_ = 0;
    bool epoch_known_ = false;
    u64 epoch_ = 0;
    u64 rng_;
    NetworkBackendStats stats_;
    Atomic<bool> stop_{false};
    Thread thread_;
};

}  // namespace engine_placeholder::asset
