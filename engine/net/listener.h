#pragma once

//! [[net::Listener]]: a listening socket with the accept loop's policy: back off when the process is out of
//! descriptors, and a cap on connections that were accepted but are not yet handled.

#include "jobs/task.h"
#include "net/async.h"
#include "net/endpoint.h"
#include "net/reactor.h"
#include "net/socket.h"
#include "net/status.h"
#include "sync/atomic.h"
#include "types.h"

namespace engine_placeholder::net
{

/// How a [[net::Listener]] is opened.
///
/// - Thread-safety: thread-compatible plain data.
struct ListenerOptions
{
    i32 backlog = 128;          ///< The length of the kernel's queue of connections not yet accepted.
    bool reuse_address = true;  ///< Allow binding a port whose old connections are still in the waiting state.
    bool dual_stack = true;   ///< On an IPv6 any-address listener, also accept IPv4 clients (as IPv4-mapped addresses).
    usize max_unhandled = 0;  ///< The most accepted connections that [[net::Listener::Handled]] has not been called
                              ///< for; 0 for no limit.
};

/// A listening socket and the policy of its accept loop.
///
/// - Thread-safety: thread-safe for [[net::Listener::Accept]] and [[net::Listener::Handled]] (several coroutines may
/// accept, as long as the socket allows one waiting reader at a time, which it does not: use one accept loop);
/// [[net::Listener::Open]] and closing are not.
/// - Ownership: owns the listening socket and closes it in the destructor. It cannot be moved (accepting coroutines
/// point at it).
class Listener
{
public:
    /// Creates a listener that is not open.
    Listener() = default;

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    /// Opens a socket, binds it to `at` and listens.
    ///
    /// For an IPv6 any-address endpoint (`[::]:port`) with `dual_stack` set, the socket also accepts IPv4 clients;
    /// whether it does is told by [[net::Listener::DualStack]] (some systems, or their configuration, do not allow it,
    /// and then the listener is IPv6 only).
    /// - Param at: where to listen; port 0 picks a free port (see [[net::Listener::LocalEndpoint]]).
    /// - Param options: backlog and policies.
    /// - Returns: success, or why not.
    /// - Fails: [[net::ErrorCode::AddressInUse]], [[net::ErrorCode::AddressNotAvailable]], [[net::ErrorCode::Invalid]]
    /// for an open listener or an endpoint with no address, [[net::ErrorCode::LimitExceeded]].
    Status Open(const Endpoint& at, const ListenerOptions& options = {});

    /// True while the listener is open.
    /// - Fails: never.
    bool IsOpen() const { return socket_.IsOpen(); }

    /// The listening socket, for closing it through [[net::Reactor::Close]].
    /// - Returns: the socket.
    Socket& GetSocket() { return socket_; }

    /// The endpoint the listener is bound to (with the port the system chose).
    /// - Returns: the endpoint; empty when not open.
    Endpoint LocalEndpoint() const { return local_; }

    /// Tells whether this listener accepts IPv4 clients on an IPv6 socket.
    /// - Returns: true only if it was asked for and the system allowed it.
    bool DualStack() const { return dual_stack_; }

    /// Accepts one connection, applying the policy of the listener.
    ///
    /// While [[net::ListenerOptions::max_unhandled]] connections are unhandled, nothing is accepted: the kernel's queue
    /// fills and further clients wait or are refused, which is the back-pressure. When the process is out of
    /// descriptors (`LimitExceeded`) the loop sleeps (5 ms, doubling to 200 ms) and tries again instead of spinning,
    /// until the options say stop. A connection that died before it was accepted is skipped.
    /// - Param reactor: the reactor to wait on.
    /// - Param connection: receives the new socket; must not be open.
    /// - Param peer: receives the peer's endpoint.
    /// - Param options: token and timeout of the whole call.
    /// - Returns: success, or the failure ([[net::ErrorCode::TimedOut]], [[net::ErrorCode::Cancelled]],
    /// [[net::ErrorCode::Closed]], ...); the count is zero.
    /// - Ownership: the listener, the socket and the endpoint are borrowed until the `co_await` returns.
    jobs::Task<IoResult> Accept(Reactor& reactor, Socket* connection, Endpoint* peer, WaitOptions options = {});

    /// Takes one place under the cap, as [[net::Listener::Accept]] does before it accepts (and counts one more when
    /// there is no cap). Call it if you take connections from the socket yourself; [[net::Listener::Handled]] gives the
    /// place back.
    /// - Returns: false, taking nothing, when the cap is reached.
    bool Reserve();

    /// Tells the listener that a connection it accepted is handled (closed, or handed to its own owner), making room
    /// under the cap.
    void Handled()
    {
        usize current = unhandled_.Load(MemoryOrder::Acquire);
        while (current > 0 &&
               !unhandled_.CompareExchangeWeak(current, current - 1, MemoryOrder::AcqRel, MemoryOrder::Acquire))
        {
        }
    }

    /// The number of accepted connections not yet reported handled.
    /// - Returns: the count.
    usize Unhandled() const { return unhandled_.Load(MemoryOrder::Acquire); }

private:
    Socket socket_;
    Endpoint local_;
    bool dual_stack_ = false;
    usize max_unhandled_ = 0;
    Atomic<usize> unhandled_{0};
};

}  // namespace engine_placeholder::net
