#pragma once

//! [[net::SimDatagramPort]]: a [[net::DatagramPort]] attached to a [[net::SimNetwork]].

#include "containers/vector.h"
#include "net/datagram.h"
#include "net/sim_network.h"
#include "types.h"

namespace vse::net
{

/// A datagram port on the simulated network: what the network delivers waits in a queue until the transport takes it.
///
/// - Thread-safety: thread-compatible; one thread drives the network and the transport.
/// - Ownership: attaches itself to the network at `local` and detaches in the destructor (only if the attach
/// succeeded); the network must outlive it.
class SimDatagramPort final : public DatagramPort, private PacketSink
{
public:
    /// Attaches to `network` at `local`.
    /// - Param network: the network; must outlive the port.
    /// - Param local: the port's endpoint.
    SimDatagramPort(SimNetwork& network, const Endpoint& local);

    /// Detaches from the network.
    ~SimDatagramPort() override;

    SimDatagramPort(const SimDatagramPort&) = delete;
    SimDatagramPort& operator=(const SimDatagramPort&) = delete;

    /// True if the network accepted the endpoint.
    /// - Returns: whether the port is attached.
    bool IsAttached() const { return attached_; }

    /// Hands the datagram to the network.
    /// - Param bytes: the datagram.
    /// - Param to: the destination.
    /// - Returns: success.
    Status SendTo(ByteSpan bytes, const Endpoint& to) override;
    /// Takes the next delivered datagram.
    /// - Param buffer: receives the bytes.
    /// - Param received: receives the size.
    /// - Param from: receives the sender.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::WouldBlock]], [[net::ErrorCode::MessageTooLarge]].
    /// - Ownership: the outputs are written on success.
    Status ReceiveFrom(MutableByteSpan buffer, usize* received, Endpoint* from) override;
    /// The port's endpoint.
    /// - Returns: the endpoint.
    Endpoint LocalEndpoint() const override { return local_; }

private:
    struct Datagram
    {
        Endpoint from;
        Vector<byte> data;
    };
    void OnPacket(const Endpoint& from, ByteSpan packet) override;
    void OnTick(u64 now_nanos) override;

    SimNetwork* network_;
    Endpoint local_;
    bool attached_ = false;
    Vector<Datagram> queue_;
    usize head_ = 0;
};

}  // namespace vse::net
