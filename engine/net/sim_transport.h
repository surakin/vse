#pragma once

//! [[net::SimulatedTransport]]: a complete [[net::Transport]] that runs over a [[net::SimNetwork]], with its own small
//! reliability protocol (acknowledgements, retransmission with backoff, a send window, keep-alive, time-out), so that
//! every guarantee of the interface can be tested under loss, reordering, duplication and corruption, and replayed
//! exactly from a seed.
//!
//! It is also the specification by example of the interface: the conformance suite
//! (`tests/net/transport_conformance.h`) passes on it first, and the UDP and TCP transports must pass the same tests.
//!
//! **Protocol.** One packet carries one message or one control item (connect request, accept, refuse, acknowledgement,
//! ping, pong, close) behind a 4-byte checksum, so a corrupted packet is dropped and counts as loss. Reliable messages
//! are numbered per channel and kind, acknowledged one by one, and sent again when the retransmission time (twice the
//! smoothed round trip plus four times its variance, doubled after each unanswered try, between 20 ms and 2 s) has
//! passed. At most 256 reliable messages are in flight per connection; the rest wait in a queue of
//! [[net::SimTransportConfig::max_queued_messages]]. Messages are at most
//! [[net::SimTransportConfig::max_message_bytes]] bytes: one packet each, with no fragmentation.

#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "net/sim_network.h"
#include "net/transport.h"
#include "types.h"

namespace vse::net
{

/// Limits and times of a [[net::SimulatedTransport]].
///
/// - Thread-safety: thread-compatible plain data.
struct SimTransportConfig
{
    u8 channels = 8;                        ///< Channels per connection.
    usize max_message_bytes = 1024;         ///< Largest message; a packet must fit the link's MTU with its header.
    usize max_connections = 1024;           ///< Connections at once, both kinds.
    usize max_queued_messages = 4096;       ///< Reliable messages waiting or in flight per connection.
    u64 timeout_nanos = 10'000'000'000ull;  ///< Silence after which a connection times out.
    u64 connect_timeout_nanos = 5'000'000'000ull;  ///< Time to get a connect answered.
    u64 keepalive_nanos = 1'000'000'000ull;        ///< Silence after which a ping is sent.
};

/// A transport over a simulated network.
///
/// - Thread-safety: thread-compatible; the thread that advances the network also uses the transport.
/// - Ownership: attaches itself to the network at `local` and detaches in the destructor; the network must outlive it.
class SimulatedTransport final : public Transport, private PacketSink
{
public:
    /// Creates a transport at `local` on `network`.
    /// - Param network: the network; must outlive the transport.
    /// - Param local: the transport's own endpoint (used as the source of its packets).
    /// - Param config: limits and times.
    SimulatedTransport(SimNetwork& network, const Endpoint& local, const SimTransportConfig& config = {});

    /// Detaches from the network. Connections end silently; the peers will time out.
    ~SimulatedTransport() override;

    /// The transport's own endpoint.
    /// - Returns: the endpoint.
    Endpoint LocalEndpoint() const { return local_; }

    /// Starts accepting connections at the transport's own endpoint (it can listen nowhere else).
    /// - Param at: must equal the local endpoint.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]] if `at` is not the local endpoint or the transport already listens.
    Status Listen(const Endpoint& at) override;
    /// See [[net::Transport::Connect]].
    /// - Param to: the peer.
    /// - Param connection: receives the id.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::LimitExceeded]].
    /// - Ownership: `*connection` is written on success.
    Status Connect(const Endpoint& to, ConnectionId* connection) override;
    /// See [[net::Transport::Send]].
    /// - Param connection: the connection.
    /// - Param channel: the channel.
    /// - Param bytes: the message.
    /// - Param reliability: how to carry it.
    /// - Returns: success.
    /// - Fails: those of [[net::Transport::Send]].
    Status Send(ConnectionId connection, u8 channel, ByteSpan bytes, Reliability reliability) override;
    /// See [[net::Transport::Poll]].
    /// - Param event: receives the event.
    /// - Returns: false when there is none.
    /// - Ownership: `*event` is written when true is returned.
    bool Poll(Event* event) override;
    /// See [[net::Transport::Close]].
    /// - Param connection: the connection.
    /// - Param code: the code for the peer.
    /// - Returns: success.
    /// - Fails: those of [[net::Transport::Close]].
    Status Close(ConnectionId connection, u32 code) override;
    /// See [[net::Transport::Statistics]].
    /// - Param connection: the connection.
    /// - Param stats: receives the counters.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]].
    /// - Ownership: `*stats` is written on success.
    Status Statistics(ConnectionId connection, ConnectionStats* stats) const override;
    /// All four kinds are carried as asked.
    /// - Returns: true.
    bool Supports(Reliability) const override { return true; }
    /// The largest message.
    /// - Returns: [[net::SimTransportConfig::max_message_bytes]].
    usize MaxMessageBytes() const override { return config_.max_message_bytes; }
    /// The number of channels.
    /// - Returns: [[net::SimTransportConfig::channels]].
    u8 ChannelCount() const override { return config_.channels; }

private:
    struct Impl;
    void OnPacket(const Endpoint& from, ByteSpan packet) override;
    void OnTick(u64 now_nanos) override;
    void OnUnreachable(const Endpoint& to) override;

    SimNetwork* network_;
    Endpoint local_;
    SimTransportConfig config_;
    bool attached_ = false;  // the network accepted our endpoint
    UniquePtr<Impl> impl_;
};

}  // namespace vse::net
