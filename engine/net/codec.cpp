#include "net/codec.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"

namespace engine_placeholder::net
{

namespace
{

constexpr f64 kTwoPi = 6.283185307179586476925286766559;
constexpr f32 kQuaternionLimit = 0.70710678118654752440f;  // 1 / sqrt(2)

u32 BitWidth(u64 value)
{
    u32 width = 0;
    while (value != 0)
    {
        ++width;
        value >>= 1;
    }
    return width;
}

usize VarintBytes(u64 value)
{
    usize bytes = 1;
    while (value >= 0x80)
    {
        ++bytes;
        value >>= 7;
    }
    return bytes;
}

bool ValidRange(f32 min, f32 max)
{
    return IsFinite(min) && IsFinite(max) && min < max;
}

// Rounds a non-negative scaled value to the nearest whole number, halves up. `scaled` has been stored in a
// variable by the caller, so the product that made it cannot be fused with the subtraction here.
u64 RoundNearest(volatile f64& scaled)
{
    const f64 value = scaled;
    const f64 floor = Floor(value);
    const volatile f64 frac = value - floor;
    return static_cast<u64>(floor) + (frac >= 0.5 ? 1 : 0);
}

}  // namespace

const char* CodecErrorName(CodecError error)
{
    switch (error)
    {
    case CodecError::Ok: return "ok";
    case CodecError::Full: return "full";
    case CodecError::Truncated: return "truncated";
    case CodecError::OutOfRange: return "out of range";
    case CodecError::TooLong: return "too long";
    case CodecError::Invalid: return "invalid";
    }
    return "unknown";
}

CodecError Quantize(f32 value, f32 min, f32 max, u32 bits, u64* out)
{
    if (!ValidRange(min, max) || bits < 1 || bits > 32 || IsNaN(value))
    {
        return CodecError::Invalid;
    }
    const f64 lo = min;
    const f64 hi = max;
    f64 v = value;  // infinities clamp like any other value outside the range
    v = v < lo ? lo : (v > hi ? hi : v);
    const f64 levels = static_cast<f64>(QuantizedLevels(bits));
    const volatile f64 range = hi - lo;
    const volatile f64 t = (v - lo) / range;
    volatile f64 scaled = t * levels;
    u64 level = RoundNearest(scaled);
    const u64 top = QuantizedLevels(bits);
    *out = level > top ? top : level;
    return CodecError::Ok;
}

CodecError Dequantize(u64 level, f32 min, f32 max, u32 bits, f32* out)
{
    if (!ValidRange(min, max) || bits < 1 || bits > 32)
    {
        return CodecError::Invalid;
    }
    if (level > QuantizedLevels(bits))
    {
        return CodecError::OutOfRange;
    }
    const f64 lo = min;
    const f64 hi = max;
    const volatile f64 range = hi - lo;
    const volatile f64 step = range / static_cast<f64>(QuantizedLevels(bits));
    const volatile f64 offset = static_cast<f64>(level) * step;
    *out = static_cast<f32>(lo + offset);
    return CodecError::Ok;
}

// ---- NetWriter ----

NetWriter::NetWriter(MutableByteSpan buffer) : buffer_(buffer)
{
    if (buffer.Size() != 0)
    {
        MemSet(buffer.Data(), 0, buffer.Size());
    }
}

void NetWriter::PutBits(u64 value, u32 bits)
{
    while (bits > 0)
    {
        const u32 offset = static_cast<u32>(bit_ % 8);
        const u32 take = 8 - offset < bits ? 8 - offset : bits;
        const u64 part = value & ((u64{1} << take) - 1);
        buffer_[bit_ / 8] = static_cast<byte>(static_cast<u8>(buffer_[bit_ / 8]) | static_cast<u8>(part << offset));
        value >>= take;
        bit_ += take;
        bits -= take;
    }
}

CodecError NetWriter::WriteBits(u64 value, u32 bits)
{
    if (bits > 64)
    {
        return CodecError::Invalid;
    }
    if (bits < 64 && (value >> bits) != 0)
    {
        return CodecError::OutOfRange;
    }
    if (const CodecError full = Reserve(bits); full != CodecError::Ok)
    {
        return full;
    }
    PutBits(value, bits);
    return CodecError::Ok;
}

CodecError NetWriter::WriteRange(i64 value, i64 min, i64 max)
{
    if (min > max)
    {
        return CodecError::Invalid;
    }
    if (value < min || value > max)
    {
        return CodecError::OutOfRange;
    }
    const u64 span = static_cast<u64>(max) - static_cast<u64>(min);
    return WriteBits(static_cast<u64>(value) - static_cast<u64>(min), BitWidth(span));
}

CodecError NetWriter::WriteF32(f32 value)
{
    u32 bits = 0;
    MemCopy(&bits, &value, sizeof(bits));
    return WriteBits(bits, 32);
}

CodecError NetWriter::WriteF64(f64 value)
{
    u64 bits = 0;
    MemCopy(&bits, &value, sizeof(bits));
    return WriteBits(bits, 64);
}

CodecError NetWriter::WriteVarint(u64 value)
{
    if (const CodecError full = Reserve(VarintBytes(value) * 8); full != CodecError::Ok)
    {
        return full;
    }
    while (value >= 0x80)
    {
        PutBits((value & 0x7f) | 0x80, 8);
        value >>= 7;
    }
    PutBits(value, 8);
    return CodecError::Ok;
}

CodecError NetWriter::WriteSignedVarint(i64 value)
{
    const u64 zigzag = (static_cast<u64>(value) << 1) ^ static_cast<u64>(value >> 63);
    return WriteVarint(zigzag);
}

CodecError NetWriter::WriteBytes(ByteSpan bytes, usize max_length)
{
    if (bytes.Size() > max_length)
    {
        return CodecError::TooLong;
    }
    const usize total = VarintBytes(bytes.Size()) * 8 + bytes.Size() * 8;
    if (const CodecError full = Reserve(total); full != CodecError::Ok)
    {
        return full;
    }
    const CodecError length = WriteVarint(bytes.Size());  // cannot fail: the room was checked
    if (length != CodecError::Ok)
    {
        return length;
    }
    if (bit_ % 8 == 0)
    {
        if (bytes.Size() != 0)
        {
            MemCopy(buffer_.Data() + bit_ / 8, bytes.Data(), bytes.Size());
            bit_ += bytes.Size() * 8;
        }
        return CodecError::Ok;
    }
    for (usize i = 0; i < bytes.Size(); ++i)
    {
        PutBits(static_cast<u8>(bytes[i]), 8);
    }
    return CodecError::Ok;
}

CodecError NetWriter::WriteString(StringView text, usize max_length)
{
    if (text.Length() > max_length)
    {
        return CodecError::TooLong;
    }
    if (const CodecError full = Reserve(VarintBytes(text.Length()) * 8 + text.Length() * 8); full != CodecError::Ok)
    {
        return full;
    }
    const CodecError length = WriteVarint(text.Length());  // cannot fail: the room was checked
    if (length != CodecError::Ok)
    {
        return length;
    }
    for (usize i = 0; i < text.Length(); ++i)
    {
        PutBits(static_cast<u8>(text.Data()[i]), 8);
    }
    return CodecError::Ok;
}

CodecError NetWriter::AlignToByte()
{
    const u32 pad = static_cast<u32>((8 - bit_ % 8) % 8);
    if (const CodecError full = Reserve(pad); full != CodecError::Ok)
    {
        return full;
    }
    bit_ += pad;  // the buffer is zeroed, so the padding bits are zero
    return CodecError::Ok;
}

CodecError NetWriter::WriteQuantized(f32 value, f32 min, f32 max, u32 bits)
{
    u64 level = 0;
    if (const CodecError q = Quantize(value, min, max, bits, &level); q != CodecError::Ok)
    {
        return q;
    }
    return WriteBits(level, bits);
}

namespace
{

// Quantizes `count` components; nothing is written unless all of them are valid and fit.
template <class Put>
CodecError WriteComponents(const f32* values, u32 count, f32 min, f32 max, u32 bits, usize room_bits, Put put)
{
    if (bits < 1 || bits > 32)
    {
        return CodecError::Invalid;
    }
    u64 levels[4] = {};
    for (u32 i = 0; i < count; ++i)
    {
        if (const CodecError q = Quantize(values[i], min, max, bits, &levels[i]); q != CodecError::Ok)
        {
            return q;
        }
    }
    if (static_cast<usize>(count) * bits > room_bits)
    {
        return CodecError::Full;
    }
    for (u32 i = 0; i < count; ++i)
    {
        put(levels[i]);
    }
    return CodecError::Ok;
}

}  // namespace

CodecError NetWriter::WriteVec2(const Vec2& value, f32 min, f32 max, u32 bits)
{
    const f32 c[2] = {value.x, value.y};
    return WriteComponents(c, 2, min, max, bits, BitsLeft(), [this, bits](u64 level) { PutBits(level, bits); });
}

CodecError NetWriter::WriteVec3(const Vec3& value, f32 min, f32 max, u32 bits)
{
    const f32 c[3] = {value.x, value.y, value.z};
    return WriteComponents(c, 3, min, max, bits, BitsLeft(), [this, bits](u64 level) { PutBits(level, bits); });
}

CodecError NetWriter::WriteVec4(const Vec4& value, f32 min, f32 max, u32 bits)
{
    const f32 c[4] = {value.x, value.y, value.z, value.w};
    return WriteComponents(c, 4, min, max, bits, BitsLeft(), [this, bits](u64 level) { PutBits(level, bits); });
}

CodecError NetWriter::WriteQuaternion(const Quaternion& value, u32 bits)
{
    if (bits < 1 || bits > 24 || !IsFinite(value.x) || !IsFinite(value.y) || !IsFinite(value.z) || !IsFinite(value.w))
    {
        return CodecError::Invalid;
    }
    f64 c[4] = {value.x, value.y, value.z, value.w};
    const volatile f64 sx = c[0] * c[0];
    const volatile f64 sy = c[1] * c[1];
    const volatile f64 sz = c[2] * c[2];
    const volatile f64 sw = c[3] * c[3];
    const volatile f64 sum_xy = sx + sy;
    const volatile f64 sum_zw = sz + sw;
    const f64 length = Sqrt(static_cast<f64>(sum_xy + sum_zw));
    if (!(length > 0.0) || !IsFinite(length))
    {
        return CodecError::Invalid;
    }
    u32 largest = 0;
    for (u32 i = 1; i < 4; ++i)
    {
        if (Abs(c[i]) > Abs(c[largest]))
        {
            largest = i;
        }
    }
    const f64 sign = c[largest] < 0 ? -1.0 : 1.0;
    u64 levels[3] = {};
    u32 n = 0;
    for (u32 i = 0; i < 4; ++i)
    {
        if (i == largest)
        {
            continue;
        }
        const volatile f64 unit = c[i] / length;
        const f32 part = static_cast<f32>(sign * unit);
        if (const CodecError q = Quantize(part, -kQuaternionLimit, kQuaternionLimit, bits, &levels[n++]);
            q != CodecError::Ok)
        {
            return q;
        }
    }
    if (const CodecError full = Reserve(2 + 3 * static_cast<usize>(bits)); full != CodecError::Ok)
    {
        return full;
    }
    PutBits(largest, 2);
    for (u64 level : levels)
    {
        PutBits(level, bits);
    }
    return CodecError::Ok;
}

CodecError NetWriter::WriteAngle(f32 radians, u32 bits)
{
    if (bits < 1 || bits > 24 || !IsFinite(radians))
    {
        return CodecError::Invalid;
    }
    const u64 steps = u64{1} << bits;
    const volatile f64 turns = static_cast<f64>(radians) / kTwoPi;
    const f64 whole = Floor(static_cast<f64>(turns));
    const volatile f64 wrapped = static_cast<f64>(turns) - whole;
    volatile f64 scaled = static_cast<f64>(wrapped) * static_cast<f64>(steps);
    const u64 level = RoundNearest(scaled) % steps;  // the last half step rounds up to a full turn: level 0
    return WriteBits(level, bits);
}

CodecError NetWriter::WriteDeltaInt(i64 value, i64 baseline)
{
    return WriteSignedVarint(static_cast<i64>(static_cast<u64>(value) - static_cast<u64>(baseline)));
}

CodecError NetWriter::WriteDeltaQuantized(f32 value, f32 baseline, f32 min, f32 max, u32 bits)
{
    u64 level = 0;
    u64 base = 0;
    if (const CodecError q = Quantize(value, min, max, bits, &level); q != CodecError::Ok)
    {
        return q;
    }
    if (const CodecError q = Quantize(baseline, min, max, bits, &base); q != CodecError::Ok)
    {
        return q;
    }
    return WriteSignedVarint(static_cast<i64>(level) - static_cast<i64>(base));
}

// ---- NetReader ----

u64 NetReader::Peek(usize at, u32 bits) const
{
    u64 value = 0;
    u32 done = 0;
    while (done < bits)
    {
        const u32 offset = static_cast<u32>(at % 8);
        const u32 take = 8 - offset < bits - done ? 8 - offset : bits - done;
        const u64 part = (static_cast<u64>(static_cast<u8>(data_[at / 8])) >> offset) & ((u64{1} << take) - 1);
        value |= part << done;
        done += take;
        at += take;
    }
    return value;
}

bool NetReader::RestIsZero() const
{
    return BitsLeft() == 0 || Peek(bit_, static_cast<u32>(BitsLeft())) == 0;
}

CodecError NetReader::ReadBits(u32 bits, u64* out)
{
    if (bits > 64)
    {
        return CodecError::Invalid;
    }
    if (bits > BitsLeft())
    {
        return CodecError::Truncated;
    }
    *out = Peek(bit_, bits);
    bit_ += bits;
    return CodecError::Ok;
}

CodecError NetReader::ReadBool(bool* out)
{
    u64 bit = 0;
    const CodecError e = ReadBits(1, &bit);
    if (e == CodecError::Ok)
    {
        *out = bit != 0;
    }
    return e;
}

CodecError NetReader::ReadRange(i64 min, i64 max, i64* out)
{
    if (min > max)
    {
        return CodecError::Invalid;
    }
    const u64 span = static_cast<u64>(max) - static_cast<u64>(min);
    const u32 bits = BitWidth(span);
    if (bits > BitsLeft())
    {
        return CodecError::Truncated;
    }
    const u64 offset = Peek(bit_, bits);
    if (offset > span)
    {
        return CodecError::OutOfRange;
    }
    bit_ += bits;
    *out = static_cast<i64>(static_cast<u64>(min) + offset);
    return CodecError::Ok;
}

CodecError NetReader::ReadU8(u8* out)
{
    u64 v = 0;
    const CodecError e = ReadBits(8, &v);
    if (e == CodecError::Ok)
    {
        *out = static_cast<u8>(v);
    }
    return e;
}

CodecError NetReader::ReadU16(u16* out)
{
    u64 v = 0;
    const CodecError e = ReadBits(16, &v);
    if (e == CodecError::Ok)
    {
        *out = static_cast<u16>(v);
    }
    return e;
}

CodecError NetReader::ReadU32(u32* out)
{
    u64 v = 0;
    const CodecError e = ReadBits(32, &v);
    if (e == CodecError::Ok)
    {
        *out = static_cast<u32>(v);
    }
    return e;
}

CodecError NetReader::ReadU64(u64* out)
{
    return ReadBits(64, out);
}

CodecError NetReader::ReadI32(i32* out)
{
    u32 v = 0;
    const CodecError e = ReadU32(&v);
    if (e == CodecError::Ok)
    {
        *out = static_cast<i32>(v);
    }
    return e;
}

CodecError NetReader::ReadI64(i64* out)
{
    u64 v = 0;
    const CodecError e = ReadU64(&v);
    if (e == CodecError::Ok)
    {
        *out = static_cast<i64>(v);
    }
    return e;
}

CodecError NetReader::ReadF32(f32* out)
{
    u32 bits = 0;
    const CodecError e = ReadU32(&bits);
    if (e == CodecError::Ok)
    {
        MemCopy(out, &bits, sizeof(bits));
    }
    return e;
}

CodecError NetReader::ReadF64(f64* out)
{
    u64 bits = 0;
    const CodecError e = ReadU64(&bits);
    if (e == CodecError::Ok)
    {
        MemCopy(out, &bits, sizeof(bits));
    }
    return e;
}

CodecError NetReader::ReadVarint(u64* out)
{
    u64 value = 0;
    u32 shift = 0;
    usize at = bit_;
    const usize total = data_.Size() * 8;
    for (;;)
    {
        if (at + 8 > total)
        {
            return CodecError::Truncated;
        }
        const u64 group = Peek(at, 8);
        at += 8;
        const u64 bits = group & 0x7f;
        if (shift == 63 && bits > 1)
        {
            return CodecError::Invalid;  // would pass 64 bits
        }
        value |= bits << shift;
        if ((group & 0x80) == 0)
        {
            if (group == 0 && shift > 0)
            {
                return CodecError::Invalid;  // a trailing zero group: not the shortest form
            }
            bit_ = at;
            *out = value;
            return CodecError::Ok;
        }
        shift += 7;
        if (shift > 63)
        {
            return CodecError::Invalid;
        }
    }
}

CodecError NetReader::ReadSignedVarint(i64* out)
{
    u64 zigzag = 0;
    if (const CodecError e = ReadVarint(&zigzag); e != CodecError::Ok)
    {
        return e;
    }
    *out = static_cast<i64>((zigzag >> 1) ^ (~(zigzag & 1) + 1));
    return CodecError::Ok;
}

CodecError NetReader::ReadBytes(usize max_length, Vector<byte>* out)
{
    const usize start = bit_;
    u64 length = 0;
    if (const CodecError e = ReadVarint(&length); e != CodecError::Ok)
    {
        return e;
    }
    if (length > max_length)
    {
        bit_ = start;
        return CodecError::TooLong;
    }
    if (length > BitsLeft() / 8)
    {
        bit_ = start;
        return CodecError::Truncated;
    }
    const usize old = out->Size();
    if (!out->TryResizeUninitialized(old + static_cast<usize>(length)))
    {
        bit_ = start;
        return CodecError::TooLong;
    }
    for (usize i = 0; i < length; ++i)
    {
        (*out)[old + i] = static_cast<byte>(Peek(bit_, 8));
        bit_ += 8;
    }
    return CodecError::Ok;
}

CodecError NetReader::ReadString(usize max_length, String<HeapAllocator>* out)
{
    const usize start = bit_;
    u64 length = 0;
    if (const CodecError e = ReadVarint(&length); e != CodecError::Ok)
    {
        return e;
    }
    if (length > max_length)
    {
        bit_ = start;
        return CodecError::TooLong;
    }
    if (length > BitsLeft() / 8)
    {
        bit_ = start;
        return CodecError::Truncated;
    }
    Vector<char> text;
    if (!text.TryResizeUninitialized(static_cast<usize>(length)))
    {
        bit_ = start;
        return CodecError::TooLong;
    }
    for (usize i = 0; i < length; ++i)
    {
        text[i] = static_cast<char>(Peek(bit_, 8));
        bit_ += 8;
    }
    if (length != 0)
    {
        out->AppendRawBytes(text.Data(), text.Size());
    }
    return CodecError::Ok;
}

CodecError NetReader::AlignToByte()
{
    const u32 pad = static_cast<u32>((8 - bit_ % 8) % 8);
    if (pad > BitsLeft())
    {
        return CodecError::Truncated;
    }
    if (Peek(bit_, pad) != 0)
    {
        return CodecError::Invalid;
    }
    bit_ += pad;
    return CodecError::Ok;
}

CodecError NetReader::ReadQuantized(f32 min, f32 max, u32 bits, f32* out)
{
    if (!ValidRange(min, max) || bits < 1 || bits > 32)
    {
        return CodecError::Invalid;
    }
    u64 level = 0;
    if (const CodecError e = ReadBits(bits, &level); e != CodecError::Ok)
    {
        return e;
    }
    return Dequantize(level, min, max, bits, out);
}

CodecError NetReader::ReadComponents(u32 count, f32 min, f32 max, u32 bits, f32* out)
{
    const usize start = bit_;
    f32 values[4] = {};
    for (u32 i = 0; i < count; ++i)
    {
        if (const CodecError e = ReadQuantized(min, max, bits, &values[i]); e != CodecError::Ok)
        {
            bit_ = start;
            return e;
        }
    }
    for (u32 i = 0; i < count; ++i)
    {
        out[i] = values[i];
    }
    return CodecError::Ok;
}

CodecError NetReader::ReadVec2(f32 min, f32 max, u32 bits, Vec2* out)
{
    f32 c[2];
    const CodecError e = ReadComponents(2, min, max, bits, c);
    if (e == CodecError::Ok)
    {
        *out = Vec2(c[0], c[1]);
    }
    return e;
}

CodecError NetReader::ReadVec3(f32 min, f32 max, u32 bits, Vec3* out)
{
    f32 c[3];
    const CodecError e = ReadComponents(3, min, max, bits, c);
    if (e == CodecError::Ok)
    {
        *out = Vec3(c[0], c[1], c[2]);
    }
    return e;
}

CodecError NetReader::ReadVec4(f32 min, f32 max, u32 bits, Vec4* out)
{
    f32 c[4];
    const CodecError e = ReadComponents(4, min, max, bits, c);
    if (e == CodecError::Ok)
    {
        *out = Vec4(c[0], c[1], c[2], c[3]);
    }
    return e;
}

CodecError NetReader::ReadQuaternion(u32 bits, Quaternion* out)
{
    if (bits < 1 || bits > 24)
    {
        return CodecError::Invalid;
    }
    if (2 + 3 * static_cast<usize>(bits) > BitsLeft())
    {
        return CodecError::Truncated;
    }
    const u64 largest = Peek(bit_, 2);
    f64 c[4] = {};
    usize at = bit_ + 2;
    volatile f64 squares = 0.0;
    for (u32 i = 0; i < 4; ++i)
    {
        if (i == largest)
        {
            continue;
        }
        f32 part = 0;
        if (const CodecError e = Dequantize(Peek(at, bits), -kQuaternionLimit, kQuaternionLimit, bits, &part);
            e != CodecError::Ok)
        {
            return e;
        }
        at += bits;
        c[i] = part;
        const volatile f64 square = c[i] * c[i];
        squares = squares + square;
    }
    const f64 rest = 1.0 - squares;
    c[largest] = rest > 0.0 ? Sqrt(rest) : 0.0;
    bit_ = at;
    *out = Quaternion(static_cast<f32>(c[0]), static_cast<f32>(c[1]), static_cast<f32>(c[2]), static_cast<f32>(c[3]));
    return CodecError::Ok;
}

CodecError NetReader::ReadAngle(u32 bits, f32* out)
{
    if (bits < 1 || bits > 24)
    {
        return CodecError::Invalid;
    }
    u64 level = 0;
    if (const CodecError e = ReadBits(bits, &level); e != CodecError::Ok)
    {
        return e;
    }
    const volatile f64 step = kTwoPi / static_cast<f64>(u64{1} << bits);
    const volatile f64 angle = static_cast<f64>(level) * step;
    *out = static_cast<f32>(angle);
    return CodecError::Ok;
}

CodecError NetReader::ReadDeltaInt(i64 baseline, i64* out)
{
    i64 diff = 0;
    if (const CodecError e = ReadSignedVarint(&diff); e != CodecError::Ok)
    {
        return e;
    }
    *out = static_cast<i64>(static_cast<u64>(baseline) + static_cast<u64>(diff));
    return CodecError::Ok;
}

CodecError NetReader::ReadDeltaQuantized(f32 baseline, f32 min, f32 max, u32 bits, f32* out)
{
    u64 base = 0;
    if (const CodecError e = Quantize(baseline, min, max, bits, &base); e != CodecError::Ok)
    {
        return e;
    }
    const usize start = bit_;
    i64 diff = 0;
    if (const CodecError e = ReadSignedVarint(&diff); e != CodecError::Ok)
    {
        return e;
    }
    // The difference is checked before it is added: a hostile value near the ends of i64 must not overflow.
    const i64 top = static_cast<i64>(QuantizedLevels(bits));
    if (diff > top || diff < -top || static_cast<i64>(base) + diff < 0 || static_cast<i64>(base) + diff > top)
    {
        bit_ = start;
        return CodecError::OutOfRange;
    }
    const i64 level = static_cast<i64>(base) + diff;
    return Dequantize(static_cast<u64>(level), min, max, bits, out);
}

}  // namespace engine_placeholder::net
