#pragma once

//! [[net::DatagramPort]]: the datagram I/O the UDP transport runs over, so the same transport code runs on a real
//! UDP socket ([[net::SocketDatagramPort]]) and on the simulated network ([[net::SimDatagramPort]]).

#include "containers/span.h"
#include "net/endpoint.h"
#include "net/status.h"
#include "types.h"

namespace vse::net
{

/// Sends and receives whole datagrams at one local endpoint. Never blocks.
///
/// - Thread-safety: thread-compatible; the thread that polls the transport also uses the port.
class DatagramPort
{
public:
    virtual ~DatagramPort() = default;

    /// Sends one datagram. A datagram that cannot be sent now is lost, as on the wire.
    /// - Param bytes: the datagram; read during the call.
    /// - Param to: the destination.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::WouldBlock]], [[net::ErrorCode::MessageTooLarge]], [[net::ErrorCode::Unreachable]],
    /// [[net::ErrorCode::Invalid]].
    virtual Status SendTo(ByteSpan bytes, const Endpoint& to) = 0;

    /// Takes the next received datagram.
    /// - Param buffer: receives the bytes; a datagram that does not fit is dropped and reported as an error.
    /// - Param received: receives the size.
    /// - Param from: receives the sender.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::WouldBlock]] when there is none, [[net::ErrorCode::MessageTooLarge]].
    /// - Ownership: the outputs are written on success.
    virtual Status ReceiveFrom(MutableByteSpan buffer, usize* received, Endpoint* from) = 0;

    /// The port's own endpoint.
    /// - Returns: the endpoint.
    virtual Endpoint LocalEndpoint() const = 0;
};

}  // namespace vse::net
