#pragma once

//! [[net::TcpTransport]]: the [[net::Transport]] interface over byte streams (Phase 20 Day 277), for what needs
//! reliable ordered delivery: a store over the network, tools, remote debugging.
//!
//! **Wire.** Everything is a frame of [[net::FrameWriter]] (a 4-byte big-endian length, then the payload). The first
//! byte of a payload is its type: `Hello` (protocol version, channel count, largest message), `Data` (channel,
//! reliability kind, message), `Close` (code), `Ping` and `Pong` (a time stamp for the round trip). Each side sends its
//! `Hello` first (the server answers only after it has read a valid client `Hello`, so a stranger that speaks something
//! else gets nothing back), and a frame of any other type before the peer's `Hello` is a protocol violation. The
//! channel count and message size in force are the smaller of the two sides'.
//!
//! **Reliability kinds.** A stream is reliable and ordered, so [[net::Reliability::ReliableOrdered]] and
//! [[net::Reliability::ReliableUnordered]] are carried as asked (the unordered kind arrives in order, which it
//! permits). The two unreliable kinds degrade: [[net::TcpTransport::Supports]] says false and
//! [[net::TcpTransport::DegradesUnreliable]] true, and such a message is sent as reliable ordered (and arrives with
//! that kind). Channels are labels on one stream, so a large message on one channel delays the others (head-of-line
//! blocking is what TCP is).
//!
//! **Limits.** A frame longer than the message limit plus its header is refused before any buffer is sized for it; the
//! send queue is bounded ([[net::ErrorCode::LimitExceeded]] is backpressure); a stream that does not finish the
//! handshake in time, stays silent for the time-out, or breaks the protocol is closed; connections over the cap are
//! closed on arrival.
//!
//! **Time and I/O.** The transport reads a [[net::Clock]] and moves bytes through a [[net::StreamProvider]], so it runs
//! unchanged over real sockets ([[net::SocketStreamProvider]]) and over the simulated streams
//! ([[net::SimStreamNetwork]]). It is polled: [[net::TcpTransport::Poll]] accepts, reads, writes, runs the timers and
//! returns events.

#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "mem/heap_allocator.h"
#include "net/clock.h"
#include "net/rate_limit.h"
#include "net/stream.h"
#include "net/transport.h"
#include "types.h"

namespace vse::net
{

/// The default limits on accepted streams: 100 per second per address (burst 100) and 5,000 per second in total
/// (burst 1,000); three failed handshakes within a minute ban an address for five minutes.
/// - Returns: the limits.
inline RateLimiterConfig DefaultAcceptLimits()
{
    RateLimiterConfig c;
    c.total_per_second = 5000;
    c.total_burst = 1000;
    c.address_per_second = 100;
    c.address_burst = 100;
    return c;
}

/// Limits and times of a [[net::TcpTransport]].
///
/// - Thread-safety: thread-compatible plain data.
struct TcpTransportConfig
{
    u8 channels = 8;                                 ///< Channels per connection offered in the handshake.
    usize max_message_bytes = 1u << 20;              ///< Largest message offered in the handshake.
    usize max_connections = 1024;                    ///< Connections at once, both kinds; more are closed on arrival.
    usize max_send_queue_bytes = 8u << 20;           ///< Bytes waiting to be written per connection.
    u64 handshake_timeout_nanos = 5'000'000'000ull;  ///< Time to connect and exchange `Hello`.
    u64 timeout_nanos = 10'000'000'000ull;           ///< Silence after which an open connection times out.
    u64 ping_interval_nanos = 1'000'000'000ull;      ///< Delay between pings (round-trip samples and keep-alive).
    u64 close_linger_nanos = 2'000'000'000ull;       ///< Time a closed connection may take to flush its last bytes.
    usize read_bytes_per_poll = 64 * 1024;           ///< Bytes read per connection per pass (fairness).
    usize max_connections_per_address = 256;         ///< Connections at once from one address (an IPv6 /64 counts as
                                                     ///< one); more are closed on arrival.
    usize max_total_send_queue_bytes = 256u << 20;   ///< Bytes waiting to be written over all connections; `Send`
                                                     ///< returns [[net::ErrorCode::LimitExceeded]] above it.
    RateLimiterConfig accept_limits = DefaultAcceptLimits();  ///< Rates of accepted streams, per address and in total;
                                                              ///< a stream over the limit is closed on arrival. A
                                                              ///< stream that breaks the protocol before its handshake
                                                              ///< is done is a strike; enough strikes ban the address.
};

/// A transport over byte streams.
///
/// - Thread-safety: thread-compatible; the thread that polls it uses it.
/// - Ownership: borrows the provider and the clock, which must outlive it.
class TcpTransport final : public Transport
{
public:
    /// Creates a transport on a provider.
    /// - Param provider: where streams come from.
    /// - Param clock: the time source.
    /// - Param config: limits and times.
    TcpTransport(StreamProvider& provider, const Clock& clock, const TcpTransportConfig& config = {});

    /// Closes every stream silently; peers see the end of the stream.
    ~TcpTransport() override;

    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;

    /// Starts accepting connections.
    /// - Param at: the endpoint to listen at.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]] if already listening, otherwise the provider's error.
    Status Listen(const Endpoint& at) override;
    /// Starts connecting.
    /// - Param to: the server.
    /// - Param connection: receives the id.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::LimitExceeded]], the provider's error.
    /// - Ownership: `*connection` is written on success.
    Status Connect(const Endpoint& to, ConnectionId* connection) override;
    /// Queues a message; it is written at the next [[net::TcpTransport::Poll]] (and as the stream accepts it).
    /// - Param connection: the connection.
    /// - Param channel: the channel.
    /// - Param bytes: the message.
    /// - Param reliability: how to carry it (unreliable kinds are sent reliably).
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]], [[net::ErrorCode::MessageTooLarge]], [[net::ErrorCode::Closed]],
    /// [[net::ErrorCode::NotConnected]] before the handshake is done, [[net::ErrorCode::LimitExceeded]] (the queue is
    /// full: retry later).
    Status Send(ConnectionId connection, u8 channel, ByteSpan bytes, Reliability reliability) override;
    /// Accepts, reads, writes, runs the timers and returns the next event.
    /// - Param event: receives the event.
    /// - Returns: false when there is none.
    /// - Ownership: `*event` is written when true is returned; its message view lasts until the next call.
    bool Poll(Event* event) override;
    /// Ends a connection; the peer is told by a `Close` frame, flushed for up to the linger time.
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
    /// Only the reliable kinds are carried as asked.
    /// - Param reliability: the kind.
    /// - Returns: true for the two reliable kinds.
    bool Supports(Reliability reliability) const override
    {
        return reliability == Reliability::ReliableOrdered || reliability == Reliability::ReliableUnordered;
    }
    /// The unreliable kinds are sent reliably.
    /// - Returns: true.
    bool DegradesUnreliable() const override { return true; }
    /// The largest message this side offers.
    /// - Returns: [[net::TcpTransportConfig::max_message_bytes]].
    usize MaxMessageBytes() const override { return config_.max_message_bytes; }
    /// The number of channels this side offers.
    /// - Returns: [[net::TcpTransportConfig::channels]].
    u8 ChannelCount() const override { return config_.channels; }

    /// The number of connections held, closed ones not yet reported included.
    /// - Returns: the count.
    usize ConnectionCount() const;

    /// What the accept rate limiter has allowed and refused.
    /// - Returns: its counters.
    const RateLimiterCounters& AcceptLimiterCounters() const;

private:
    struct Impl;
    TcpTransportConfig config_;
    UniquePtr<Impl> impl_;
};

}  // namespace vse::net
