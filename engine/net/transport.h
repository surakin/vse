#pragma once

//! [[net::Transport]]: the interface everything above networking uses to exchange messages with peers, whatever carries
//! them (the in-process [[net::SimulatedTransport]], the UDP transport of Days 274-276, the TCP transport of Day 277).
//!
//! A transport is **polled**: the application calls [[net::Transport::Poll]] once or more per frame and receives
//! [[net::Event]]s (a connection opened, a message arrived, a connection closed with its reason); nothing calls back
//! into the application and no thread of the application's is blocked. A message is delivered whole, or not at all, and
//! never corrupted or duplicated into delivery twice. What survives loss depends on the [[net::Reliability]] the sender
//! chose, per message, on one of a fixed number of channels per connection. The channels are independent: a lost
//! reliable message on one channel does not hold back the messages of another.
//!
//! **Buffers.** The bytes of a received message are a view valid until the next call to [[net::Transport::Poll]] on the
//! same transport; copy what you keep. [[net::Transport::Send]] copies the bytes it is given.
//!
//! **Closing.** After [[net::Transport::Close]], or after a [[net::EventKind::Closed]] event, a connection delivers no
//! more events and refuses sends; its id is never reused by that transport.

#include "containers/span.h"
#include "net/endpoint.h"
#include "net/status.h"
#include "types.h"

namespace engine_placeholder::net
{

/// How a message is carried.
enum class Reliability : u8
{
    Unreliable,           ///< May be lost, reordered; delivered at most once.
    UnreliableSequenced,  ///< May be lost; a message older than one already delivered on the channel is dropped.
    ReliableOrdered,      ///< Arrives exactly once, in the order sent on its channel (until the connection ends).
    ReliableUnordered,    ///< Arrives exactly once, in any order.
};

/// The name of a reliability, for logs and tests.
/// - Param reliability: the value.
/// - Ownership: the string is static.
/// - Fails: never.
const char* ReliabilityName(Reliability reliability);

/// Identifies a connection within one transport. Zero is never a valid id.
using ConnectionId = u32;

/// Why a connection ended.
enum class DisconnectReason : u8
{
    None,           ///< Not a reason: used for events that are not closes.
    LocalClose,     ///< The application called [[net::Transport::Close]].
    PeerClosed,     ///< The peer closed it (its application reason is in the event).
    TimedOut,       ///< Nothing was heard from the peer for the timeout, or the connect did not complete.
    Refused,        ///< Nobody accepted the connection (no listener, or it was full).
    Reset,          ///< The connection broke (a stream reset).
    ProtocolError,  ///< The peer broke the protocol.
};

/// The name of a disconnect reason, for logs and tests.
/// - Param reason: the value.
/// - Ownership: the string is static.
/// - Fails: never.
const char* DisconnectReasonName(DisconnectReason reason);

/// What happened.
enum class EventKind : u8
{
    Opened,   ///< A connection is established: one made by [[net::Transport::Connect]] or one a listener accepted.
    Message,  ///< A message arrived.
    Closed,   ///< The connection ended; this is its last event.
};

/// One thing that happened on a transport.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `message` points into the transport and is valid until the next [[net::Transport::Poll]].
struct Event
{
    EventKind kind = EventKind::Opened;  ///< What happened.
    ConnectionId connection = 0;         ///< The connection the event belongs to.
    bool incoming = false;               ///< For Opened: the peer connected to us (true) or we connected to it (false).
    Endpoint peer;                       ///< For Opened: the peer's endpoint.
    u8 channel = 0;                      ///< For Message: the channel it was sent on.
    Reliability reliability = Reliability::Unreliable;  ///< For Message: how it was sent.
    ByteSpan message;                                   ///< For Message: the bytes.
    DisconnectReason reason = DisconnectReason::None;   ///< For Closed: why.
    u32 close_code = 0;  ///< For Closed with PeerClosed: the code the peer gave to Close; otherwise 0.
};

/// What a connection has done, for the application and for tests.
///
/// - Thread-safety: thread-compatible plain data.
struct ConnectionStats
{
    f64 rtt_seconds = 0;            ///< Smoothed round-trip time; 0 until measured.
    f64 loss = 0;                   ///< Estimated fraction of sent packets that needed to be sent again, 0 to 1.
    u64 bytes_sent = 0;             ///< Payload bytes handed to the connection by Send.
    u64 bytes_received = 0;         ///< Payload bytes delivered as messages.
    u64 messages_sent = 0;          ///< Messages accepted by Send.
    u64 messages_received = 0;      ///< Messages delivered.
    u64 packets_sent = 0;           ///< Packets (or segments) put on the wire, retransmissions included.
    u64 packets_received = 0;       ///< Packets that arrived and were valid.
    usize send_queue_messages = 0;  ///< Reliable messages not yet acknowledged, or waiting to be sent.
    usize send_queue_bytes = 0;     ///< Their payload bytes.
};

/// A source and sink of messages between this process and its peers.
///
/// - Thread-safety: thread-compatible: one thread uses a transport at a time (the one that polls it). An
/// implementation that runs its own threads synchronizes internally.
/// - Ownership: the transport owns its connections. Destroying it with traffic in flight is safe and silent: nothing is
/// delivered afterwards.
class Transport
{
public:
    virtual ~Transport() = default;

    /// Starts accepting connections at `at`.
    /// - Param at: where to listen.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::AddressInUse]], [[net::ErrorCode::Invalid]] if already listening,
    /// [[net::ErrorCode::Unsupported]], or the failure of the carrier.
    virtual Status Listen(const Endpoint& at) = 0;

    /// Starts a connection to `to`. The connection is usable for [[net::Transport::Send]] at once (messages wait until
    /// it opens); the outcome arrives as an [[net::EventKind::Opened]] or a [[net::EventKind::Closed]] event.
    /// - Param to: the peer.
    /// - Param connection: receives the connection's id.
    /// - Returns: success (the attempt was started).
    /// - Fails: [[net::ErrorCode::LimitExceeded]] (too many connections), [[net::ErrorCode::Invalid]].
    /// - Ownership: `*connection` is written on success.
    virtual Status Connect(const Endpoint& to, ConnectionId* connection) = 0;

    /// Queues a message for a connection.
    /// - Param connection: the connection.
    /// - Param channel: 0 to [[net::Transport::ChannelCount]] minus one.
    /// - Param bytes: the message, 1 to [[net::Transport::MaxMessageBytes]] bytes; copied.
    /// - Param reliability: how to carry it; see [[net::Transport::Supports]].
    /// - Returns: success (the message is queued, not delivered).
    /// - Fails: [[net::ErrorCode::Invalid]] (unknown connection, channel out of range, empty message),
    /// [[net::ErrorCode::MessageTooLarge]], [[net::ErrorCode::Closed]] (the connection ended or is closing),
    /// [[net::ErrorCode::Unsupported]] (the reliability is not supported), [[net::ErrorCode::LimitExceeded]] (the
    /// send queue is full: retry later).
    virtual Status Send(ConnectionId connection, u8 channel, ByteSpan bytes, Reliability reliability) = 0;

    /// Takes the next event, if there is one. Never blocks. Also lets the transport make progress (a transport that is
    /// not driven by threads of its own does its work here or in its own update call).
    /// - Param event: receives the event; its message view is valid until the next call.
    /// - Returns: false when there is no event.
    /// - Ownership: `*event` is written when true is returned.
    virtual bool Poll(Event* event) = 0;

    /// Ends a connection. Messages not yet delivered to the peer may be lost. A [[net::EventKind::Closed]] event with
    /// [[net::DisconnectReason::LocalClose]] is queued, and the peer gets `code`.
    /// - Param connection: the connection.
    /// - Param code: an application value sent to the peer in its Closed event.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]] for an unknown connection, [[net::ErrorCode::Closed]] if it already ended.
    virtual Status Close(ConnectionId connection, u32 code) = 0;

    /// Reads a connection's counters.
    /// - Param connection: the connection.
    /// - Param stats: receives the counters.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]] for an unknown connection (one that has ended is still known until its
    /// Closed event was polled).
    /// - Ownership: `*stats` is written on success.
    virtual Status Statistics(ConnectionId connection, ConnectionStats* stats) const = 0;

    /// Tells whether `reliability` is carried as asked. A transport that cannot (a stream carries everything
    /// reliably) says false for the kinds it would degrade; [[net::Transport::Send]] then fails with `Unsupported`
    /// unless [[net::Transport::DegradesUnreliable]] is true.
    /// - Param reliability: the kind.
    /// - Returns: true if supported as asked.
    virtual bool Supports(Reliability reliability) const = 0;

    /// Tells whether an unsupported unreliable kind is sent reliably instead (documented per transport) instead of
    /// failing.
    /// - Returns: true for a stream transport.
    virtual bool DegradesUnreliable() const { return false; }

    /// The largest message [[net::Transport::Send]] accepts.
    /// - Returns: the size in bytes.
    virtual usize MaxMessageBytes() const = 0;

    /// The number of channels per connection.
    /// - Returns: the count (channels are numbered from 0).
    virtual u8 ChannelCount() const = 0;
};

}  // namespace engine_placeholder::net
