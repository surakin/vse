#pragma once

//! The wire format and the small algorithms of the UDP transport (Phase 20 Day 274): packet kinds and headers,
//! wraparound-safe sequence numbers, the acknowledgement window, and the keyed hash behind the handshake cookie.
//! Everything here is a pure function or a small value type, tested without a network.
//!
//! **Format.** One datagram is one packet. All integers are little-endian. The first byte is the kind in its low three
//! bits, a flag (0x08, payloads only: the acknowledgement fields are valid), a reserved bit that must be zero (0x10),
//! and the protocol version in its high three (anything else is dropped).
//!
//! ```
//! ConnectRequest     [kind][u64 client_salt][zero padding to 1200 bytes]
//! Challenge          [kind][u64 client_salt][u64 cookie]
//! ChallengeResponse  [kind][u64 client_salt][u64 cookie][zero padding to 1200 bytes]
//! Accepted           [kind][u64 client_salt][u16 connection_id]
//! Denied             [kind][u64 client_salt][u8 reason]
//! Payload            [kind][u16 connection_id][u16 sequence][u16 ack][u32 ack_bits][frames...]
//! Disconnect         [kind][u16 connection_id][u32 code]
//!                    (a client that gives up during the handshake sends connection_id 0 and appends [u64 client_salt])
//! Reset              [kind][u16 connection_id]
//! ```
//!
//! The two requests that a stranger can send are padded to the full size, and every answer to a stranger is far smaller
//! than the request, so the server cannot be used to amplify traffic towards a forged source address.

#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace engine_placeholder::net::udp
{

/// The protocol version carried in every packet.
constexpr u8 kVersion = 1;

/// The largest datagram the transport sends (the IPv6 minimum MTU less the headers, rounded down).
constexpr usize kMtu = 1200;

/// The size the handshake requests are padded to.
constexpr usize kHandshakeBytes = 1200;

/// The bytes of a payload packet's header, after the kind byte included.
constexpr usize kPayloadHeaderBytes = 1 + 2 + 2 + 2 + 4;

/// The bytes of the largest payload carried by one packet.
constexpr usize kMaxPayloadBytes = kMtu - kPayloadHeaderBytes;

/// What a packet is.
enum class PacketKind : u8
{
    ConnectRequest = 0,     ///< Client to server: starts a handshake.
    Challenge = 1,          ///< Server to client: the cookie.
    ChallengeResponse = 2,  ///< Client to server: the cookie returned.
    Accepted = 3,           ///< Server to client: the connection exists.
    Denied = 4,             ///< Server to client: the connection is refused.
    Payload = 5,            ///< Either way: acknowledged packets carrying messages.
    Disconnect = 6,         ///< Either way: the connection is closed.
    Reset = 7,              ///< Server to client: the connection is unknown.
};

/// Why a server refused a connection.
enum class DenyReason : u8
{
    ServerFull = 1,    ///< The connection table is full.
    AddressLimit = 2,  ///< This address already has its share of connections.
    NotListening = 3,  ///< The endpoint does not accept connections.
};

/// Builds the first byte of a packet.
/// - Param kind: the kind.
/// - Param has_ack: for a payload: the acknowledgement fields mean something (a packet was received).
/// - Returns: kind, flag and version.
/// - Fails: never.
u8 FirstByte(PacketKind kind, bool has_ack = false);

/// Reads the first byte of a packet.
/// - Param first: the byte.
/// - Param kind: receives the kind.
/// - Param has_ack: receives the acknowledgement flag (false for every kind but a payload).
/// - Returns: false if the version is not ours, the kind is unknown, or a flag is set that the kind does not use.
/// - Ownership: the outputs are written when true is returned.
bool ParseFirstByte(u8 first, PacketKind* kind, bool* has_ack = nullptr);

/// SipHash-2-4 (Aumasson and Bernstein, 2012) of `data` under a 128-bit key. A keyed hash for the handshake cookie so a
/// stranger cannot forge one without the key; it is **not** an authenticated encryption, and the cookie only stops
/// spoofed-source floods (see the guide's trust note).
/// - Param key0: the first half of the key.
/// - Param key1: the second half of the key.
/// - Param data: the bytes.
/// - Returns: the 64-bit hash.
/// - Fails: never.
u64 SipHash24(u64 key0, u64 key1, ByteSpan data);

/// True if sequence `a` is newer than `b` on the 16-bit circle (wraparound-safe).
/// - Param a: a sequence number.
/// - Param b: a sequence number.
/// - Returns: whether `a` is after `b`.
/// - Fails: never.
bool SeqNewer(u16 a, u16 b);

/// The distance from `older` to `newer` on the circle, 0 to 65535.
/// - Param newer: a sequence number.
/// - Param older: a sequence number.
/// - Returns: `newer - older` modulo 65536.
/// - Fails: never.
u16 SeqDistance(u16 newer, u16 older);

/// What the receiver made of a packet's sequence number.
enum class SeqVerdict : u8
{
    Fresh,      ///< Not seen before; recorded.
    Duplicate,  ///< Seen before.
    TooOld,     ///< More than 32 packets behind the newest: cannot be told from a duplicate, dropped.
};

/// Tracks which packet sequence numbers arrived, to detect duplicates and to build the acknowledgement of the next
/// packet sent: the newest sequence plus a bit set for each of the 32 before it.
///
/// - Thread-safety: not-thread-safe.
class ReceiveWindow
{
public:
    /// Records a packet.
    /// - Param seq: its sequence number.
    /// - Returns: whether it is new.
    SeqVerdict OnReceive(u16 seq);

    /// The newest sequence received; meaningful once [[net::udp::ReceiveWindow::Any]].
    /// - Returns: the sequence.
    u16 Latest() const { return latest_; }

    /// Bit `i` set: sequence `Latest() - 1 - i` was received.
    /// - Returns: the bits.
    u32 Bits() const { return bits_; }

    /// True once a packet was recorded.
    /// - Returns: whether anything arrived.
    bool Any() const { return any_; }

private:
    bool any_ = false;
    u16 latest_ = 0;
    u32 bits_ = 0;
};

/// Remembers the packets sent, to turn the acknowledgements of the peer into the list of packets that arrived.
///
/// - Thread-safety: not-thread-safe.
class SendLog
{
public:
    /// The number of packets remembered.
    static constexpr u16 kCapacity = 256;

    /// Notes that packet `seq` was sent. Sequences must be given in order, each once.
    /// - Param seq: the sequence number.
    void OnSent(u16 seq);

    /// Applies an acknowledgement: `ack` and the 32 packets before it that `bits` marks.
    /// - Param ack: the newest sequence the peer has seen.
    /// - Param bits: bit `i` set: `ack - 1 - i` was seen.
    /// - Param newly: appended with each sequence this call acknowledged for the first time; may be null.
    /// - Returns: how many packets were newly acknowledged.
    /// - Ownership: `newly` is appended to, never cleared.
    u32 OnAck(u16 ack, u32 bits, Vector<u16>* newly);

private:
    struct Entry
    {
        u16 seq = 0;
        bool used = false;
        bool acked = false;
    };
    Entry entries_[kCapacity];
};

/// The fields of a payload packet's header.
///
/// - Thread-safety: thread-compatible plain data.
struct PayloadHeader
{
    u16 connection = 0;    ///< The server's id for the connection.
    u16 sequence = 0;      ///< This packet's sequence number.
    u16 ack = 0;           ///< The newest sequence the sender has received.
    u32 ack_bits = 0;      ///< The 32 before it, as in [[net::udp::ReceiveWindow::Bits]].
    bool has_ack = false;  ///< False until the sender has received a packet: `ack` and `ack_bits` mean nothing.
};

/// Appends a payload header (kind byte included).
/// - Param header: the fields.
/// - Param out: appended to.
/// - Ownership: `out` is appended to.
void WritePayloadHeader(const PayloadHeader& header, Vector<byte>* out);

/// Reads a payload header.
/// - Param packet: the whole packet, kind byte included.
/// - Param header: receives the fields.
/// - Param body: receives the bytes after the header.
/// - Returns: false if the packet is shorter than the header.
/// - Ownership: the outputs are written when true is returned; `body` views `packet`.
bool ReadPayloadHeader(ByteSpan packet, PayloadHeader* header, ByteSpan* body);

}  // namespace engine_placeholder::net::udp
