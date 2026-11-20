#pragma once

//! Length-prefixed framing for byte streams: [[net::FrameWriter]] and [[net::FrameReader]].
//!
//! A frame is a 4-byte big-endian length followed by that many payload bytes. A frame carries at least one byte and at
//! most `max_frame_bytes`; the limit is checked on the length, before any buffer is sized for the payload, so a hostile
//! peer cannot make the reader allocate by announcing a large frame (the payload buffer grows only with bytes that
//! really arrived). The reader accepts the stream cut at any place, down to one byte at a time.

#include "containers/span.h"
#include "containers/vector.h"
#include "net/status.h"
#include "types.h"

namespace vse::net
{

/// The size of the length prefix of a frame.
inline constexpr usize kFrameHeaderBytes = 4;

/// Encodes frames.
///
/// - Thread-safety: thread-safe (it holds only the limit).
class FrameWriter
{
public:
    /// Creates a writer for frames of at most `max_frame_bytes`.
    /// - Param max_frame_bytes: the largest payload; capped at 2^31 - 1.
    explicit FrameWriter(usize max_frame_bytes) : max_(max_frame_bytes > 0x7fffffff ? 0x7fffffff : max_frame_bytes) {}

    /// The largest payload this writer accepts.
    /// - Returns: the limit in bytes.
    usize MaxFrameBytes() const { return max_; }

    /// Appends the header and the payload of one frame to `out`.
    /// - Param payload: the frame's bytes; must not be empty.
    /// - Param out: receives the frame at its end; unchanged on failure.
    /// - Ownership: `*out` is appended to during the call.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]] for an empty payload, [[net::ErrorCode::LimitExceeded]] above the limit,
    /// [[net::ErrorCode::Other]] if memory ran out.
    Status Encode(ByteSpan payload, Vector<byte>* out) const;

    /// Writes the header of a frame of `size` bytes.
    /// - Param size: the payload size.
    /// - Param header: receives the 4 bytes.
    /// - Returns: success.
    /// - Fails: [[net::ErrorCode::Invalid]] for 0, [[net::ErrorCode::LimitExceeded]] above the limit.
    Status Header(usize size, byte (&header)[kFrameHeaderBytes]) const;

private:
    usize max_;
};

/// Decodes frames from a stream that arrives in pieces of any size.
///
/// Feed bytes with [[net::FrameReader::Consume]]; it stops as soon as one frame is complete, so the bytes after it
/// stay with the caller. A bad length (zero, or above the limit) fails for good: the same failure is returned until
/// the reader is [[net::FrameReader::Reset]].
///
/// - Thread-safety: thread-compatible; one thread feeds it.
/// - Ownership: owns the payload buffer of the frame in progress; [[net::FrameReader::Frame]] points into it.
class FrameReader
{
public:
    /// Creates a reader for frames of at most `max_frame_bytes`.
    /// - Param max_frame_bytes: the largest payload; capped at 2^31 - 1.
    explicit FrameReader(usize max_frame_bytes) : max_(max_frame_bytes > 0x7fffffff ? 0x7fffffff : max_frame_bytes) {}

    /// Takes bytes from the front of `bytes` until one frame is complete or the bytes run out.
    /// - Param bytes: what arrived.
    /// - Param used: receives how many bytes were taken (all of them unless a frame completed or it failed). A
    /// completed frame must be taken with [[net::FrameReader::Pop]] before more bytes are consumed.
    /// - Returns: success, or the sticky failure.
    /// - Fails: [[net::ErrorCode::Invalid]] for a zero length, [[net::ErrorCode::LimitExceeded]] for a length above
    /// the limit, [[net::ErrorCode::Other]] if memory ran out. After a failure `used` counts the bytes up to and
    /// including the bad header.
    /// - Ownership: `*used` is written during the call.
    Status Consume(ByteSpan bytes, usize* used);

    /// Tells whether a frame is complete and waiting to be taken.
    /// - Returns: true after the call to [[net::FrameReader::Consume]] that finished a frame, until
    /// [[net::FrameReader::Pop]].
    bool HasFrame() const { return complete_; }

    /// The payload of the complete frame.
    /// - Returns: the bytes; valid until [[net::FrameReader::Pop]], [[net::FrameReader::Reset]] or the next
    /// [[net::FrameReader::Consume]]; empty when no frame is complete.
    ByteSpan Frame() const { return complete_ ? ByteSpan(payload_.Data(), payload_.Size()) : ByteSpan(); }

    /// Releases the complete frame so the next one can be read.
    void Pop();

    /// Tells whether part of a frame (a header or a payload) is buffered: the stream ending now ends in the middle of
    /// a frame.
    /// - Returns: true between the first byte of a frame and its completion.
    bool InFrame() const { return !complete_ && (header_have_ > 0 || reading_payload_); }

    /// The bytes held for the frame in progress (the header bytes seen plus the payload bytes).
    /// - Returns: the count; never more than the limit plus the header size.
    usize BufferedBytes() const { return header_have_ + payload_.Size(); }

    /// Forgets the frame in progress and any failure.
    void Reset();

    /// The largest payload this reader accepts.
    /// - Returns: the limit in bytes.
    usize MaxFrameBytes() const { return max_; }

private:
    usize max_;
    byte header_[kFrameHeaderBytes] = {};
    usize header_have_ = 0;
    bool reading_payload_ = false;
    usize expected_ = 0;
    Vector<byte> payload_;
    bool complete_ = false;
    Status failure_;
};

}  // namespace vse::net
