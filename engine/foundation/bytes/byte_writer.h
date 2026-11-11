#pragma once

//! [[bytes::ByteWriter]]: the canonical byte encoding, written. Little-endian, fixed-width integers, floats as their bit patterns, no padding.
//! The matching reader is [[bytes::ByteReader]] (`bytes/byte_reader.h`). Storage (Phase 19) and networking (Phase 20) share them.

#include "containers/span.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "log/assert.h"
#include "types.h"

namespace engine_placeholder::bytes {

/// Appends values to a growing buffer in the canonical encoding: every integer little-endian at its own width; `f32` and `f64` as the bit pattern of the
/// value (a NaN payload, `-0` and denormals survive); `bool` as one byte (0 or 1); lengths as unsigned LEB128 varints in their shortest form.
///
/// Thread-safety: not-thread-safe; synchronize externally if shared.
/// Ownership: owns the buffer; `Bytes()` views it until the next write or `Clear`.
class ByteWriter {
public:
    /// Creates an empty writer.
    ByteWriter() = default;

    /// Appends one byte.
    /// Aborts: the allocator is out of memory (as every append).
    void WriteU8(u8 value) { buffer_.Push(static_cast<byte>(value)); }
    /// Appends two bytes, low first.
    void WriteU16(u16 value) { WriteLittleEndian(value, 2); }
    /// Appends four bytes, low first.
    void WriteU32(u32 value) { WriteLittleEndian(value, 4); }
    /// Appends eight bytes, low first.
    void WriteU64(u64 value) { WriteLittleEndian(value, 8); }
    /// Appends the two's-complement byte.
    void WriteI8(i8 value) { WriteU8(static_cast<u8>(value)); }
    /// Appends the two's-complement value, low byte first.
    void WriteI16(i16 value) { WriteU16(static_cast<u16>(value)); }
    /// Appends the two's-complement value, low byte first.
    void WriteI32(i32 value) { WriteU32(static_cast<u32>(value)); }
    /// Appends the two's-complement value, low byte first.
    void WriteI64(i64 value) { WriteU64(static_cast<u64>(value)); }
    /// Appends the bit pattern of `value` as four bytes.
    void WriteF32(f32 value) {
        u32 bits = 0;
        MemCopy(&bits, &value, sizeof(bits));
        WriteU32(bits);
    }
    /// Appends the bit pattern of `value` as eight bytes.
    void WriteF64(f64 value) {
        u64 bits = 0;
        MemCopy(&bits, &value, sizeof(bits));
        WriteU64(bits);
    }
    /// Appends one byte, 1 for true and 0 for false.
    void WriteBool(bool value) { WriteU8(value ? 1 : 0); }
    /// Appends `value` as an unsigned LEB128 varint (seven bits per byte, low group first, the high bit set on all but the last byte): one to ten bytes.
    void WriteVarint(u64 value) {
        while (value >= 0x80) {
            WriteU8(static_cast<u8>((value & 0x7f) | 0x80));
            value >>= 7;
        }
        WriteU8(static_cast<u8>(value));
    }
    /// Appends the bytes as they are. The bytes may be a view of this writer's own `Bytes()` (they are appended as they were before the call).
    void WriteBytes(ByteSpan bytes) {
        const usize count = bytes.Size();
        const usize begin = reinterpret_cast<usize>(buffer_.Data());
        const usize at = reinterpret_cast<usize>(bytes.Data());
        const bool own = count != 0 && at >= begin && at < begin + buffer_.Size();
        const usize offset = own ? at - begin : 0; // the buffer may move when it grows: remember where in it the bytes were
        buffer_.Reserve(buffer_.Size() + count);
        for (usize i = 0; i < count; ++i) {
            buffer_.Push(own ? buffer_.Data()[offset + i] : bytes[i]);
        }
    }

    /// The bytes written so far.
    ByteSpan Bytes() const { return ByteSpan(buffer_.Data(), buffer_.Size()); }
    /// The number of bytes written.
    usize Size() const { return buffer_.Size(); }
    /// Forgets everything written (keeps the capacity).
    void Clear() { buffer_.Clear(); }
    /// Cuts the buffer back to `size` bytes, which must not exceed `Size()` (to undo a partly written value).
    /// Aborts: `size` is above `Size()`.
    void Truncate(usize size) {
        ENGINE_ASSERT(size <= buffer_.Size(), "ByteWriter::Truncate: the size is above the written size");
        buffer_.Resize(size);
    }

private:
    void WriteLittleEndian(u64 value, u32 count) {
        for (u32 i = 0; i < count; ++i) {
            buffer_.Push(static_cast<byte>((value >> (8 * i)) & 0xff));
        }
    }

    Vector<byte> buffer_;
};

} // namespace engine_placeholder::bytes
