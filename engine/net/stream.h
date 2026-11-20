#pragma once

//! [[net::StreamIo]] and [[net::StreamProvider]]: the byte-stream I/O the TCP transport runs over, so the same
//! transport code runs on real TCP sockets ([[net::SocketStreamProvider]]) and on a simulated reliable stream
//! ([[net::SimStreamNetwork]]).

#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "net/endpoint.h"
#include "net/status.h"
#include "types.h"

namespace vse::net
{

/// One connected (or connecting) byte stream. Never blocks.
///
/// - Thread-safety: thread-compatible; the thread that polls the transport also uses the stream.
class StreamIo
{
public:
    virtual ~StreamIo() = default;

    /// Completes a connection that was under way.
    /// - Returns: success when connected.
    /// - Fails: [[net::ErrorCode::WouldBlock]] while still connecting, or the connection's error
    /// ([[net::ErrorCode::Refused]], [[net::ErrorCode::TimedOut]], [[net::ErrorCode::Unreachable]]).
    virtual Status FinishConnect() = 0;

    /// Takes the bytes that have arrived.
    /// - Param buffer: receives the bytes; not empty.
    /// - Param received: receives the count.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::WouldBlock]] when none, [[net::ErrorCode::Closed]] at the end of the stream (the peer
    /// shut down its writing half), [[net::ErrorCode::Reset]].
    /// - Ownership: the outputs are written on success.
    virtual Status Receive(MutableByteSpan buffer, usize* received) = 0;

    /// Sends bytes; fewer than asked may be taken.
    /// - Param bytes: the bytes; read during the call.
    /// - Param sent: receives the count taken.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::WouldBlock]], [[net::ErrorCode::Closed]], [[net::ErrorCode::Reset]].
    /// - Ownership: `*sent` is written on success.
    virtual Status Send(ByteSpan bytes, usize* sent) = 0;

    /// Ends the writing half: the peer reads the end of the stream after what was sent.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::NotConnected]].
    virtual Status ShutdownWrite() = 0;
};

/// Listens for and makes streams at one local endpoint.
///
/// - Thread-safety: thread-compatible; the thread that polls the transport also uses the provider.
class StreamProvider
{
public:
    virtual ~StreamProvider() = default;

    /// Starts accepting streams at the provider's endpoint.
    /// - Param at: where to listen.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::AddressInUse]], [[net::ErrorCode::Invalid]].
    virtual Status Listen(const Endpoint& at) = 0;

    /// Takes one pending incoming stream.
    /// - Param stream: receives it.
    /// - Param peer: receives the peer's endpoint.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::WouldBlock]] when none is pending, [[net::ErrorCode::LimitExceeded]].
    /// - Ownership: the outputs are written on success.
    virtual Status Accept(UniquePtr<StreamIo>* stream, Endpoint* peer) = 0;

    /// Starts a connection.
    /// - Param to: the server.
    /// - Param stream: receives a stream that is connecting; poll [[net::StreamIo::FinishConnect]].
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Refused]], [[net::ErrorCode::Unreachable]], [[net::ErrorCode::LimitExceeded]],
    /// [[net::ErrorCode::Invalid]].
    /// - Ownership: `*stream` is written on success.
    virtual Status Connect(const Endpoint& to, UniquePtr<StreamIo>* stream) = 0;
};

}  // namespace vse::net
