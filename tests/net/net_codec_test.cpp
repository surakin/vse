// The message codec (Phase 20 Day 272): bit packing, ranges, varints, quantization with its error bounds, deltas,
// golden bytes, and the behavior of every malformed input.

#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "net/codec.h"

namespace engine_placeholder::net
{
namespace
{

u64 CodecTestNext(u64* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

f64 CodecTestUnit(u64* state)
{
    return static_cast<f64>(CodecTestNext(state) >> 11) / static_cast<f64>(u64{1} << 53);
}

constexpr usize kCodecBuffer = 4096;

struct Buffer
{
    byte data[kCodecBuffer];
    MutableByteSpan Span() { return MutableByteSpan(data, kCodecBuffer); }
};

bool CodecTestSame(ByteSpan a, ByteSpan b)
{
    if (a.Size() != b.Size())
    {
        return false;
    }
    for (usize i = 0; i < a.Size(); ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

// One randomly chosen value, written and read back. The kinds exercise every width and alignment.
struct Op
{
    u32 kind = 0;
    u64 a = 0;
    u64 b = 0;
    i64 lo = 0;
    i64 hi = 0;
    u32 bits = 0;
    Vector<byte> bytes;
    f32 f = 0;
};

constexpr u32 kKinds = 12;

Op CodecTestRandomOp(u64* state)
{
    Op op;
    op.kind = static_cast<u32>(CodecTestNext(state) % kKinds);
    switch (op.kind)
    {
    case 0:  // raw bits
        op.bits = static_cast<u32>(CodecTestNext(state) % 65);
        op.a = op.bits == 0 ? 0 : (CodecTestNext(state) >> (64 - op.bits));
        break;
    case 1:  // bool
        op.a = CodecTestNext(state) & 1;
        break;
    case 2:  // ranged integer
    {
        const i64 base = static_cast<i64>(CodecTestNext(state) % 2001) - 1000;
        const u64 span = CodecTestNext(state) % 5000;
        op.lo = base;
        op.hi = base + static_cast<i64>(span);
        op.a = static_cast<u64>(op.lo + static_cast<i64>(CodecTestNext(state) % (span + 1)));
        break;
    }
    case 3:  // varint
        op.a = CodecTestNext(state) >> (CodecTestNext(state) % 64);
        break;
    case 4:  // signed varint
        op.a = CodecTestNext(state) >> (CodecTestNext(state) % 64);
        break;
    case 5:  // byte array
    {
        const usize n = CodecTestNext(state) % 40;
        for (usize i = 0; i < n; ++i)
        {
            op.bytes.Push(static_cast<byte>(CodecTestNext(state) & 0xff));
        }
        break;
    }
    case 6:  // f32 raw
    {
        const u32 pattern = static_cast<u32>(CodecTestNext(state));
        MemCopy(&op.f, &pattern, 4);
        break;
    }
    case 7:  // quantized
        op.bits = 1 + static_cast<u32>(CodecTestNext(state) % 32);
        op.f = static_cast<f32>(CodecTestUnit(state) * 200.0 - 100.0);
        break;
    case 8:  // align
        break;
    case 9:  // delta int
        op.a = CodecTestNext(state);
        op.b = CodecTestNext(state) >> (CodecTestNext(state) % 64);
        break;
    case 10:  // angle
        op.bits = 1 + static_cast<u32>(CodecTestNext(state) % 24);
        op.f = static_cast<f32>(CodecTestUnit(state) * 40.0 - 20.0);
        break;
    default:  // u32
        op.a = CodecTestNext(state) & 0xffffffffu;
        break;
    }
    return op;
}

CodecError CodecTestWrite(NetWriter& w, const Op& op)
{
    switch (op.kind)
    {
    case 0: return w.WriteBits(op.a, op.bits);
    case 1: return w.WriteBool(op.a != 0);
    case 2: return w.WriteRange(static_cast<i64>(op.a), op.lo, op.hi);
    case 3: return w.WriteVarint(op.a);
    case 4: return w.WriteSignedVarint(static_cast<i64>(op.a));
    case 5: return w.WriteBytes(ByteSpan(op.bytes.Data(), op.bytes.Size()), 64);
    case 6: return w.WriteF32(op.f);
    case 7: return w.WriteQuantized(op.f, -100.0f, 100.0f, op.bits);
    case 8: return w.AlignToByte();
    case 9: return w.WriteDeltaInt(static_cast<i64>(op.a), static_cast<i64>(op.b));
    case 10: return w.WriteAngle(op.f, op.bits);
    default: return w.WriteU32(static_cast<u32>(op.a));
    }
}

// Reads one value and compares it with what was written; returns Ok and sets *same, or the read's error.
CodecError CodecTestRead(NetReader& r, const Op& op, bool* same)
{
    *same = true;
    switch (op.kind)
    {
    case 0:
    {
        u64 v = 0;
        const CodecError e = r.ReadBits(op.bits, &v);
        *same = v == op.a;
        return e;
    }
    case 1:
    {
        bool v = false;
        const CodecError e = r.ReadBool(&v);
        *same = v == (op.a != 0);
        return e;
    }
    case 2:
    {
        i64 v = 0;
        const CodecError e = r.ReadRange(op.lo, op.hi, &v);
        *same = v == static_cast<i64>(op.a);
        return e;
    }
    case 3:
    {
        u64 v = 0;
        const CodecError e = r.ReadVarint(&v);
        *same = v == op.a;
        return e;
    }
    case 4:
    {
        i64 v = 0;
        const CodecError e = r.ReadSignedVarint(&v);
        *same = v == static_cast<i64>(op.a);
        return e;
    }
    case 5:
    {
        Vector<byte> v;
        const CodecError e = r.ReadBytes(64, &v);
        *same = CodecTestSame(ByteSpan(v.Data(), v.Size()), ByteSpan(op.bytes.Data(), op.bytes.Size()));
        return e;
    }
    case 6:
    {
        f32 v = 0;
        const CodecError e = r.ReadF32(&v);
        u32 a = 0;
        u32 b = 0;
        MemCopy(&a, &v, 4);
        MemCopy(&b, &op.f, 4);
        *same = a == b;
        return e;
    }
    case 7:
    {
        f32 v = 0;
        const CodecError e = r.ReadQuantized(-100.0f, 100.0f, op.bits, &v);
        const f64 bound = 200.0 / (2.0 * static_cast<f64>(QuantizedLevels(op.bits))) + 100.0 * 6.0e-8;
        *same = e != CodecError::Ok || Abs(static_cast<f64>(v) - static_cast<f64>(op.f)) <= bound;
        return e;
    }
    case 8: return r.AlignToByte();
    case 9:
    {
        i64 v = 0;
        const CodecError e = r.ReadDeltaInt(static_cast<i64>(op.b), &v);
        *same = v == static_cast<i64>(op.a);
        return e;
    }
    case 10:
    {
        f32 v = 0;
        const CodecError e = r.ReadAngle(op.bits, &v);
        // The distance on the circle between the decoded angle and the input.
        f64 d = static_cast<f64>(v) - static_cast<f64>(op.f);
        d = d - 6.283185307179586 * Floor(d / 6.283185307179586 + 0.5);
        const f64 bound = 3.141592653589793 / static_cast<f64>(u64{1} << op.bits) + 1.0e-5;
        *same = e != CodecError::Ok || Abs(d) <= bound;
        return e;
    }
    default:
    {
        u32 v = 0;
        const CodecError e = r.ReadU32(&v);
        *same = v == static_cast<u32>(op.a);
        return e;
    }
    }
}

}  // namespace

TEST_CASE("Mixed sequences of values round-trip", "[net][codec]")
{
    const u64 sequences = (ENGINE_SANITIZER_ADDRESS || ENGINE_SANITIZER_THREAD) ? 60000 : 1000000;
    Buffer buffer;
    u64 state = 0x243f6a8885a308d3ull;
    u64 bad = 0;
    for (u64 s = 0; s < sequences; ++s)
    {
        const u32 count = 1 + static_cast<u32>(CodecTestNext(&state) % 10);
        Op ops[10];
        NetWriter w(buffer.Span());
        bool wrote = true;
        for (u32 i = 0; i < count; ++i)
        {
            ops[i] = CodecTestRandomOp(&state);
            wrote = wrote && CodecTestWrite(w, ops[i]) == CodecError::Ok;
        }
        NetReader r(w.Bytes());
        bool all_same = wrote;
        for (u32 i = 0; i < count && all_same; ++i)
        {
            bool same = false;
            all_same = CodecTestRead(r, ops[i], &same) == CodecError::Ok && same;
        }
        if (!all_same)
        {
            ++bad;
        }
    }
    CHECK(bad == 0);
}

TEST_CASE("Every truncation of a message fails cleanly and leaves the reader where it was", "[net][codec]")
{
    Buffer buffer;
    u64 state = 77;
    for (int round = 0; round < 300; ++round)
    {
        const u32 count = 1 + static_cast<u32>(CodecTestNext(&state) % 8);
        Op ops[8];
        NetWriter w(buffer.Span());
        for (u32 i = 0; i < count; ++i)
        {
            ops[i] = CodecTestRandomOp(&state);
            REQUIRE(CodecTestWrite(w, ops[i]) == CodecError::Ok);
        }
        const ByteSpan full = w.Bytes();
        for (usize cut = 0; cut < full.Size(); ++cut)
        {
            NetReader r(ByteSpan(full.Data(), cut));
            u32 i = 0;
            for (; i < count; ++i)
            {
                bool same = false;
                const usize before = r.BitPosition();
                const CodecError e = CodecTestRead(r, ops[i], &same);
                if (e != CodecError::Ok)
                {
                    // A truncated message may also decode as a malformed one; what matters: the position is kept.
                    REQUIRE(r.BitPosition() == before);
                    break;
                }
            }
            // The cut removed at least one whole byte of data (a byte can hold only padding after an align).
            if (i == count)
            {
                REQUIRE(cut + 1 >= full.Size());
            }
        }
    }
}

TEST_CASE("Bits are packed from the low bit of the first byte", "[net][codec]")
{
    Buffer buffer;
    NetWriter w(buffer.Span());
    REQUIRE(w.WriteBits(0b101, 3) == CodecError::Ok);
    REQUIRE(w.WriteBits(0b11, 2) == CodecError::Ok);
    REQUIRE(w.WriteBool(true) == CodecError::Ok);
    CHECK(w.BitsWritten() == 6);
    CHECK(w.BytesWritten() == 1);
    CHECK(w.Bytes()[0] == byte{0b00111101});
}

TEST_CASE("A fixed-width integer at a byte boundary is little-endian", "[net][codec]")
{
    Buffer buffer;
    NetWriter w(buffer.Span());
    REQUIRE(w.WriteU32(0x01020304u) == CodecError::Ok);
    REQUIRE(w.WriteU16(0xaabb) == CodecError::Ok);
    const ByteSpan out = w.Bytes();
    REQUIRE(out.Size() == 6);
    CHECK(out[0] == byte{4});
    CHECK(out[1] == byte{3});
    CHECK(out[2] == byte{2});
    CHECK(out[3] == byte{1});
    CHECK(out[4] == byte{0xbb});
    CHECK(out[5] == byte{0xaa});
}

TEST_CASE("Golden bytes pin the format", "[net][codec]")
{
    Buffer buffer;
    NetWriter w(buffer.Span());
    REQUIRE(w.WriteBool(true) == CodecError::Ok);
    REQUIRE(w.WriteRange(7, 5, 12) == CodecError::Ok);  // 3 bits: offset 2
    REQUIRE(w.WriteBits(0x2a, 6) == CodecError::Ok);
    REQUIRE(w.AlignToByte() == CodecError::Ok);
    REQUIRE(w.WriteVarint(300) == CodecError::Ok);
    REQUIRE(w.WriteSignedVarint(-3) == CodecError::Ok);
    REQUIRE(w.WriteString("hi", 8) == CodecError::Ok);
    REQUIRE(w.WriteQuantized(0.5f, 0.0f, 1.0f, 8) == CodecError::Ok);
    REQUIRE(w.WriteAngle(3.14159265f, 8) == CodecError::Ok);
    REQUIRE(w.WriteDeltaInt(105, 100) == CodecError::Ok);
    // Worked out by hand: bit 0 is the bool, bits 1-3 the range offset 2, bits 4-9 the 6 bits 0x2a, then zero padding;
    // 300 is ac 02; zigzag(-3) is 5; the string is its length 2 and "hi"; 0.5 of 255 levels rounds up to 128; half a
    // turn of 256 steps is 128; zigzag(5) is 10.
    const byte golden[] = {byte{0xa5}, byte{0x02}, byte{0xac}, byte{0x02}, byte{0x05}, byte{0x02},
                           byte{0x68}, byte{0x69}, byte{0x80}, byte{0x80}, byte{0x0a}};
    const ByteSpan out = w.Bytes();
    INFO("size " << out.Size());
    REQUIRE(out.Size() == sizeof(golden));
    for (usize i = 0; i < sizeof(golden); ++i)
    {
        INFO("byte " << i << " is " << static_cast<int>(static_cast<u8>(out[i])));
        CHECK(out[i] == golden[i]);
    }
}

TEST_CASE("A full buffer refuses a write and keeps its position", "[net][codec]")
{
    byte small[2] = {};
    NetWriter w(MutableByteSpan(small, 2));
    REQUIRE(w.WriteBits(0x1ff, 9) == CodecError::Ok);
    CHECK(w.WriteBits(0xff, 8) == CodecError::Full);
    CHECK(w.BitsWritten() == 9);
    CHECK(w.WriteVarint(1u << 20) == CodecError::Full);
    CHECK(w.WriteBytes(ByteSpan(small, 2), 10) == CodecError::Full);
    CHECK(w.BitsWritten() == 9);
    CHECK(w.WriteBits(0x7f, 7) == CodecError::Ok);
    CHECK(w.WriteBool(true) == CodecError::Full);
}

TEST_CASE("A value that does not fit its bits or range is refused", "[net][codec]")
{
    Buffer buffer;
    NetWriter w(buffer.Span());
    CHECK(w.WriteBits(8, 3) == CodecError::OutOfRange);
    CHECK(w.WriteBits(1, 65) == CodecError::Invalid);
    CHECK(w.WriteRange(11, 5, 10) == CodecError::OutOfRange);
    CHECK(w.WriteRange(4, 5, 10) == CodecError::OutOfRange);
    CHECK(w.WriteRange(5, 10, 5) == CodecError::Invalid);
    CHECK(w.BitsWritten() == 0);
    // A range of one value takes no bits; a range of the whole i64 takes 64.
    CHECK(w.WriteRange(9, 9, 9) == CodecError::Ok);
    CHECK(w.BitsWritten() == 0);
    CHECK(w.WriteRange(-1, Limits<i64>::Min(), Limits<i64>::Max()) == CodecError::Ok);
    CHECK(w.BitsWritten() == 64);
    NetReader r(w.Bytes());
    i64 a = 0;
    i64 b = 0;
    CHECK(r.ReadRange(9, 9, &a) == CodecError::Ok);
    CHECK(a == 9);
    CHECK(r.ReadRange(Limits<i64>::Min(), Limits<i64>::Max(), &b) == CodecError::Ok);
    CHECK(b == -1);
}

TEST_CASE("A decoded range value above the maximum is refused", "[net][codec]")
{
    const byte bytes[1] = {byte{7}};  // 3 bits, offset 7, but the range 0..5 allows up to 5
    NetReader r{ByteSpan(bytes, 1)};
    i64 v = -1;
    CHECK(r.ReadRange(0, 5, &v) == CodecError::OutOfRange);
    CHECK(v == -1);
    CHECK(r.BitPosition() == 0);
    u64 bits = 0;
    CHECK(r.ReadBits(3, &bits) == CodecError::Ok);  // the reader is still usable
    CHECK(bits == 7);
}

TEST_CASE("Varints: shortest form only, 64 bits at most", "[net][codec]")
{
    const u64 values[] = {0, 1, 127, 128, 16383, 16384, 0xffffffffull, 0x7fffffffffffffffull, ~u64{0}};
    for (u64 value : values)
    {
        for (u32 offset : {0u, 3u})
        {
            Buffer buffer;
            NetWriter w(buffer.Span());
            REQUIRE(w.WriteBits(0, offset) == CodecError::Ok);
            REQUIRE(w.WriteVarint(value) == CodecError::Ok);
            NetReader r(w.Bytes());
            u64 skip = 0;
            REQUIRE(r.ReadBits(offset, &skip) == CodecError::Ok);
            u64 back = 0;
            REQUIRE(r.ReadVarint(&back) == CodecError::Ok);
            CHECK(back == value);
        }
    }
    const byte overlong[] = {byte{0x80}, byte{0x00}};  // 0 in two bytes
    const byte too_big[] = {byte{0xff}, byte{0xff}, byte{0xff}, byte{0xff}, byte{0xff},
                            byte{0xff}, byte{0xff}, byte{0xff}, byte{0xff}, byte{0x7f}};  // above 64 bits
    const byte endless[] = {byte{0x80}, byte{0x80}, byte{0x80}, byte{0x80}, byte{0x80}, byte{0x80},
                            byte{0x80}, byte{0x80}, byte{0x80}, byte{0x80}, byte{0x80}, byte{0x01}};
    const byte cut[] = {byte{0x80}};
    u64 v = 5;
    {
        NetReader r{ByteSpan(overlong, 2)};
        CHECK(r.ReadVarint(&v) == CodecError::Invalid);
        CHECK(r.BitPosition() == 0);
    }
    {
        NetReader r{ByteSpan(too_big, 10)};
        CHECK(r.ReadVarint(&v) == CodecError::Invalid);
    }
    {
        NetReader r{ByteSpan(endless, 12)};
        CHECK(r.ReadVarint(&v) == CodecError::Invalid);
    }
    {
        NetReader r{ByteSpan(cut, 1)};
        CHECK(r.ReadVarint(&v) == CodecError::Truncated);
    }
    CHECK(v == 5);
}

TEST_CASE("Signed varints keep small magnitudes short and survive the extremes", "[net][codec]")
{
    const i64 values[] = {0, -1, 1, -64, 63, -65, 64, Limits<i64>::Min(), Limits<i64>::Max()};
    for (i64 value : values)
    {
        Buffer buffer;
        NetWriter w(buffer.Span());
        REQUIRE(w.WriteSignedVarint(value) == CodecError::Ok);
        if (value >= -64 && value <= 63)
        {
            CHECK(w.BytesWritten() == 1);
        }
        NetReader r(w.Bytes());
        i64 back = 0;
        REQUIRE(r.ReadSignedVarint(&back) == CodecError::Ok);
        CHECK(back == value);
    }
}

TEST_CASE("Strings and byte arrays have a mandatory maximum, checked before anything is allocated", "[net][codec]")
{
    Buffer buffer;
    NetWriter w(buffer.Span());
    CHECK(w.WriteString("hello", 4) == CodecError::TooLong);
    CHECK(w.BitsWritten() == 0);
    REQUIRE(w.WriteString("hello", 5) == CodecError::Ok);
    NetReader r(w.Bytes());
    String<HeapAllocator> text(HeapAllocator::Default());
    CHECK(r.ReadString(4, &text) == CodecError::TooLong);
    CHECK(r.BitPosition() == 0);
    CHECK(text.Length() == 0);
    REQUIRE(r.ReadString(5, &text) == CodecError::Ok);
    CHECK(text.Length() == 5);

    // A length that promises far more than the message holds is truncated, not allocated.
    const byte lie[] = {byte{0xff}, byte{0xff}, byte{0xff}, byte{0x7f}, byte{1}, byte{2}};
    NetReader liar{ByteSpan(lie, 6)};
    Vector<byte> out;
    CHECK(liar.ReadBytes(Limits<usize>::Max(), &out) == CodecError::Truncated);
    CHECK(out.Size() == 0);
    CHECK(out.Capacity() == 0);
    CHECK(liar.BitPosition() == 0);
}

TEST_CASE("Strings and arrays work at any bit offset, empty ones too", "[net][codec]")
{
    for (u32 offset = 0; offset < 8; ++offset)
    {
        Buffer buffer;
        NetWriter w(buffer.Span());
        REQUIRE(w.WriteBits(0, offset) == CodecError::Ok);
        REQUIRE(w.WriteString("", 4) == CodecError::Ok);
        REQUIRE(w.WriteString("abc", 4) == CodecError::Ok);
        NetReader r(w.Bytes());
        u64 skip = 0;
        REQUIRE(r.ReadBits(offset, &skip) == CodecError::Ok);
        String<HeapAllocator> a(HeapAllocator::Default());
        String<HeapAllocator> b(HeapAllocator::Default());
        REQUIRE(r.ReadString(4, &a) == CodecError::Ok);
        REQUIRE(r.ReadString(4, &b) == CodecError::Ok);
        CHECK(a.Length() == 0);
        CHECK(b.Length() == 3);
    }
}

TEST_CASE("Padding bits must be zero", "[net][codec]")
{
    const byte dirty[] = {byte{0b10100001}};
    NetReader r{ByteSpan(dirty, 1)};
    bool b = false;
    REQUIRE(r.ReadBool(&b) == CodecError::Ok);
    CHECK(r.AlignToByte() == CodecError::Invalid);
    CHECK(r.BitPosition() == 1);
    const byte clean[] = {byte{0b00000001}};
    NetReader ok{ByteSpan(clean, 1)};
    REQUIRE(ok.ReadBool(&b) == CodecError::Ok);
    CHECK(ok.AlignToByte() == CodecError::Ok);
    CHECK(ok.AtEnd());
}

TEST_CASE("Quantization error stays within its bound over a dense sweep", "[net][codec]")
{
    const f32 mins[] = {0.0f, -1.0f, -100.0f, 3.0f};
    const f32 maxs[] = {1.0f, 1.0f, 100.0f, 3.5f};
    for (u32 bits : {1u, 2u, 3u, 5u, 8u, 12u, 16u, 20u, 24u, 32u})
    {
        for (int range = 0; range < 4; ++range)
        {
            const f32 lo = mins[range];
            const f32 hi = maxs[range];
            const f64 levels = static_cast<f64>(QuantizedLevels(bits));
            const f64 bound = (static_cast<f64>(hi) - lo) / (2.0 * levels) +
                              6.0e-8 * FMax(Abs(static_cast<f64>(lo)), Abs(static_cast<f64>(hi)));
            const int samples = bits <= 12 ? 20000 : 4000;
            for (int i = 0; i <= samples; ++i)
            {
                const f32 v = static_cast<f32>(lo + (static_cast<f64>(hi) - lo) * i / samples);
                u64 level = 0;
                REQUIRE(Quantize(v, lo, hi, bits, &level) == CodecError::Ok);
                REQUIRE(level <= QuantizedLevels(bits));
                f32 back = 0;
                REQUIRE(Dequantize(level, lo, hi, bits, &back) == CodecError::Ok);
                REQUIRE(Abs(static_cast<f64>(back) - v) <= bound);
            }
            // The ends are exact levels.
            u64 level = 0;
            REQUIRE(Quantize(lo, lo, hi, bits, &level) == CodecError::Ok);
            CHECK(level == 0);
            REQUIRE(Quantize(hi, lo, hi, bits, &level) == CodecError::Ok);
            CHECK(level == QuantizedLevels(bits));
        }
    }
}

TEST_CASE("NaN is rejected, infinities clamp, minus zero is zero", "[net][codec]")
{
    u64 level = 77;
    const f32 nan = Limits<f32>::QuietNaN();
    const f32 inf = Limits<f32>::Infinity();
    CHECK(Quantize(nan, -1.0f, 1.0f, 8, &level) == CodecError::Invalid);
    CHECK(level == 77);
    CHECK(Quantize(inf, -1.0f, 1.0f, 8, &level) == CodecError::Ok);
    CHECK(level == 255);
    CHECK(Quantize(-inf, -1.0f, 1.0f, 8, &level) == CodecError::Ok);
    CHECK(level == 0);
    CHECK(Quantize(5.0f, -1.0f, 1.0f, 8, &level) == CodecError::Ok);
    CHECK(level == 255);
    u64 zero = 0;
    u64 minus_zero = 1;
    REQUIRE(Quantize(0.0f, -1.0f, 1.0f, 8, &zero) == CodecError::Ok);
    REQUIRE(Quantize(-0.0f, -1.0f, 1.0f, 8, &minus_zero) == CodecError::Ok);
    CHECK(zero == minus_zero);
    // Bad parameters.
    CHECK(Quantize(0.0f, 1.0f, 1.0f, 8, &level) == CodecError::Invalid);
    CHECK(Quantize(0.0f, 2.0f, 1.0f, 8, &level) == CodecError::Invalid);
    CHECK(Quantize(0.0f, -inf, 1.0f, 8, &level) == CodecError::Invalid);
    CHECK(Quantize(0.0f, -1.0f, 1.0f, 0, &level) == CodecError::Invalid);
    CHECK(Quantize(0.0f, -1.0f, 1.0f, 33, &level) == CodecError::Invalid);
    f32 back = 0;
    CHECK(Dequantize(256, -1.0f, 1.0f, 8, &back) == CodecError::OutOfRange);
    // A NaN refused by the writer leaves the buffer untouched.
    Buffer buffer;
    NetWriter w(buffer.Span());
    CHECK(w.WriteQuantized(nan, -1.0f, 1.0f, 8) == CodecError::Invalid);
    CHECK(w.WriteVec3(Vec3(0.0f, nan, 0.0f), -1.0f, 1.0f, 8) == CodecError::Invalid);
    CHECK(w.BitsWritten() == 0);
}

TEST_CASE("Quantization gives the same levels on every compiler: fixed expectations", "[net][codec]")
{
    // Values chosen near the rounding edges; the expected levels were worked out by hand in exact arithmetic.
    struct Case
    {
        f32 value;
        f32 min;
        f32 max;
        u32 bits;
        u64 level;
    };
    const Case cases[] = {
        {0.5f, 0.0f, 1.0f, 1, 1},       // 0.5 * 1 = 0.5: halves round up
        {0.25f, 0.0f, 1.0f, 2, 1},      // 0.25 * 3 = 0.75
        {0.125f, 0.0f, 1.0f, 2, 0},     // 0.375
        {0.375f, 0.0f, 1.0f, 2, 1},     // 1.125
        {0.5f, 0.0f, 1.0f, 2, 2},       // 1.5: up
        {-0.5f, -1.0f, 1.0f, 3, 2},     // 0.25 * 7 = 1.75
        {0.0f, -1.0f, 1.0f, 3, 4},      // 0.5 * 7 = 3.5: up
        {1.0f, 0.0f, 100.0f, 8, 3},     // 0.01 * 255 = 2.55
        {99.0f, 0.0f, 100.0f, 8, 252},  // 252.45
    };
    for (const Case& c : cases)
    {
        u64 level = 0;
        REQUIRE(Quantize(c.value, c.min, c.max, c.bits, &level) == CodecError::Ok);
        INFO("value " << c.value << " bits " << c.bits);
        CHECK(level == c.level);
    }
}

TEST_CASE("Vectors quantize component by component", "[net][codec]")
{
    Buffer buffer;
    NetWriter w(buffer.Span());
    REQUIRE(w.WriteVec2(Vec2(1.5f, -2.5f), -10.0f, 10.0f, 12) == CodecError::Ok);
    REQUIRE(w.WriteVec3(Vec3(1.0f, 2.0f, 3.0f), -10.0f, 10.0f, 16) == CodecError::Ok);
    REQUIRE(w.WriteVec4(Vec4(-1.0f, 0.0f, 9.0f, 10.0f), -10.0f, 10.0f, 10) == CodecError::Ok);
    CHECK(w.BitsWritten() == 2 * 12 + 3 * 16 + 4 * 10);
    NetReader r(w.Bytes());
    Vec2 a;
    Vec3 b;
    Vec4 c;
    REQUIRE(r.ReadVec2(-10.0f, 10.0f, 12, &a) == CodecError::Ok);
    REQUIRE(r.ReadVec3(-10.0f, 10.0f, 16, &b) == CodecError::Ok);
    REQUIRE(r.ReadVec4(-10.0f, 10.0f, 10, &c) == CodecError::Ok);
    const f32 e12 = 20.0f / (2.0f * 4095.0f) + 1.0e-6f;
    CHECK(Abs(a.x - 1.5f) <= e12);
    CHECK(Abs(a.y + 2.5f) <= e12);
    CHECK(Abs(b.z - 3.0f) <= 20.0f / (2.0f * 65535.0f) + 1.0e-6f);
    CHECK(Abs(c.w - 10.0f) <= 1.0e-6f);
    // A truncated vector keeps the reader where it was.
    NetReader cut(ByteSpan(w.Bytes().Data(), 3));
    Vec3 none(7.0f, 7.0f, 7.0f);
    CHECK(cut.ReadVec3(-10.0f, 10.0f, 16, &none) == CodecError::Truncated);
    CHECK(none.x == 7.0f);
    CHECK(cut.BitPosition() == 0);
}

TEST_CASE("Rotations: smallest three stays within its documented bound", "[net][codec]")
{
    u64 state = 4242;
    for (u32 bits : {4u, 8u, 10u, 16u, 24u})
    {
        const f64 bound = 4.0 * 1.4142135623730951 / (2.0 * static_cast<f64>(QuantizedLevels(bits))) + 2.0e-6;
        f64 worst = 0;
        for (int i = 0; i < 20000; ++i)
        {
            Quaternion q(
                static_cast<f32>(CodecTestUnit(&state) * 2 - 1), static_cast<f32>(CodecTestUnit(&state) * 2 - 1),
                static_cast<f32>(CodecTestUnit(&state) * 2 - 1), static_cast<f32>(CodecTestUnit(&state) * 2 - 1));
            if (q.Length() < 0.01f)
            {
                continue;
            }
            Buffer buffer;
            NetWriter w(buffer.Span());
            REQUIRE(w.WriteQuaternion(q, bits) == CodecError::Ok);
            CHECK(w.BitsWritten() == 2 + 3 * bits);
            NetReader r(w.Bytes());
            Quaternion back;
            REQUIRE(r.ReadQuaternion(bits, &back) == CodecError::Ok);
            const Quaternion n = q.Normalized();
            // q and -q are the same rotation: compare with the sign that matches.
            const f64 dot = static_cast<f64>(n.x) * back.x + static_cast<f64>(n.y) * back.y +
                            static_cast<f64>(n.z) * back.z + static_cast<f64>(n.w) * back.w;
            const f64 s = dot < 0 ? -1.0 : 1.0;
            const f64 dx = n.x - s * back.x;
            const f64 dy = n.y - s * back.y;
            const f64 dz = n.z - s * back.z;
            const f64 dw = n.w - s * back.w;
            const f64 error = Sqrt(dx * dx + dy * dy + dz * dz + dw * dw);
            worst = FMax(worst, error);
            REQUIRE(error <= bound);
        }
        INFO("bits " << bits << " worst " << worst);
    }
}

TEST_CASE("Rotations: identity, axes and bad input", "[net][codec]")
{
    Buffer buffer;
    NetWriter w(buffer.Span());
    REQUIRE(w.WriteQuaternion(Quaternion::Identity(), 12) == CodecError::Ok);
    REQUIRE(w.WriteQuaternion(Quaternion(0, 0, 0, -1), 12) == CodecError::Ok);  // the same rotation
    NetReader r(w.Bytes());
    Quaternion a;
    Quaternion b;
    REQUIRE(r.ReadQuaternion(12, &a) == CodecError::Ok);
    REQUIRE(r.ReadQuaternion(12, &b) == CodecError::Ok);
    CHECK(Abs(a.w - 1.0f) <= 1.0e-5f);
    CHECK(Abs(b.w - 1.0f) <= 1.0e-5f);
    const usize before = w.BitsWritten();
    CHECK(w.WriteQuaternion(Quaternion(0, 0, 0, 0), 12) == CodecError::Invalid);
    CHECK(w.WriteQuaternion(Quaternion(Limits<f32>::QuietNaN(), 0, 0, 1), 12) == CodecError::Invalid);
    CHECK(w.WriteQuaternion(Quaternion(Limits<f32>::Infinity(), 0, 0, 1), 12) == CodecError::Invalid);
    CHECK(w.WriteQuaternion(Quaternion::Identity(), 0) == CodecError::Invalid);
    CHECK(w.WriteQuaternion(Quaternion::Identity(), 25) == CodecError::Invalid);
    CHECK(w.BitsWritten() == before);
    // A scaled quaternion is normalized first.
    REQUIRE(w.WriteQuaternion(Quaternion(0, 0, 0, 50), 12) == CodecError::Ok);
}

TEST_CASE("Angles wrap and stay within half a step", "[net][codec]")
{
    for (u32 bits : {1u, 4u, 8u, 12u, 16u, 24u})
    {
        const f64 bound = 3.141592653589793 / static_cast<f64>(u64{1} << bits) + 2.0e-6;
        for (int i = -2000; i <= 2000; ++i)
        {
            const f32 angle = static_cast<f32>(i) * 0.0173f;  // about -34.6 to 34.6 radians
            Buffer buffer;
            NetWriter w(buffer.Span());
            REQUIRE(w.WriteAngle(angle, bits) == CodecError::Ok);
            NetReader r(w.Bytes());
            f32 back = 0;
            REQUIRE(r.ReadAngle(bits, &back) == CodecError::Ok);
            REQUIRE(back >= 0.0f);
            REQUIRE(back <= 6.2831860f);
            f64 d = static_cast<f64>(back) - angle;
            d = d - 6.283185307179586 * Floor(d / 6.283185307179586 + 0.5);
            REQUIRE(Abs(d) <= bound);
        }
    }
    Buffer buffer;
    NetWriter w(buffer.Span());
    CHECK(w.WriteAngle(Limits<f32>::QuietNaN(), 8) == CodecError::Invalid);
    CHECK(w.WriteAngle(Limits<f32>::Infinity(), 8) == CodecError::Invalid);
    CHECK(w.WriteAngle(1.0f, 0) == CodecError::Invalid);
    CHECK(w.BitsWritten() == 0);
}

TEST_CASE("Deltas against a baseline", "[net][codec]")
{
    Buffer buffer;
    NetWriter w(buffer.Span());
    REQUIRE(w.WriteDeltaInt(1005, 1000) == CodecError::Ok);
    CHECK(w.BytesWritten() == 1);                                                        // a small change is one byte
    REQUIRE(w.WriteDeltaInt(Limits<i64>::Min(), Limits<i64>::Max()) == CodecError::Ok);  // wraps without loss
    REQUIRE(w.WriteDeltaQuantized(0.52f, 0.5f, 0.0f, 1.0f, 10) == CodecError::Ok);
    NetReader r(w.Bytes());
    i64 a = 0;
    i64 b = 0;
    f32 c = 0;
    REQUIRE(r.ReadDeltaInt(1000, &a) == CodecError::Ok);
    REQUIRE(r.ReadDeltaInt(Limits<i64>::Max(), &b) == CodecError::Ok);
    REQUIRE(r.ReadDeltaQuantized(0.5f, 0.0f, 1.0f, 10, &c) == CodecError::Ok);
    CHECK(a == 1005);
    CHECK(b == Limits<i64>::Min());
    CHECK(Abs(c - 0.52f) <= 1.0f / (2.0f * 1023.0f) + 1.0e-6f);
    // A difference that leads outside the levels is rejected and the reader stays put.
    Buffer other;
    NetWriter w2(other.Span());
    REQUIRE(w2.WriteSignedVarint(5000) == CodecError::Ok);
    NetReader r2(w2.Bytes());
    f32 out = -1.0f;
    CHECK(r2.ReadDeltaQuantized(0.5f, 0.0f, 1.0f, 10, &out) == CodecError::OutOfRange);
    CHECK(r2.BitPosition() == 0);
    CHECK(out == -1.0f);
    // The ends of i64 are refused too (the sum must not be formed before the check).
    for (const i64 extreme : {Limits<i64>::Max(), Limits<i64>::Min(), Limits<i64>::Max() - 1})
    {
        Buffer hostile;
        NetWriter w3(hostile.Span());
        REQUIRE(w3.WriteSignedVarint(extreme) == CodecError::Ok);
        NetReader r3(w3.Bytes());
        CHECK(r3.ReadDeltaQuantized(1.0f, 0.0f, 1.0f, 32, &out) == CodecError::OutOfRange);
        CHECK(r3.BitPosition() == 0);
    }
}

TEST_CASE("Raw floats keep every bit pattern", "[net][codec]")
{
    Buffer buffer;
    NetWriter w(buffer.Span());
    const f32 values[] = {0.0f, -0.0f, 1.5f, Limits<f32>::Infinity(), Limits<f32>::QuietNaN(), Limits<f32>::Min()};
    for (f32 v : values)
    {
        REQUIRE(w.WriteF32(v) == CodecError::Ok);
    }
    REQUIRE(w.WriteF64(-2.25) == CodecError::Ok);
    NetReader r(w.Bytes());
    for (f32 v : values)
    {
        f32 back = 1.0f;
        REQUIRE(r.ReadF32(&back) == CodecError::Ok);
        u32 x = 0;
        u32 y = 0;
        MemCopy(&x, &v, 4);
        MemCopy(&y, &back, 4);
        CHECK(x == y);
    }
    f64 d = 0;
    REQUIRE(r.ReadF64(&d) == CodecError::Ok);
    CHECK(d == -2.25);
}

}  // namespace engine_placeholder::net
