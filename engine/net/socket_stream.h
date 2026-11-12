#pragma once

//! [[net::SocketStreamProvider]]: a [[net::StreamProvider]] on real, non-blocking TCP sockets.

#include "net/socket.h"
#include "net/stream.h"
#include "types.h"

namespace engine_placeholder::net
{

/// Makes and accepts TCP streams. Sockets get `TCP_NODELAY`: the transport frames its own messages.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the listening socket.
class SocketStreamProvider final : public StreamProvider
{
public:
    /// Opens a TCP listener and starts listening.
    /// - Param at: the local endpoint (port 0 for a free port).
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::AddressInUse]], [[net::ErrorCode::Invalid]].
    Status Listen(const Endpoint& at) override;
    /// The endpoint that is listened on.
    /// - Returns: the endpoint with the port the system chose, or the default endpoint before
    /// [[net::SocketStreamProvider::Listen]].
    Endpoint LocalEndpoint() const { return local_; }
    /// Takes one pending connection.
    /// - Param stream: receives it.
    /// - Param peer: receives the peer's endpoint.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::WouldBlock]], [[net::ErrorCode::LimitExceeded]], [[net::ErrorCode::Invalid]].
    /// - Ownership: the outputs are written on success.
    Status Accept(UniquePtr<StreamIo>* stream, Endpoint* peer) override;
    /// Starts connecting.
    /// - Param to: the server.
    /// - Param stream: receives the connecting stream.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Refused]], [[net::ErrorCode::Unreachable]], [[net::ErrorCode::LimitExceeded]].
    /// - Ownership: `*stream` is written on success.
    Status Connect(const Endpoint& to, UniquePtr<StreamIo>* stream) override;

private:
    Socket listener_;
    Endpoint local_;
};

}  // namespace engine_placeholder::net
