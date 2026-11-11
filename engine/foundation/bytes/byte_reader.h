#pragma once

//! [[bytes::ByteReader]]: the canonical byte encoding, read back with every access bounds-checked. The matching writer
//! is [[bytes::ByteWriter]].

#include "bytes/byte_error.h"
#include "containers/span.h"
#include "cstr/mem_ops.h"
#include "types.h"

namespace engine_placeholder::bytes
{

/// Reads values from a span of bytes, in the encoding [[bytes::ByteWriter]] writes. Input is untrusted: a read that
/// would pass the end returns
/// [[bytes::ByteError::Truncated]], a length above the caller's limit returns `TooLong`, and a malformed value returns
/// `Invalid`. A failed read does not move the reader and does not write its output. The reader never reads outside the
/// span it was given.
///
/// - Thread-safety: not-thread-safe; synchronize externally if shared.
/// - Ownership: borrows the span; the bytes must outlive the reader and every span it hands out.
class ByteReader
{
public:
    /// Creates a reader over `data`.
    explicit ByteReader(ByteSpan data) : data_(data) {}

    /// The number of bytes not read yet.
    usize Remaining() const { return data_.Size() - position_; }
    /// The number of bytes read so far.
    usize Position() const { return position_; }
    /// Returns true when every byte has been read.
    /// - Fails: never.
    bool AtEnd() const { return position_ == data_.Size(); }

    /// Reads one byte. Fails: `Truncated`.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadU8(u8* out)
    {
        if (Remaining() < 1)
        {
            return ByteError::Truncated;
        }
        *out = static_cast<u8>(data_[position_]);
        ++position_;
        return ByteError::Ok;
    }
    /// Reads two bytes, low first. Fails: `Truncated`.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadU16(u16* out)
    {
        u64 value = 0;
        const ByteError error = ReadLittleEndian(2, &value);
        if (error == ByteError::Ok)
        {
            *out = static_cast<u16>(value);
        }
        return error;
    }
    /// Reads four bytes, low first. Fails: `Truncated`.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadU32(u32* out)
    {
        u64 value = 0;
        const ByteError error = ReadLittleEndian(4, &value);
        if (error == ByteError::Ok)
        {
            *out = static_cast<u32>(value);
        }
        return error;
    }
    /// Reads eight bytes, low first. Fails: `Truncated`.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadU64(u64* out) { return ReadLittleEndian(8, out); }
    /// Reads a two's-complement byte. Fails: `Truncated`.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadI8(i8* out)
    {
        u8 value = 0;
        const ByteError error = ReadU8(&value);
        if (error == ByteError::Ok)
        {
            *out = static_cast<i8>(value);
        }
        return error;
    }
    /// Reads a two's-complement value. Fails: `Truncated`.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadI16(i16* out)
    {
        u16 value = 0;
        const ByteError error = ReadU16(&value);
        if (error == ByteError::Ok)
        {
            *out = static_cast<i16>(value);
        }
        return error;
    }
    /// Reads a two's-complement value. Fails: `Truncated`.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadI32(i32* out)
    {
        u32 value = 0;
        const ByteError error = ReadU32(&value);
        if (error == ByteError::Ok)
        {
            *out = static_cast<i32>(value);
        }
        return error;
    }
    /// Reads a two's-complement value. Fails: `Truncated`.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadI64(i64* out)
    {
        u64 value = 0;
        const ByteError error = ReadU64(&value);
        if (error == ByteError::Ok)
        {
            *out = static_cast<i64>(value);
        }
        return error;
    }
    /// Reads the bit pattern of an `f32` (a NaN keeps its payload). Fails: `Truncated`.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadF32(f32* out)
    {
        u32 bits = 0;
        const ByteError error = ReadU32(&bits);
        if (error == ByteError::Ok)
        {
            MemCopy(out, &bits, sizeof(bits));
        }
        return error;
    }
    /// Reads the bit pattern of an `f64` (a NaN keeps its payload). Fails: `Truncated`.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadF64(f64* out)
    {
        u64 bits = 0;
        const ByteError error = ReadU64(&bits);
        if (error == ByteError::Ok)
        {
            MemCopy(out, &bits, sizeof(bits));
        }
        return error;
    }
    /// Reads one byte that must be 0 or 1. Fails: `Truncated`; `Invalid` for any other byte.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadBool(bool* out)
    {
        if (Remaining() < 1)
        {
            return ByteError::Truncated;
        }
        const u8 value = static_cast<u8>(data_[position_]);
        if (value > 1)
        {
            return ByteError::Invalid;
        }
        ++position_;
        *out = value == 1;
        return ByteError::Ok;
    }
    /// Reads an unsigned LEB128 varint. Only the shortest form is accepted (a trailing zero group is `Invalid`), so
    /// every value has one encoding.
    /// - Fails: `Truncated`; `Invalid` (longer than ten bytes, a value above 64 bits, or not the shortest form).
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadVarint(u64* out)
    {
        u64 value = 0;
        u32 shift = 0;
        usize at = position_;
        for (;;)
        {
            if (at >= data_.Size())
            {
                return ByteError::Truncated;
            }
            const u8 group = static_cast<u8>(data_[at]);
            ++at;
            const u64 bits = group & 0x7f;
            if (shift == 63 && bits > 1)
            {
                return ByteError::Invalid;  // would pass 64 bits
            }
            value |= bits << shift;
            if ((group & 0x80) == 0)
            {
                if (group == 0 && shift > 0)
                {
                    return ByteError::Invalid;  // a trailing zero group: not the shortest form
                }
                break;
            }
            shift += 7;
            if (shift > 63)
            {
                return ByteError::Invalid;
            }
        }
        position_ = at;
        *out = value;
        return ByteError::Ok;
    }
    /// Reads a varint that is a length or a count and checks it against `max`.
    ///
    /// - Param max: the largest accepted value (inclusive).
    /// - Fails: `Truncated`; `Invalid` (as `ReadVarint`); `TooLong` when the value is above `max` (the reader does not
    /// move).
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadLength(u64 max, u64* out)
    {
        const usize before = position_;
        u64 value = 0;
        const ByteError error = ReadVarint(&value);
        if (error != ByteError::Ok)
        {
            return error;
        }
        if (value > max)
        {
            position_ = before;
            return ByteError::TooLong;
        }
        *out = value;
        return ByteError::Ok;
    }
    /// Takes the next `count` bytes as a view into the input (no copy).
    ///
    /// - Param count: how many bytes; may be zero.
    /// - Param out: receives the view, valid while the input is.
    /// - Fails: `Truncated` when fewer than `count` bytes remain.
    /// - Ownership: `out` is written during the call only, and only on success.
    ByteError ReadBytes(usize count, ByteSpan* out)
    {
        if (count > Remaining())
        {
            return ByteError::Truncated;
        }
        *out = ByteSpan(data_.Data() + position_, count);
        position_ += count;
        return ByteError::Ok;
    }

private:
    ByteError ReadLittleEndian(u32 count, u64* out)
    {
        if (Remaining() < count)
        {
            return ByteError::Truncated;
        }
        u64 value = 0;
        for (u32 i = 0; i < count; ++i)
        {
            value |= static_cast<u64>(static_cast<u8>(data_[position_ + i])) << (8 * i);
        }
        position_ += count;
        *out = value;
        return ByteError::Ok;
    }

    ByteSpan data_;
    usize position_ = 0;
};

}  // namespace engine_placeholder::bytes
