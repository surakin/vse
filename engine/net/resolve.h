#pragma once

//! [[net::Resolve]]: a host name and a port to endpoints, as a task, and [[net::ConnectAny]] / [[net::ConnectTo]] built
//! on it.

#include "jobs/task.h"
#include "net/async.h"
#include "net/endpoint.h"
#include "net/reactor.h"
#include "net/resolver.h"
#include "net/socket.h"
#include "net/status.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::net
{

/// The outcome of [[net::Resolve]].
///
/// - Thread-safety: thread-compatible plain data.
struct ResolveResult
{
    Status status;          ///< Success, or why not: NotFound, TimedOut, Cancelled, LimitExceeded, ...
    EndpointSet endpoints;  ///< The endpoints in the system's order (at most [[net::EndpointSet::kCapacity]]); empty on
                            ///< failure.
};

/// Turns `host` and `port` into endpoints without blocking a participant or the reactor.
///
/// A numeric host (`127.0.0.1`, `::1`, `[::1]`) is parsed here and never reaches the resolver thread. A name is queued
/// for the
/// [[net::Resolver]]; the coroutine waits for its signal, with the timeout and cancel token of `options`. The operating
/// system's resolver cannot be cancelled, so a resolution that times out or is cancelled is abandoned: the coroutine
/// goes on at once and the result is dropped when the thread finishes.
/// - Param reactor: the reactor the coroutine waits on.
/// - Param resolver: the resolver thread to use (not used for a numeric host).
/// - Param host: a host name or a numeric address, not containing a zero byte; read when the task starts, so it must
/// stay valid from the call until the `co_await` returns (await the task in the expression that creates it).
/// - Param port: the port of every endpoint.
/// - Param family: [[net::AddressFamily::V4]] or [[net::AddressFamily::V6]] to ask for one family,
/// [[net::AddressFamily::None]] for any.
/// - Param options: token and timeout.
/// - Returns: the endpoints, or the failure in the status.
/// - Fails: [[net::ErrorCode::Invalid]] (empty or too long), [[net::ErrorCode::NotFound]],
/// [[net::ErrorCode::TimedOut]],
/// [[net::ErrorCode::Cancelled]], [[net::ErrorCode::LimitExceeded]] (the resolver's queue is full).
/// - Ownership: `reactor`, `resolver` and `host` are borrowed until the `co_await` returns.
jobs::Task<ResolveResult> Resolve(Reactor& reactor, Resolver& resolver, StringView host, u16 port,
                                  AddressFamily family = AddressFamily::None, WaitOptions options = {});

/// How [[net::ConnectAny]] tries a list of endpoints.
///
/// - Thread-safety: thread-compatible plain data.
struct ConnectOptions
{
    i64 attempt_timeout_nanos = 5'000'000'000;   ///< How long each endpoint gets; negative for no limit.
    i64 resolve_timeout_nanos = 30'000'000'000;  ///< How long [[net::ConnectTo]] gives the name resolution; negative
                                                 ///< for no limit.
    CancelToken* token = nullptr;                ///< Cancels the whole operation.
};

/// Connects `socket` to the first endpoint of `endpoints` that accepts, trying them one after the other, each for at
/// most the attempt timeout (a sequential "happy eyeballs": the order is the resolver's, which puts the preferred
/// family first). A socket of the right family is opened for each attempt and closed again if it fails.
/// - Param reactor: the reactor to wait on.
/// - Param endpoints: the candidates, in order; not empty.
/// - Param socket: receives the connected stream socket; must not be open.
/// - Param options: per-attempt timeout and cancel token.
/// - Ownership: `endpoints` and `*socket` are borrowed until the `co_await` returns; the socket receives the
/// connection.
/// - Returns: success, or the failure of the last attempt ([[net::ErrorCode::Cancelled]] as soon as the token is
/// cancelled).
/// - Fails: [[net::ErrorCode::Invalid]] for an empty set or an open `*socket`.
jobs::Task<IoResult> ConnectAny(Reactor& reactor, const EndpointSet& endpoints, Socket* socket,
                                ConnectOptions options = {});

/// Resolves `host` and connects to the first endpoint that accepts: [[net::Resolve]], then [[net::ConnectAny]].
/// - Param reactor: the reactor to wait on.
/// - Param resolver: the resolver thread.
/// - Param host: a name or an address.
/// - Param port: the port.
/// - Param socket: receives the connected socket; must not be open.
/// - Param options: the resolution and attempt timeouts, and the token (it cancels both steps).
/// - Ownership: `host` and `*socket` are borrowed until the `co_await` returns; the socket receives the connection.
/// - Returns: success, or why not.
jobs::Task<IoResult> ConnectTo(Reactor& reactor, Resolver& resolver, StringView host, u16 port, Socket* socket,
                               ConnectOptions options = {});

}  // namespace engine_placeholder::net
