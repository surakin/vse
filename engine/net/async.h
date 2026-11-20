#pragma once

//! Asynchronous socket operations for coroutines: [[net::Accept]], [[net::Connect]], [[net::Receive]], [[net::Send]],
//! [[net::ReceiveFrom]] and [[net::SendTo]].
//!
//! Each is a [[jobs::Task]]: `co_await` it from a job (or another task) and it returns an [[net::IoResult]]. It tries
//! the non-blocking operation of the [[net::Socket]] first; when that would block it waits on the [[net::Reactor]] (so
//! the coroutine is resumed on a participant, never on the reactor thread) and tries again, until the operation
//! finishes, fails, times out or is cancelled. The arguments are borrowed for the whole operation: the socket, buffers
//! and endpoints must stay alive and unmoved until the `co_await` returns, which is the case when the task is awaited
//! in the expression that creates it.
//!
//! The timeout of a [[net::WaitOptions]] covers the whole operation, not each wait: a [[net::Send]] that trickles data
//! still ends when the time is up. Only one coroutine may wait for each direction of a socket at a time.

#include "containers/span.h"
#include "jobs/task.h"
#include "net/endpoint.h"
#include "net/reactor.h"
#include "net/socket.h"
#include "net/status.h"
#include "types.h"

namespace vse::net
{

/// The outcome of an asynchronous operation: a status and a byte count.
///
/// - Thread-safety: thread-compatible plain data.
struct IoResult
{
    Status status;    ///< Success, or why the operation ended: a socket error, TimedOut, Cancelled or Closed.
    usize count = 0;  ///< The bytes transferred before it ended (also when it failed part way).
};

/// Receives into `buffer`, waiting until at least one byte (or a datagram) has arrived.
/// - Param reactor: the reactor to wait on.
/// - Param socket: a connected stream, or a UDP socket.
/// - Param buffer: where the data goes; must not be empty.
/// - Param options: token and timeout of the whole operation.
/// - Returns: Success with the count; on a stream the end of the stream is [[net::ErrorCode::Closed]] with a count of
/// zero.
jobs::Task<IoResult> Receive(Reactor& reactor, Socket& socket, MutableByteSpan buffer, WaitOptions options = {});

/// Sends every byte of `bytes`, waiting whenever the socket's buffer is full.
/// - Param reactor: the reactor to wait on.
/// - Param socket: a connected socket.
/// - Param bytes: the data; for a UDP socket one datagram.
/// - Param options: token and timeout of the whole operation.
/// - Returns: Success with `bytes.Size()`, or the failure with the count sent before it.
jobs::Task<IoResult> Send(Reactor& reactor, Socket& socket, ByteSpan bytes, WaitOptions options = {});

/// Receives one datagram and its sender.
/// - Param reactor: the reactor to wait on.
/// - Param socket: a UDP socket.
/// - Param buffer: where the datagram goes; must not be empty.
/// - Param from: receives the sender.
/// - Param options: token and timeout of the whole operation.
/// - Ownership: `socket`, the buffer and the output pointers are borrowed until the `co_await` returns.
/// - Returns: Success with the datagram's size, [[net::ErrorCode::MessageTooLarge]] if it did not fit, or another
/// failure.
jobs::Task<IoResult> ReceiveFrom(Reactor& reactor, Socket& socket, MutableByteSpan buffer, Endpoint* from,
                                 WaitOptions options = {});

/// Sends one datagram, waiting while the socket's buffer is full.
/// - Param reactor: the reactor to wait on.
/// - Param socket: a UDP socket.
/// - Param bytes: the datagram.
/// - Param to: where to send it.
/// - Param options: token and timeout of the whole operation.
/// - Returns: Success with the size, or the failure.
jobs::Task<IoResult> SendTo(Reactor& reactor, Socket& socket, ByteSpan bytes, const Endpoint& to,
                            WaitOptions options = {});

/// Accepts one connection, waiting until one is pending.
/// - Param reactor: the reactor to wait on.
/// - Param listener: a listening socket.
/// - Param connection: receives the new socket; must not be open.
/// - Param peer: receives the peer's endpoint.
/// - Param options: token and timeout of the whole operation.
/// - Ownership: `socket`, the buffer and the output pointers are borrowed until the `co_await` returns.
/// - Returns: Success, or the failure (the count is zero).
jobs::Task<IoResult> Accept(Reactor& reactor, Socket& listener, Socket* connection, Endpoint* peer,
                            WaitOptions options = {});

/// Connects `socket` to `remote`, waiting for the handshake.
/// - Param reactor: the reactor to wait on.
/// - Param socket: a fresh stream socket.
/// - Param remote: where to connect.
/// - Param options: token and timeout of the whole operation (the connect timeout).
/// - Returns: Success, or why not ([[net::ErrorCode::Refused]], [[net::ErrorCode::TimedOut]], ...); the count is zero.
jobs::Task<IoResult> Connect(Reactor& reactor, Socket& socket, const Endpoint& remote, WaitOptions options = {});

}  // namespace vse::net
