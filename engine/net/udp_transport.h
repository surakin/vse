#pragma once

//! [[net::UdpTransport]]: the game-traffic transport over UDP datagrams (Phase 20 Days 274-276). Day 274 builds the
//! first third: the packet format, a handshake that costs a server no state before the client proves it owns its
//! address, the connection state machine and the sequence/acknowledgement machinery of every packet. Messages are
//! carried unreliably, one per packet; channels with reliability, fragmentation, round-trip time and congestion
//! control follow on Days 275 and 276 behind the same [[net::Transport]] interface.
//!
//! **Handshake** (the wire format is in `net/udp_protocol.h`). The client sends a ConnectRequest padded to a full
//! datagram. The server answers with a small Challenge holding a cookie, a keyed hash of the client's endpoint, the
//! client's random salt and the current time period, and **keeps nothing**. The client repeats the cookie in a
//! ChallengeResponse (padded again); only then does the server create the connection and answer Accepted with the
//! connection id. Requests that are not full-size are dropped, so no answer is larger than the request that caused
//! it. A forged source address cannot complete the exchange and so cannot make the server hold a connection or flood
//! a third party. A cookie is valid for the period it was made in and the next one. Handshake packets are repeated
//! with a doubling delay until answered or the connect time-out.
//!
//! **Trust.** The cookie stops spoofed-source floods and amplification. It does not authenticate a peer and nothing
//! is encrypted: until the encryption outcome of Day 280 is built this transport is for trusted networks only.
//!
//! **Time and I/O.** The transport reads a [[net::Clock]] and moves datagrams through a [[net::DatagramPort]], so it
//! runs unchanged over a real socket ([[net::SocketDatagramPort]]) and over the simulated network
//! ([[net::SimDatagramPort]]). It is polled: [[net::UdpTransport::Poll]] receives, runs the timers and returns events.

#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "net/clock.h"
#include "net/datagram.h"
#include "net/transport.h"
#include "net/udp_protocol.h"
#include "types.h"

namespace engine_placeholder::net
{

/// Limits and times of a [[net::UdpTransport]].
///
/// - Thread-safety: thread-compatible plain data.
struct UdpTransportConfig
{
    u8 channels = 8;                               ///< Channels per connection.
    usize max_connections = 1024;                  ///< Connections at once, both kinds.
    usize max_connections_per_address = 8;         ///< Accepted connections from one address.
    u64 connect_timeout_nanos = 5'000'000'000ull;  ///< Time to complete a handshake.
    u64 timeout_nanos = 10'000'000'000ull;         ///< Silence after which a connection times out.
    u64 keepalive_nanos = 1'000'000'000ull;        ///< Silence after which an empty packet is sent.
    u64 ack_delay_nanos = 20'000'000ull;           ///< How long an acknowledgement waits for a packet to ride on.
    u64 handshake_retry_nanos = 200'000'000ull;   ///< First delay before a handshake packet is repeated (doubles, up to
                                                  ///< 1 s).
    u64 cookie_period_nanos = 10'000'000'000ull;  ///< Length of a cookie's time period; a cookie lives two.
    u32 close_repeats = 3;                        ///< Times a Disconnect packet is sent.
    u64 close_interval_nanos = 50'000'000ull;     ///< Delay between them.
    u16 initial_sequence = 0;                     ///< First packet sequence number (tests start near 65535).
    u64 seed = 0;  ///< Salts and cookie keys come from this seed if it is not zero (tests); otherwise from the system.
};

/// Counts of what the transport dropped or sent, for tests and diagnostics.
///
/// - Thread-safety: thread-compatible plain data.
struct UdpCounters
{
    u64 datagrams_received = 0;  ///< Datagrams taken from the port.
    u64 datagrams_sent = 0;      ///< Datagrams given to the port.
    u64 send_failures = 0;       ///< Datagrams the port refused.
    u64 dropped_short = 0;       ///< Shorter than their kind's header or not full-size where they must be.
    u64 dropped_oversize = 0;    ///< Longer than the MTU.
    u64 dropped_unknown = 0;     ///< Unknown version or kind.
    u64 dropped_bad_cookie = 0;  ///< A ChallengeResponse whose cookie did not check.
    u64 dropped_stale = 0;       ///< Replayed handshake packets and payloads too old to judge.
    u64 dropped_duplicate = 0;   ///< Payload packets whose sequence was already received.
    u64 challenges_sent = 0;     ///< Challenge packets sent.
    u64 resets_sent = 0;         ///< Reset packets sent for payloads of unknown connections.
    u64 denied_sent = 0;         ///< Denied packets sent.
};

/// What the transport knows of one connection's packet layer.
///
/// - Thread-safety: thread-compatible plain data.
struct UdpConnectionInfo
{
    bool connected = false;     ///< The handshake finished and the connection is open.
    u16 next_sequence = 0;      ///< The sequence number the next packet will carry.
    u16 latest_received = 0;    ///< The newest sequence received.
    bool any_received = false;  ///< Something was received.
    u64 packets_acked = 0;      ///< Packets of ours the peer acknowledged.
    u64 duplicates = 0;         ///< Payload packets dropped as duplicates.
    u64 too_old = 0;            ///< Payload packets dropped for being more than 32 behind.
};

/// A transport over UDP datagrams.
///
/// - Thread-safety: thread-compatible; the thread that polls it uses it.
/// - Ownership: borrows the port and the clock, which must outlive it.
class UdpTransport final : public Transport
{
public:
    /// Creates a transport on a port.
    /// - Param port: where datagrams go and come from; its local endpoint is the endpoint [[net::UdpTransport::Listen]]
    /// accepts.
    /// - Param clock: the time source.
    /// - Param config: limits and times.
    UdpTransport(DatagramPort& port, const Clock& clock, const UdpTransportConfig& config = {});

    /// Ends every connection silently; peers time out.
    ~UdpTransport() override;

    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    /// Starts accepting connections at the port's endpoint.
    /// - Param at: must equal the port's local endpoint.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]] if `at` is not the port's endpoint or the transport already listens.
    Status Listen(const Endpoint& at) override;
    /// Starts a handshake with `to`.
    /// - Param to: the server.
    /// - Param connection: receives the id.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::LimitExceeded]].
    /// - Ownership: `*connection` is written on success.
    Status Connect(const Endpoint& to, ConnectionId* connection) override;
    /// Sends a message, unreliably, in one packet.
    /// - Param connection: the connection.
    /// - Param channel: the channel.
    /// - Param bytes: the message.
    /// - Param reliability: only [[net::Reliability::Unreliable]] is carried before Day 275.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]], [[net::ErrorCode::MessageTooLarge]], [[net::ErrorCode::Closed]],
    /// [[net::ErrorCode::Unsupported]] for another reliability, [[net::ErrorCode::NotConnected]] while the handshake
    /// is under way.
    Status Send(ConnectionId connection, u8 channel, ByteSpan bytes, Reliability reliability) override;
    /// Receives datagrams, runs the timers and returns the next event.
    /// - Param event: receives the event.
    /// - Returns: false when there is none.
    /// - Ownership: `*event` is written when true is returned; its message view lasts until the next call.
    bool Poll(Event* event) override;
    /// Ends a connection; the peer is told by a Disconnect packet repeated a few times.
    /// - Param connection: the connection.
    /// - Param code: the code for the peer.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]], [[net::ErrorCode::Closed]].
    Status Close(ConnectionId connection, u32 code) override;
    /// Reads a connection's counters.
    /// - Param connection: the connection.
    /// - Param stats: receives the counters.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]].
    /// - Ownership: `*stats` is written on success.
    Status Statistics(ConnectionId connection, ConnectionStats* stats) const override;
    /// Only unreliable messages are carried as asked (so far).
    /// - Param reliability: the kind.
    /// - Returns: true for Unreliable.
    bool Supports(Reliability reliability) const override { return reliability == Reliability::Unreliable; }
    /// The largest message.
    /// - Returns: the bytes that fit one packet after the frame header.
    usize MaxMessageBytes() const override;
    /// The number of channels.
    /// - Returns: [[net::UdpTransportConfig::channels]].
    u8 ChannelCount() const override { return config_.channels; }

    /// Reads the packet layer of a connection.
    /// - Param connection: the connection.
    /// - Param info: receives the values.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]].
    /// - Ownership: `*info` is written on success.
    Status Info(ConnectionId connection, UdpConnectionInfo* info) const;

    /// The number of connections held, of both kinds, closed ones not yet reported included.
    /// - Returns: the count.
    usize ConnectionCount() const;

    /// What was dropped and sent.
    /// - Returns: the counters.
    const UdpCounters& Counters() const;

private:
    struct Impl;
    UdpTransportConfig config_;
    UniquePtr<Impl> impl_;
};

}  // namespace engine_placeholder::net
