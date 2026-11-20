#pragma once

//! [[net::SocketDatagramPort]]: a [[net::DatagramPort]] on a real, non-blocking UDP socket.

#include "net/datagram.h"
#include "net/socket.h"
#include "types.h"

namespace vse::net
{

/// A datagram port over an operating-system UDP socket.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns the socket.
class SocketDatagramPort final : public DatagramPort
{
public:
    /// Opens a UDP socket of `family` and binds it.
    /// - Param local: where to bind; port 0 picks a free port (see [[net::SocketDatagramPort::LocalEndpoint]]).
    /// - Param out: receives the port.
    /// - Param receive_buffer_bytes: the receive buffer to ask for (0: the system default); a smaller grant is not an
    /// error.
    /// - Returns: success.
    /// - Fails: the failures of [[net::Socket::OpenUdp]] and [[net::Socket::Bind]].
    /// - Ownership: `*out` owns the socket on success.
    static Status Open(const Endpoint& local, SocketDatagramPort* out, usize receive_buffer_bytes = 4u << 20);

    /// Creates a port that is not open.
    SocketDatagramPort() = default;

    /// Sends one datagram.
    /// - Param bytes: the datagram.
    /// - Param to: the destination.
    /// - Returns: success.
    /// - Fails: those of [[net::Socket::SendTo]].
    Status SendTo(ByteSpan bytes, const Endpoint& to) override;
    /// Receives one datagram.
    /// - Param buffer: receives the bytes.
    /// - Param received: receives the size.
    /// - Param from: receives the sender.
    /// - Returns: success.
    /// - Fails: those of [[net::Socket::ReceiveFrom]].
    /// - Ownership: the outputs are written on success.
    Status ReceiveFrom(MutableByteSpan buffer, usize* received, Endpoint* from) override;
    /// The bound endpoint.
    /// - Returns: the endpoint.
    Endpoint LocalEndpoint() const override { return local_; }

private:
    Socket socket_;
    Endpoint local_;
};

}  // namespace vse::net
