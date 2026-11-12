#pragma once

//! [[net::Resolver]]: turns host names into endpoints on a thread of its own, so a frame never waits for a name server.

#include "containers/shared_ptr.h"
#include "containers/vector.h"
#include "net/address.h"
#include "net/endpoint.h"
#include "net/signal.h"
#include "net/status.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

namespace engine_placeholder::net
{

/// The longest host name the resolver accepts, in bytes (the DNS limit of 253, without the terminating zero).
inline constexpr usize kMaxHostNameBytes = 253;

/// Resolves `host` and `port` with the operating system's resolver into at most [[net::EndpointSet::kCapacity]]
/// endpoints: the addresses the system returns, in its order, without duplicates, the rest dropped.
///
/// - Backend: `getaddrinfo` with a stream socket hint and a numeric service (`engine/net/posix/resolve_posix.cpp`;
/// `GetAddrInfoW` with the name in UTF-16 in `engine/net/windows/resolve_windows.cpp`); the
/// error codes `EAI_*` become [[net::ErrorCode::NotFound]] (the name does not exist), [[net::ErrorCode::TimedOut]] (the
/// name server did not answer),
/// [[net::ErrorCode::Unreachable]], [[net::ErrorCode::LimitExceeded]] or [[net::ErrorCode::Other]].
/// - Param host: a host name, not an address and not empty.
/// - Param port: the port every endpoint gets.
/// - Param family: [[net::AddressFamily::V4]] or [[net::AddressFamily::V6]] to ask for one family,
/// [[net::AddressFamily::None]] for both.
/// - Param out: receives the endpoints.
/// - Returns: success with at least one endpoint, or the failure.
/// - Ownership: `host` is only read during the call; `*out` is written.
/// - Fails: [[net::ErrorCode::NotFound]] and the others above; [[net::ErrorCode::Invalid]] for an empty or too long
/// name, or one that contains a zero byte.
/// - Thread-safety: thread-safe. It blocks for as long as the name server takes.
Status SystemResolve(StringView host, u16 port, AddressFamily family, EndpointSet* out);

/// A resolver function: [[net::SystemResolve]], or a test's own.
using ResolveFn = Status (*)(StringView host, u16 port, AddressFamily family, EndpointSet* out);

/// One request to the resolver thread; shared between the coroutine that asked, the thread, and nobody else, so that a
/// request the coroutine abandoned (a timeout, a cancel) stays valid until the thread has finished it. Internal to
/// [[net::Resolve]].
///
/// - Internal: public only so that the resolver's queue can hold it.
/// - Thread-safety: thread-compatible; the thread writes the result and then fires `done`, and the coroutine reads it
/// only after `done` ended its wait.
struct ResolveRequest
{
    char host[kMaxHostNameBytes + 1] = {};       ///< The name, zero-terminated.
    u16 port = 0;                                ///< The port.
    AddressFamily family = AddressFamily::None;  ///< The family asked for.
    Status status;                               ///< The outcome, valid after `done` fired.
    EndpointSet endpoints;                       ///< The endpoints, valid after `done` fired and `status` is success.
    Atomic<bool> abandoned{
        false};   ///< Set by the coroutine that gave up; the thread then skips the lookup if it has not started.
    Signal done;  ///< Fired by the resolver thread when it is finished (also when it dropped the request).
};

/// One thread that runs resolutions in the order they were submitted, from a queue of bounded size.
///
/// The operating system's resolver cannot be cancelled, so a resolution that was abandoned (its coroutine timed out or
/// was cancelled) still runs to its end and its result is dropped; only the thread is occupied meanwhile. Shut the
/// resolver down before the reactor that the coroutines wait on, and expect [[net::Resolver::Shutdown]] to wait for the
/// resolution in progress.
///
/// - Thread-safety: thread-safe. Any thread may submit or shut down.
/// - Ownership: owns its thread and joins it in the destructor; queued requests are shared with their coroutines.
class Resolver
{
public:
    /// The default number of requests that may wait in the queue.
    static constexpr usize kDefaultQueue = 256;

    /// Starts the thread.
    /// - Param function: the resolver function to call; [[net::SystemResolve]] by default, a test passes its own.
    /// - Param queue_capacity: how many requests may wait; further ones are refused with
    /// [[net::ErrorCode::LimitExceeded]].
    explicit Resolver(ResolveFn function = &SystemResolve, usize queue_capacity = kDefaultQueue);

    /// Shuts down as [[net::Resolver::Shutdown]] does.
    ~Resolver();

    Resolver(const Resolver&) = delete;
    Resolver& operator=(const Resolver&) = delete;

    /// Queues a request. The thread fills in its result and fires its signal.
    /// - Param request: the request; shared, so the caller may drop its own reference at any time.
    /// - Returns: success, or why the request was not queued ([[net::ErrorCode::LimitExceeded]] when the queue is full,
    /// [[net::ErrorCode::Cancelled]] after shutdown).
    /// - Fails: as described under Returns; the request is not queued and its signal is not fired.
    Status Submit(const SharedPtr<ResolveRequest>& request);

    /// Stops accepting requests, ends the queued ones with [[net::ErrorCode::Cancelled]], waits for the resolution in
    /// progress and joins the thread. Idempotent.
    void Shutdown();

private:
    void Loop();

    ResolveFn function_;
    Mutex lock_;
    ConditionVariable wake_;
    Vector<SharedPtr<ResolveRequest>> ring_;  // a ring of capacity entries
    usize head_ = 0;
    usize count_ = 0;
    bool stopping_ = false;
    Mutex shutdown_lock_;
    bool joined_ = false;
    Thread thread_;
};

}  // namespace engine_placeholder::net
