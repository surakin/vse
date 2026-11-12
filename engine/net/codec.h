#pragma once

//! The wire vocabulary of messages: [[net::NetWriter]] and [[net::NetReader]], a bit-packed encoding with ranged
//! integers, canonical varints, length-limited strings and byte arrays, and quantized floats, vectors, rotations and
//! angles, plus deltas against a baseline.
//!
//! **Layout.** Bits are packed from the least significant bit of byte 0 upward; a value's low bit comes first. A
//! fixed-width integer written at a byte boundary is therefore exactly the little-endian encoding of
//! [[bytes::ByteWriter]]. Nothing is padded unless [[net::NetWriter::AlignToByte]] is called, which writes zero bits
//! that the reader requires to be zero (one valid encoding of every message).
//!
//! **Errors are values** ([[net::CodecError]]). A failed write or read changes nothing: the position stays where it
//! was and the reader remains usable. Every length and count has a mandatory maximum; nothing is allocated for a
//! length before it is checked against both the maximum and the bytes that remain.
//!
//! **Quantization is deterministic**: the arithmetic is done in `f64` with every intermediate product stored before it
//! is used again, so no compiler can fuse a multiply and an add differently on another platform.

#include "containers/span.h"
#include "containers/vector.h"
#include "math/quaternion.h"
#include "mem/heap_allocator.h"
#include "math/vector.h"
#include "string/string.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::net
{

/// Why a write or a read failed.
enum class [[nodiscard]] CodecError : u8
{
    Ok,          ///< Success.
    Full,        ///< The writer's buffer has no room for the value.
    Truncated,   ///< The input ends before the value does.
    OutOfRange,  ///< A value does not fit the range or the number of bits it was given, or a decoded value is outside
                 ///< the range it must be in.
    TooLong,     ///< A length is above the maximum the caller gave.
    Invalid,     ///< The value or the encoding is not valid: NaN, a bad range, a varint that is not in its shortest
                 ///< form, non-zero padding bits.
};

/// A stable lower-case name for `error`, for logs and tests.
/// - Param error: the error.
/// - Ownership: the string is static.
/// - Fails: never.
const char* CodecErrorName(CodecError error);

/// The largest value of a quantized number of `bits` bits.
/// - Param bits: 1 to 32.
/// - Returns: `2^bits - 1`.
inline constexpr u64 QuantizedLevels(u32 bits)
{
    return (u64{1} << bits) - 1;
}

/// Maps a float to the integer that stands for it.
///
/// A value outside `[min, max]` (infinities included) is clamped to the nearest end; `-0` is `0`; NaN is rejected.
/// The result is the nearest of the `2^bits` evenly spaced levels (halves round up).
/// - Param value: the number.
/// - Param min: the smallest representable value; finite.
/// - Param max: the largest; finite and above `min`.
/// - Param bits: 1 to 32.
/// - Param out: receives the level, 0 to `2^bits - 1`; unchanged on failure.
/// - Ownership: `*out` is written on success.
/// - Returns: success.
/// - Fails: [[net::CodecError::Invalid]] for NaN, a non-finite or empty range, or `bits` outside 1 to 32.
CodecError Quantize(f32 value, f32 min, f32 max, u32 bits, u64* out);

/// Maps a level back to a float.
///
/// The error of `Quantize` followed by `Dequantize` is at most `(max - min) / (2 * (2^bits - 1))` plus one rounding of
/// the result to `f32` (at most `2^-24 * max(|min|, |max|)`), measured against the value clamped to the range.
/// - Param level: 0 to `2^bits - 1`.
/// - Param min: as for [[net::Quantize]].
/// - Param max: as for [[net::Quantize]].
/// - Param bits: 1 to 32.
/// - Param out: receives the value; unchanged on failure.
/// - Ownership: `*out` is written on success.
/// - Returns: success.
/// - Fails: [[net::CodecError::OutOfRange]] if `level` is above `2^bits - 1`, [[net::CodecError::Invalid]] as for
/// [[net::Quantize]].
CodecError Dequantize(u64 level, f32 min, f32 max, u32 bits, f32* out);

/// Writes values into a caller's buffer.
///
/// - Thread-safety: thread-compatible; one thread writes.
/// - Ownership: borrows the buffer, which must outlive the writer; the constructor zeroes it.
class NetWriter
{
public:
    /// Creates a writer over `buffer` (zeroed) positioned at its start.
    /// - Param buffer: where the message goes.
    explicit NetWriter(MutableByteSpan buffer);

    /// The bits written so far.
    /// - Returns: the position in bits.
    usize BitsWritten() const { return bit_; }

    /// The bytes written so far, a partly filled last byte included.
    /// - Returns: the position in bytes, rounded up.
    usize BytesWritten() const { return (bit_ + 7) / 8; }

    /// The message so far.
    /// - Returns: the written bytes, valid while the buffer is.
    ByteSpan Bytes() const { return ByteSpan(buffer_.Data(), BytesWritten()); }

    /// The bits that still fit.
    /// - Returns: the room in bits.
    usize BitsLeft() const { return buffer_.Size() * 8 - bit_; }

    /// Writes the low `bits` bits of `value`.
    /// - Param value: the number; must be below `2^bits`.
    /// - Param bits: 0 to 64.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::OutOfRange]] if `value` does not fit, [[net::CodecError::Invalid]] for `bits` above
    /// 64, [[net::CodecError::Full]].
    CodecError WriteBits(u64 value, u32 bits);

    /// Writes one bit.
    /// - Param value: the flag.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteBool(bool value) { return WriteBits(value ? 1 : 0, 1); }

    /// Writes an integer in `[min, max]` with the fewest bits that can tell its values apart (none when `min == max`).
    /// - Param value: the number.
    /// - Param min: the smallest allowed.
    /// - Param max: the largest allowed.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Invalid]] if `min > max`, [[net::CodecError::OutOfRange]] if `value` is outside,
    /// [[net::CodecError::Full]].
    CodecError WriteRange(i64 value, i64 min, i64 max);

    /// Writes a fixed-width unsigned integer (little-endian when byte aligned).
    /// - Param value: the number.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteU8(u8 value) { return WriteBits(value, 8); }
    /// As [[net::NetWriter::WriteU8]], 16 bits.
    /// - Param value: the number.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteU16(u16 value) { return WriteBits(value, 16); }
    /// As [[net::NetWriter::WriteU8]], 32 bits.
    /// - Param value: the number.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteU32(u32 value) { return WriteBits(value, 32); }
    /// As [[net::NetWriter::WriteU8]], 64 bits.
    /// - Param value: the number.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteU64(u64 value) { return WriteBits(value, 64); }
    /// Writes a signed integer as its two's complement bits, 32 wide.
    /// - Param value: the number.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteI32(i32 value) { return WriteBits(static_cast<u32>(value), 32); }
    /// Writes a signed integer as its two's complement bits, 64 wide.
    /// - Param value: the number.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteI64(i64 value) { return WriteBits(static_cast<u64>(value), 64); }
    /// Writes a float as its 32 bit pattern (NaNs and infinities pass through).
    /// - Param value: the number.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteF32(f32 value);
    /// Writes a double as its 64 bit pattern.
    /// - Param value: the number.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteF64(f64 value);

    /// Writes an unsigned LEB128 varint in its shortest form, the encoding of [[bytes::ByteWriter::WriteVarint]]
    /// (at any bit offset).
    /// - Param value: the number.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteVarint(u64 value);

    /// Writes a signed integer as a zigzag varint (small magnitudes are short).
    /// - Param value: the number.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteSignedVarint(i64 value);

    /// Writes a length-prefixed byte array.
    /// - Param bytes: the data.
    /// - Param max_length: the longest allowed.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::TooLong]] if `bytes` is longer than `max_length`, [[net::CodecError::Full]].
    CodecError WriteBytes(ByteSpan bytes, usize max_length);

    /// Writes a length-prefixed string (its bytes; UTF-8 is not checked).
    /// - Param text: the string.
    /// - Param max_length: the longest allowed, in bytes.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::TooLong]], [[net::CodecError::Full]].
    CodecError WriteString(StringView text, usize max_length);

    /// Pads with zero bits to the next byte boundary.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError AlignToByte();

    /// Writes a float in `[min, max]` with `bits` bits (see [[net::Quantize]]).
    /// - Param value: the number.
    /// - Param min: the smallest value.
    /// - Param max: the largest.
    /// - Param bits: 1 to 32.
    /// - Returns: success.
    /// - Fails: as [[net::Quantize]], [[net::CodecError::Full]].
    CodecError WriteQuantized(f32 value, f32 min, f32 max, u32 bits);

    /// Writes the components of a vector, each as [[net::NetWriter::WriteQuantized]]; all or nothing.
    /// - Param value: the vector.
    /// - Param min: the smallest component value.
    /// - Param max: the largest.
    /// - Param bits: bits per component.
    /// - Returns: success.
    /// - Fails: as [[net::NetWriter::WriteQuantized]].
    CodecError WriteVec2(const Vec2& value, f32 min, f32 max, u32 bits);
    /// As [[net::NetWriter::WriteVec2]] for three components.
    /// - Param value: the vector.
    /// - Param min: the smallest component value.
    /// - Param max: the largest.
    /// - Param bits: bits per component.
    /// - Returns: success.
    /// - Fails: as [[net::NetWriter::WriteQuantized]].
    CodecError WriteVec3(const Vec3& value, f32 min, f32 max, u32 bits);
    /// As [[net::NetWriter::WriteVec2]] for four components.
    /// - Param value: the vector.
    /// - Param min: the smallest component value.
    /// - Param max: the largest.
    /// - Param bits: bits per component.
    /// - Returns: success.
    /// - Fails: as [[net::NetWriter::WriteQuantized]].
    CodecError WriteVec4(const Vec4& value, f32 min, f32 max, u32 bits);

    /// Writes a rotation as "smallest three": the index of the largest component (2 bits), then the other three
    /// quantized in `[-1/sqrt(2), 1/sqrt(2)]` with `bits` bits each, the sign chosen so the largest is positive. The
    /// input is normalized first. The decoded quaternion differs from the normalized input (up to sign) by at most
    /// `4 * sqrt(2) / (2 * (2^bits - 1))` in the Euclidean norm of the four components.
    /// - Param value: the rotation; finite and not zero.
    /// - Param bits: per component, 1 to 24.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Invalid]] for a non-finite or zero quaternion or bad `bits`,
    /// [[net::CodecError::Full]].
    CodecError WriteQuaternion(const Quaternion& value, u32 bits);

    /// Writes an angle in radians modulo a full turn with `bits` bits; the decoded angle is in `[0, 2 pi)` and within
    /// `pi / 2^bits` (circularly) of the input, plus the rounding of `f32`.
    /// - Param radians: finite angle.
    /// - Param bits: 1 to 24.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Invalid]] for NaN or infinity or bad `bits`, [[net::CodecError::Full]].
    CodecError WriteAngle(f32 radians, u32 bits);

    /// Writes `value` as its difference from `baseline` (a signed varint; wraps without loss at the ends of the range).
    /// - Param value: the number.
    /// - Param baseline: what the reader already has.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Full]].
    CodecError WriteDeltaInt(i64 value, i64 baseline);

    /// Writes a quantized float as the difference of its level from the level of `baseline` (a signed varint).
    /// - Param value: the number.
    /// - Param baseline: what the reader already has (quantized the same way).
    /// - Param min: the smallest value.
    /// - Param max: the largest.
    /// - Param bits: 1 to 32.
    /// - Returns: success.
    /// - Fails: as [[net::Quantize]], [[net::CodecError::Full]].
    CodecError WriteDeltaQuantized(f32 value, f32 baseline, f32 min, f32 max, u32 bits);

private:
    // Writes without checking: the caller has made sure the bits fit.
    void PutBits(u64 value, u32 bits);
    CodecError Reserve(usize bits) const { return bits > BitsLeft() ? CodecError::Full : CodecError::Ok; }

    MutableByteSpan buffer_;
    usize bit_ = 0;
};

/// Reads values written by [[net::NetWriter]].
///
/// - Thread-safety: thread-compatible; one thread reads.
/// - Ownership: borrows the data, which must outlive the reader.
class NetReader
{
public:
    /// Creates a reader over `data` positioned at its start.
    /// - Param data: the message.
    explicit NetReader(ByteSpan data) : data_(data) {}

    /// The bits read so far.
    /// - Returns: the position in bits.
    usize BitPosition() const { return bit_; }

    /// The bits that remain.
    /// - Returns: the count.
    usize BitsLeft() const { return data_.Size() * 8 - bit_; }

    /// Tells whether less than a byte remains and all of it is padding, i.e. the message has been read to its end.
    /// - Returns: true at the end of a message that was written in whole bytes.
    bool AtEnd() const { return BitsLeft() < 8 && RestIsZero(); }

    /// Reads `bits` bits.
    /// - Param bits: 0 to 64.
    /// - Param out: receives the value; unchanged on failure.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Truncated]], [[net::CodecError::Invalid]] for `bits` above 64.
    CodecError ReadBits(u32 bits, u64* out);

    /// Reads one bit.
    /// - Param out: receives the flag.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Truncated]].
    CodecError ReadBool(bool* out);

    /// Reads an integer written by [[net::NetWriter::WriteRange]].
    /// - Param min: the smallest allowed.
    /// - Param max: the largest.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Invalid]] if `min > max`, [[net::CodecError::OutOfRange]] if the bits decode to a
    /// value above `max`, [[net::CodecError::Truncated]].
    CodecError ReadRange(i64 min, i64 max, i64* out);

    /// Reads a fixed-width unsigned integer.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Truncated]].
    CodecError ReadU8(u8* out);
    /// As [[net::NetReader::ReadU8]], 16 bits.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Truncated]].
    CodecError ReadU16(u16* out);
    /// As [[net::NetReader::ReadU8]], 32 bits.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Truncated]].
    CodecError ReadU32(u32* out);
    /// As [[net::NetReader::ReadU8]], 64 bits.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Truncated]].
    CodecError ReadU64(u64* out);
    /// Reads a signed 32 bit integer.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Truncated]].
    CodecError ReadI32(i32* out);
    /// Reads a signed 64 bit integer.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Truncated]].
    CodecError ReadI64(i64* out);
    /// Reads a float from its bit pattern.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Truncated]].
    CodecError ReadF32(f32* out);
    /// Reads a double from its bit pattern.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Truncated]].
    CodecError ReadF64(f64* out);

    /// Reads an unsigned varint; only the shortest form is accepted.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Truncated]], [[net::CodecError::Invalid]] for a longer-than-needed form or one
    /// that passes 64 bits.
    CodecError ReadVarint(u64* out);

    /// Reads a zigzag varint.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: as [[net::NetReader::ReadVarint]].
    CodecError ReadSignedVarint(i64* out);

    /// Reads a length-prefixed byte array and appends it to `out`.
    /// - Param max_length: the longest allowed; a longer array is rejected before anything is allocated.
    /// - Param out: receives the bytes (appended); unchanged on failure.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::TooLong]], [[net::CodecError::Truncated]] (also when the announced length is more
    /// than the remaining bytes), [[net::CodecError::Invalid]].
    CodecError ReadBytes(usize max_length, Vector<byte>* out);

    /// Reads a length-prefixed string and appends its bytes to `out` (UTF-8 is not checked).
    /// - Param max_length: the longest allowed, in bytes.
    /// - Param out: receives the text (appended); unchanged on failure.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: as [[net::NetReader::ReadBytes]].
    CodecError ReadString(usize max_length, String<HeapAllocator>* out);

    /// Skips to the next byte boundary; the skipped bits must be zero.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Invalid]] for a non-zero padding bit.
    CodecError AlignToByte();

    /// Reads a float written by [[net::NetWriter::WriteQuantized]].
    /// - Param min: the smallest value.
    /// - Param max: the largest.
    /// - Param bits: 1 to 32.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Invalid]] for a bad range, [[net::CodecError::Truncated]].
    CodecError ReadQuantized(f32 min, f32 max, u32 bits, f32* out);

    /// Reads a 2-component vector.
    /// - Param min: the smallest component value.
    /// - Param max: the largest.
    /// - Param bits: bits per component.
    /// - Param out: receives the vector; unchanged on failure.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: as [[net::NetReader::ReadQuantized]].
    CodecError ReadVec2(f32 min, f32 max, u32 bits, Vec2* out);
    /// Reads a 3-component vector.
    /// - Param min: the smallest component value.
    /// - Param max: the largest.
    /// - Param bits: bits per component.
    /// - Param out: receives the vector; unchanged on failure.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: as [[net::NetReader::ReadQuantized]].
    CodecError ReadVec3(f32 min, f32 max, u32 bits, Vec3* out);
    /// Reads a 4-component vector.
    /// - Param min: the smallest component value.
    /// - Param max: the largest.
    /// - Param bits: bits per component.
    /// - Param out: receives the vector; unchanged on failure.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: as [[net::NetReader::ReadQuantized]].
    CodecError ReadVec4(f32 min, f32 max, u32 bits, Vec4* out);

    /// Reads a rotation written by [[net::NetWriter::WriteQuaternion]]; the result is a unit quaternion with a
    /// non-negative largest component.
    /// - Param bits: per component, 1 to 24.
    /// - Param out: receives the rotation; unchanged on failure.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Invalid]] for bad `bits`, [[net::CodecError::Truncated]].
    CodecError ReadQuaternion(u32 bits, Quaternion* out);

    /// Reads an angle written by [[net::NetWriter::WriteAngle]].
    /// - Param bits: 1 to 24.
    /// - Param out: receives the angle in `[0, 2 pi)`.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::Invalid]] for bad `bits`, [[net::CodecError::Truncated]].
    CodecError ReadAngle(u32 bits, f32* out);

    /// Reads a value written by [[net::NetWriter::WriteDeltaInt]].
    /// - Param baseline: the value the writer used.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: as [[net::NetReader::ReadVarint]].
    CodecError ReadDeltaInt(i64 baseline, i64* out);

    /// Reads a value written by [[net::NetWriter::WriteDeltaQuantized]].
    /// - Param baseline: the value the writer used.
    /// - Param min: the smallest value.
    /// - Param max: the largest.
    /// - Param bits: 1 to 32.
    /// - Param out: receives the value.
    /// - Ownership: `*out` is written on success.
    /// - Returns: success.
    /// - Fails: [[net::CodecError::OutOfRange]] if the difference leads outside the levels, otherwise as
    /// [[net::NetReader::ReadVarint]] and [[net::Quantize]].
    CodecError ReadDeltaQuantized(f32 baseline, f32 min, f32 max, u32 bits, f32* out);

private:
    u64 Peek(usize at, u32 bits) const;
    CodecError ReadComponents(u32 count, f32 min, f32 max, u32 bits, f32* out);
    bool RestIsZero() const;

    ByteSpan data_;
    usize bit_ = 0;
};

}  // namespace engine_placeholder::net
