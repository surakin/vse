#pragma once

//! [[net::UdpTransport]]: the game-traffic transport over UDP datagrams (Phase 20 Days 274-276). Day 274 builds the
//! first third: the packet format, a handshake that costs a server no state before the client proves it owns its
//! address, the connection state machine and the sequence/acknowledgement machinery of every packet. Day 275 adds the
//! four reliability kinds on independent channels, several small messages per packet and fragmentation of large ones;
//! congestion control and the refined round-trip estimate follow on Day 276, behind the same [[net::Transport]]
//! interface.
//!
//! **Data.** A message becomes one frame, or fragments of 1024 bytes for a message that does not fit a packet. Frames
//! of all channels are packed into packets (resent frames first, then channels in ascending order, so a lower channel
//! number is a higher priority). A packet's acknowledgement acknowledges its reliable frames; a frame is sent again
//! when its packet is three or more packets behind an acknowledged one and was not acknowledged, or when its time-out
//! (from the round-trip estimate, doubling per try) passes. Reliable-ordered delivery keeps a bounded reorder buffer
//! per channel; a peer whose gap exceeds it, or that exceeds the reassembly and reorder byte budget, is disconnected
//! with [[net::DisconnectReason::ProtocolError]]. Fragments are declared up front (index, count, total size) and
//! checked exactly: sizes, overlap and total are validated before any memory is committed.
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
#include "net/rate_limit.h"
#include "net/transport.h"
#include "net/udp_protocol.h"
#include "types.h"

namespace engine_placeholder::net
{

/// The default limits on handshake packets: 200 per second per address (burst 200) and 20,000 per second in total
/// (burst 2,000), no strikes or bans (an unverified source address could be forged).
/// - Returns: the limits.
inline RateLimiterConfig DefaultHandshakeLimits()
{
    RateLimiterConfig c;
    c.total_per_second = 20000;
    c.total_burst = 2000;
    c.address_per_second = 200;
    c.address_burst = 200;
    return c;
}

/// Limits and times of a [[net::UdpTransport]].
///
/// - Thread-safety: thread-compatible plain data.
struct UdpTransportConfig
{
    u8 channels = 8;                     ///< Channels per connection.
    usize max_message_bytes = 1u << 20;  ///< Largest message; above one packet it is fragmented (at most 65535
                                         ///< fragments of 1024 bytes; more is lowered to that).
    usize max_queued_frames = 8192;      ///< Frames (messages or fragments) waiting or unacknowledged per connection.
    usize window_frames = 256;           ///< Reliable frames in flight per connection.
    usize max_receive_buffer_bytes = 16u << 20;  ///< Reassembly plus out-of-order bytes per connection; more ends it.
    usize max_reassemblies = 256;                ///< Messages being reassembled per connection (at least the peer's
                                                 ///< window: a reliable sender may have that many partial at once).
    u64 reassembly_timeout_nanos = 5'000'000'000ull;  ///< Unreliable messages missing a fragment this long are dropped.
    usize max_connections = 1024;                     ///< Connections at once, both kinds.
    usize max_connections_per_address = 8;            ///< Accepted connections from one address.
    u64 connect_timeout_nanos = 5'000'000'000ull;     ///< Time to complete a handshake.
    u64 timeout_nanos = 10'000'000'000ull;            ///< Silence after which a connection times out.
    u64 max_bandwidth_bytes_per_second = 0;      ///< Cap on the send rate of one connection; 0 for none (the congestion
                                                 ///< window still applies).
    u64 keepalive_nanos = 1'000'000'000ull;      ///< Silence after which an empty packet is sent.
    u64 ack_delay_nanos = 20'000'000ull;         ///< How long an acknowledgement waits for a packet to ride on.
    u64 handshake_retry_nanos = 200'000'000ull;  ///< First delay before a handshake packet is repeated (doubles, up to
                                                 ///< 1 s).
    u64 cookie_period_nanos = 10'000'000'000ull;  ///< Length of a cookie's time period; a cookie lives two.
    u32 close_repeats = 3;                        ///< Times a Disconnect packet is sent.
    u64 close_interval_nanos = 50'000'000ull;     ///< Delay between them.
    u16 initial_sequence = 0;                     ///< First packet sequence number (tests start near 65535).
    RateLimiterConfig
        handshake_limits = DefaultHandshakeLimits();    ///< Rates of ConnectRequest packets, per address (an
                                                        ///< IPv6 /64 counts as one) and in total; excess is
                                                        ///< dropped silently. There are no strikes or bans
                                                        ///< (a source address may be forged until a cookie checks).
    usize max_total_receive_buffer_bytes = 256u << 20;  ///< Reassembly plus out-of-order bytes over all connections; a
                                                        ///< connection that would exceed it is ended with
                                                        ///< [[net::DisconnectReason::ProtocolError]].
    u64 seed = 0;  ///< Salts and cookie keys come from this seed if it is not zero (tests); otherwise from the system.
};

/// Counts of what the transport dropped or sent, for tests and diagnostics.
///
/// - Thread-safety: thread-compatible plain data.
struct UdpCounters
{
    u64 datagrams_received = 0;    ///< Datagrams taken from the port.
    u64 datagrams_sent = 0;        ///< Datagrams given to the port.
    u64 send_failures = 0;         ///< Datagrams the port refused.
    u64 dropped_short = 0;         ///< Shorter than their kind's header or not full-size where they must be.
    u64 dropped_oversize = 0;      ///< Longer than the MTU.
    u64 dropped_unknown = 0;       ///< Unknown version or kind.
    u64 dropped_bad_cookie = 0;    ///< A ChallengeResponse whose cookie did not check.
    u64 dropped_stale = 0;         ///< Replayed handshake packets and payloads too old to judge.
    u64 dropped_duplicate = 0;     ///< Payload packets whose sequence was already received.
    u64 challenges_sent = 0;       ///< Challenge packets sent.
    u64 resets_sent = 0;           ///< Reset packets sent for payloads of unknown connections.
    u64 denied_sent = 0;           ///< Denied packets sent.
    u64 dropped_rate_limited = 0;  ///< Handshake packets dropped by the rate limits.
    u64 budget_closes = 0;         ///< Connections ended because the global receive budget was full.
};

/// What the transport knows of one connection's packet layer.
///
/// - Thread-safety: thread-compatible plain data.
struct UdpConnectionInfo
{
    bool connected = false;      ///< The handshake finished and the connection is open.
    u16 next_sequence = 0;       ///< The sequence number the next packet will carry.
    u16 latest_received = 0;     ///< The newest sequence received.
    bool any_received = false;   ///< Something was received.
    u64 packets_acked = 0;       ///< Packets of ours the peer acknowledged.
    u64 frames_resent = 0;       ///< Reliable frames sent again after a loss or a time-out.
    usize buffered_bytes = 0;    ///< Bytes held for reassembly and for out-of-order delivery.
    usize reassemblies = 0;      ///< Messages being reassembled.
    u64 window_cuts = 0;         ///< Times the congestion window was halved after losses.
    u64 timeouts_fired = 0;      ///< Times a time-out reset the window to its smallest size.
    u64 dropped_unreliable = 0;  ///< Queued unreliable frames discarded to make room (oldest first).
    u64 duplicates = 0;          ///< Payload packets dropped as duplicates.
    u64 too_old = 0;             ///< Payload packets dropped for being more than 32 behind.
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
    /// Queues a message; it is packed into a packet at the next [[net::UdpTransport::Poll]].
    /// - Param connection: the connection.
    /// - Param channel: the channel.
    /// - Param bytes: the message.
    /// - Param reliability: how to carry it.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]], [[net::ErrorCode::MessageTooLarge]], [[net::ErrorCode::Closed]],
    /// [[net::ErrorCode::LimitExceeded]] (the queue is full: retry later), [[net::ErrorCode::NotConnected]] while the
    /// handshake is under way.
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
    /// All four kinds are carried as asked.
    /// - Param reliability: the kind.
    /// - Returns: true.
    bool Supports(Reliability reliability) const override
    {
        return static_cast<u8>(reliability) <= static_cast<u8>(Reliability::ReliableUnordered);
    }
    /// The largest message.
    /// - Returns: [[net::UdpTransportConfig::max_message_bytes]].
    usize MaxMessageBytes() const override { return config_.max_message_bytes; }
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

    /// What the handshake rate limiter has allowed and refused.
    /// - Returns: its counters.
    const RateLimiterCounters& HandshakeLimiterCounters() const;

private:
    struct Impl;
    UdpTransportConfig config_;
    UniquePtr<Impl> impl_;
};

}  // namespace engine_placeholder::net
