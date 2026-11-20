#pragma once

//! [[net::SimNetwork]]: a deterministic in-process packet network for tests, with impairments per direction, driven by
//! a clock that moves only when the test says so.
//!
//! Packets go from one [[net::Endpoint]] to another through a link that can add latency and jitter, drop packets
//! (uniformly or in bursts), reorder, duplicate and corrupt them, cap the bandwidth with a bounded queue, and refuse
//! packets above an MTU. Every random choice comes from one seeded generator, and packets in flight are ordered by
//! (delivery time, order of sending), so the same seed and the same calls give the same packet trace byte for byte.
//!
//! Time moves in whole ticks of one millisecond: [[net::SimNetwork::Advance]] delivers the packets that are due and
//! calls [[net::PacketSink::OnTick]] on every attached sink, tick by tick.

#include "bytes/byte_writer.h"
#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "net/clock.h"
#include "net/endpoint.h"
#include "net/status.h"
#include "types.h"

namespace vse::net
{

/// What a link does to the packets that cross it (one direction).
///
/// - Thread-safety: thread-compatible plain data.
struct LinkConfig
{
    u64 latency_nanos = 0;  ///< Fixed delay of every packet.
    u64 jitter_nanos = 0;   ///< Extra delay, uniform between 0 and this.
    f64 loss = 0;           ///< Probability that a packet is dropped, 0 to 1.
    f64 burst_start = 0;    ///< Probability that a packet starts a burst of drops.
    u32 burst_length = 0;   ///< Packets dropped in a burst, the first included.
    f64 reorder = 0;        ///< Probability that a packet is delayed by `reorder_delay_nanos`, so later ones pass.
    u64 reorder_delay_nanos = 0;  ///< The delay of a reordered packet; 0 for three times the latency plus 1 ms.
    f64 duplicate = 0;            ///< Probability that a packet arrives twice.
    f64 corrupt = 0;              ///< Probability that one bit of a packet is flipped.
    u64 bandwidth_bps = 0;        ///< Bits per second the link carries; 0 for no limit.
    usize queue_bytes = 0;        ///< With a bandwidth limit: the bytes that may wait; more are dropped. 0: unlimited.
    usize mtu = 1200;             ///< Packets above this size are dropped.
};

/// Receives the packets of one endpoint.
///
/// - Thread-safety: thread-compatible; its methods are called on the thread that calls [[net::SimNetwork::Advance]].
class PacketSink
{
public:
    virtual ~PacketSink() = default;

    /// A packet arrived (possibly corrupted, possibly a duplicate).
    /// - Param from: the sender.
    /// - Param packet: the bytes, valid during the call.
    virtual void OnPacket(const Endpoint& from, ByteSpan packet) = 0;

    /// Time moved forward by one tick; the clock already shows the new time.
    /// - Param now_nanos: the new time.
    virtual void OnTick(u64 now_nanos) = 0;

    /// A packet this sink sent could not be delivered because nobody is attached at the destination.
    virtual void OnUnreachable(const Endpoint&) {}
};

/// How many packets met each fate.
///
/// - Thread-safety: thread-compatible plain data.
struct SimCounters
{
    u64 sent = 0;           ///< Packets handed to the network.
    u64 delivered = 0;      ///< Packets delivered to a sink (duplicates counted each time).
    u64 dropped_loss = 0;   ///< Dropped by loss or a burst.
    u64 dropped_mtu = 0;    ///< Dropped for being above the MTU.
    u64 dropped_queue = 0;  ///< Dropped by a full bandwidth queue.
    u64 duplicated = 0;     ///< Extra copies made.
    u64 reordered = 0;      ///< Packets given the reorder delay.
    u64 corrupted = 0;      ///< Packets with a flipped bit.
    u64 unreachable = 0;    ///< Packets whose destination had no sink.
};

/// The network.
///
/// - Thread-safety: thread-compatible; one thread drives it and everything attached to it.
/// - Ownership: borrows the sinks, which must [[net::SimNetwork::Detach]] before they are destroyed.
class SimNetwork
{
public:
    /// The length of one tick.
    static constexpr u64 kTickNanos = 1'000'000;

    /// Creates a network whose random choices come from `seed`.
    /// - Param seed: the seed; the same seed gives the same behavior.
    explicit SimNetwork(u64 seed);

    SimNetwork(const SimNetwork&) = delete;
    SimNetwork& operator=(const SimNetwork&) = delete;

    /// Sets the link used where no link of its own was set.
    /// - Param config: the impairments.
    void SetDefaultLink(const LinkConfig& config) { default_link_ = config; }

    /// Sets the link for one direction.
    /// - Param from: the sender's endpoint.
    /// - Param to: the receiver's endpoint.
    /// - Param config: the impairments.
    void SetLink(const Endpoint& from, const Endpoint& to, const LinkConfig& config);

    /// Attaches a sink at an endpoint.
    /// - Param at: the endpoint.
    /// - Param sink: receives packets and ticks.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::AddressInUse]] if something is attached there.
    /// - Ownership: borrows `sink`, which must stay alive until [[net::SimNetwork::Detach]].
    Status Attach(const Endpoint& at, PacketSink* sink);

    /// Detaches the sink at an endpoint; packets still in flight to it become unreachable.
    /// - Param at: the endpoint.
    void Detach(const Endpoint& at);

    /// A number from the network's generator, for a sink that needs an identifier that is the same on every replay
    /// of a seed (a connection nonce).
    /// - Returns: 64 random bits.
    u64 NewNonce() { return Next(); }

    /// An endpoint on the loopback address with a port nobody has taken, for a sink that does not care.
    /// - Returns: a fresh endpoint.
    Endpoint AllocateEndpoint();

    /// Sends a packet through the link from `from` to `to`.
    /// - Param from: the sender's endpoint.
    /// - Param to: the receiver's endpoint.
    /// - Param packet: the bytes; copied.
    void Transmit(const Endpoint& from, const Endpoint& to, ByteSpan packet);

    /// Moves time forward by at least `nanos` (a whole number of ticks), delivering what is due.
    /// - Param nanos: how far.
    void Advance(u64 nanos);

    /// The current time.
    /// - Returns: nanoseconds since the network was created plus the clock's origin.
    u64 NowNanos() const { return clock_.NowNanos(); }

    /// The clock that follows the network's time.
    /// - Returns: the clock.
    const Clock& GetClock() const { return clock_; }

    /// The tally so far.
    /// - Returns: the counters.
    const SimCounters& Counters() const { return counters_; }

    /// Starts recording a trace of every drop and delivery.
    void EnableTrace() { tracing_ = true; }

    /// The trace: for each event a kind byte, the time, the two ports, the length and a checksum of the bytes.
    /// - Returns: the recorded bytes.
    ByteSpan Trace() const { return trace_.Bytes(); }

private:
    struct InFlight
    {
        u64 deliver = 0;
        u64 order = 0;
        Endpoint from;
        Endpoint to;
        Vector<byte> data;
    };
    struct LinkState
    {
        Endpoint from;
        Endpoint to;
        bool has_config = false;
        LinkConfig config;
        u64 free_at = 0;  // when the link finishes sending what is queued
        u32 burst_left = 0;
    };
    struct Attached
    {
        Endpoint at;
        PacketSink* sink = nullptr;
    };

    LinkState& LinkFor(const Endpoint& from, const Endpoint& to);
    f64 Uniform();
    u64 Next();
    void Push(u64 deliver, const Endpoint& from, const Endpoint& to, const Vector<byte>& data);
    void Trace(u8 kind, const Endpoint& from, const Endpoint& to, ByteSpan data);
    void DeliverDue();
    void SiftUp(usize index);
    void SiftDown(usize index);
    bool Before(const UniquePtr<InFlight>& a, const UniquePtr<InFlight>& b) const;

    ManualClock clock_;
    LinkConfig default_link_;
    Vector<LinkState> links_;
    Vector<Attached> attached_;
    Vector<Attached> tick_sinks_;  // scratch for Advance, kept to avoid an allocation per tick
    Vector<UniquePtr<InFlight>> heap_;
    u64 state_;
    u64 order_ = 0;
    u16 next_port_ = 20000;
    SimCounters counters_;
    bool tracing_ = false;
    bytes::ByteWriter trace_;
};

}  // namespace vse::net
