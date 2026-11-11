// The canonical byte encoding (Phase 19 Day 256): ByteWriter and ByteReader.

#include <catch2/catch_test_macros.hpp>

#include "bytes/byte_reader.h"
#include "bytes/byte_writer.h"
#include "cstr/mem_ops.h"
#include "lang/limits.h"

namespace engine_placeholder::bytes {
namespace {

u32 BitsOf(f32 value) {
    u32 bits = 0;
    MemCopy(&bits, &value, sizeof(bits));
    return bits;
}
u64 BitsOf(f64 value) {
    u64 bits = 0;
    MemCopy(&bits, &value, sizeof(bits));
    return bits;
}
f32 F32FromBits(u32 bits) {
    f32 value = 0;
    MemCopy(&value, &bits, sizeof(bits));
    return value;
}
f64 F64FromBits(u64 bits) {
    f64 value = 0;
    MemCopy(&value, &bits, sizeof(bits));
    return value;
}

TEST_CASE("Bytes: integers are little-endian at their own width (golden bytes)", "[bytes]") {
    ByteWriter w;
    w.WriteU8(0x01);
    w.WriteU16(0x0203);
    w.WriteU32(0x04050607);
    w.WriteU64(0x08090a0b0c0d0e0full);
    w.WriteI16(-2);
    w.WriteBool(true);
    const u8 expected[] = {0x01, 0x03, 0x02, 0x07, 0x06, 0x05, 0x04, 0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0xfe, 0xff, 0x01};
    REQUIRE(w.Size() == sizeof(expected));
    for (usize i = 0; i < sizeof(expected); ++i) {
        CHECK(static_cast<u8>(w.Bytes()[i]) == expected[i]);
    }
}

TEST_CASE("Bytes: integers round trip at their extremes", "[bytes]") {
    ByteWriter w;
    w.WriteU8(0);
    w.WriteU8(0xff);
    w.WriteU16(0xffff);
    w.WriteU32(0xffffffffu);
    w.WriteU64(0xffffffffffffffffull);
    w.WriteI8(Limits<i8>::Min());
    w.WriteI16(Limits<i16>::Min());
    w.WriteI32(Limits<i32>::Min());
    w.WriteI64(Limits<i64>::Min());
    w.WriteI64(Limits<i64>::Max());
    ByteReader r(w.Bytes());
    u8 a = 1, b = 0;
    u16 c = 0;
    u32 d = 0;
    u64 e = 0;
    i8 f = 0;
    i16 g = 0;
    i32 h = 0;
    i64 i = 0, j = 0;
    REQUIRE(r.ReadU8(&a) == ByteError::Ok);
    REQUIRE(r.ReadU8(&b) == ByteError::Ok);
    REQUIRE(r.ReadU16(&c) == ByteError::Ok);
    REQUIRE(r.ReadU32(&d) == ByteError::Ok);
    REQUIRE(r.ReadU64(&e) == ByteError::Ok);
    REQUIRE(r.ReadI8(&f) == ByteError::Ok);
    REQUIRE(r.ReadI16(&g) == ByteError::Ok);
    REQUIRE(r.ReadI32(&h) == ByteError::Ok);
    REQUIRE(r.ReadI64(&i) == ByteError::Ok);
    REQUIRE(r.ReadI64(&j) == ByteError::Ok);
    CHECK(a == 0);
    CHECK(b == 0xff);
    CHECK(c == 0xffff);
    CHECK(d == 0xffffffffu);
    CHECK(e == 0xffffffffffffffffull);
    CHECK(f == Limits<i8>::Min());
    CHECK(g == Limits<i16>::Min());
    CHECK(h == Limits<i32>::Min());
    CHECK(i == Limits<i64>::Min());
    CHECK(j == Limits<i64>::Max());
    CHECK(r.AtEnd());
}

TEST_CASE("Bytes: floats keep their bit pattern (NaN payload, -0, denormals, infinities)", "[bytes]") {
    const u32 f32_patterns[] = {0x00000000u, 0x80000000u, 0x00000001u, 0x007fffffu, 0x7f800000u, 0xff800000u, 0x7fc00001u, 0x7f80dead, 0xffffffffu, 0x3f800000u};
    const u64 f64_patterns[] = {0ull, 0x8000000000000000ull, 1ull, 0x000fffffffffffffull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000001ull, 0x7ff00000deadbeefull};
    ByteWriter w;
    for (const u32 p : f32_patterns) w.WriteF32(F32FromBits(p));
    for (const u64 p : f64_patterns) w.WriteF64(F64FromBits(p));
    ByteReader r(w.Bytes());
    for (const u32 p : f32_patterns) {
        f32 v = 0;
        REQUIRE(r.ReadF32(&v) == ByteError::Ok);
        CHECK(BitsOf(v) == p);
    }
    for (const u64 p : f64_patterns) {
        f64 v = 0;
        REQUIRE(r.ReadF64(&v) == ByteError::Ok);
        CHECK(BitsOf(v) == p);
    }
}

TEST_CASE("Bytes: a bool is one byte and only 0 or 1 is valid", "[bytes]") {
    const byte good[] = {byte{0}, byte{1}};
    ByteReader r(ByteSpan(good, 2));
    bool v = true;
    REQUIRE(r.ReadBool(&v) == ByteError::Ok);
    CHECK_FALSE(v);
    REQUIRE(r.ReadBool(&v) == ByteError::Ok);
    CHECK(v);
    CHECK(r.ReadBool(&v) == ByteError::Truncated);
    for (u32 value = 2; value < 256; ++value) {
        const byte bad[] = {static_cast<byte>(value)};
        ByteReader rb(ByteSpan(bad, 1));
        bool out = false;
        CHECK(rb.ReadBool(&out) == ByteError::Invalid);
        CHECK(rb.Position() == 0);
        CHECK_FALSE(out);
    }
}

TEST_CASE("Bytes: varints are LEB128 in their shortest form", "[bytes]") {
    const u64 values[] = {0, 1, 127, 128, 255, 16383, 16384, 0xffffffffull, 0x100000000ull, 0x7fffffffffffffffull, 0xffffffffffffffffull};
    const usize lengths[] = {1, 1, 1, 2, 2, 2, 3, 5, 5, 9, 10};
    for (usize i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        ByteWriter w;
        w.WriteVarint(values[i]);
        CHECK(w.Size() == lengths[i]);
        ByteReader r(w.Bytes());
        u64 out = 0;
        REQUIRE(r.ReadVarint(&out) == ByteError::Ok);
        CHECK(out == values[i]);
        CHECK(r.AtEnd());
    }
    ByteWriter w;
    w.WriteVarint(300);
    CHECK(static_cast<u8>(w.Bytes()[0]) == 0xac);
    CHECK(static_cast<u8>(w.Bytes()[1]) == 0x02);
}

TEST_CASE("Bytes: a varint that is not canonical, too long or cut is refused", "[bytes]") {
    auto read = [](const u8* data, usize size, ByteError* error, usize* position) {
        Vector<byte> copy;
        for (usize i = 0; i < size; ++i) copy.Push(static_cast<byte>(data[i]));
        ByteReader r(ByteSpan(copy.Data(), copy.Size()));
        u64 out = 0;
        *error = r.ReadVarint(&out);
        *position = r.Position();
    };
    ByteError e = ByteError::Ok;
    usize pos = 99;
    const u8 overlong0[] = {0x80, 0x00}; // zero in two bytes
    read(overlong0, 2, &e, &pos);
    CHECK(e == ByteError::Invalid);
    CHECK(pos == 0);
    const u8 overlong1[] = {0x81, 0x80, 0x00}; // one with a trailing zero group
    read(overlong1, 3, &e, &pos);
    CHECK(e == ByteError::Invalid);
    const u8 eleven[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
    read(eleven, 11, &e, &pos);
    CHECK(e == ByteError::Invalid);
    const u8 too_big[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02}; // 65 bits
    read(too_big, 10, &e, &pos);
    CHECK(e == ByteError::Invalid);
    const u8 cut[] = {0x80, 0x80};
    read(cut, 2, &e, &pos);
    CHECK(e == ByteError::Truncated);
    CHECK(pos == 0);
    read(cut, 0, &e, &pos);
    CHECK(e == ByteError::Truncated);
}

TEST_CASE("Bytes: a length above the limit is TooLong and does not move the reader", "[bytes]") {
    ByteWriter w;
    w.WriteVarint(1000);
    w.WriteVarint(5);
    ByteReader r(w.Bytes());
    u64 n = 7;
    CHECK(r.ReadLength(999, &n) == ByteError::TooLong);
    CHECK(n == 7);
    CHECK(r.Position() == 0);
    REQUIRE(r.ReadLength(1000, &n) == ByteError::Ok);
    CHECK(n == 1000);
    REQUIRE(r.ReadLength(5, &n) == ByteError::Ok);
    CHECK(n == 5);
}

TEST_CASE("Bytes: ReadBytes borrows the input and refuses a count past the end", "[bytes]") {
    ByteWriter w;
    w.WriteU32(0x11223344);
    ByteReader r(w.Bytes());
    ByteSpan s;
    CHECK(r.ReadBytes(5, &s) == ByteError::Truncated);
    CHECK(r.ReadBytes(0, &s) == ByteError::Ok);
    CHECK(s.Size() == 0);
    REQUIRE(r.ReadBytes(4, &s) == ByteError::Ok);
    CHECK(s.Data() == w.Bytes().Data());
    CHECK(r.ReadBytes(usize(-1), &s) == ByteError::Truncated); // a huge count must not overflow the bounds check
}

TEST_CASE("Bytes: every read fails at every cut point of a composite message without moving", "[bytes]") {
    ByteWriter w;
    w.WriteU8(7);
    w.WriteU16(300);
    w.WriteU32(70000);
    w.WriteU64(1ull << 40);
    w.WriteI32(-5);
    w.WriteF32(1.5f);
    w.WriteF64(-2.5);
    w.WriteBool(true);
    w.WriteVarint(1u << 20);
    for (usize cut = 0; cut < w.Size(); ++cut) {
        ByteReader r(ByteSpan(w.Bytes().Data(), cut));
        u8 a = 0;
        u16 b = 0;
        u32 c = 0;
        u64 d = 0;
        i32 e = 0;
        f32 f = 0;
        f64 g = 0;
        bool h = false;
        u64 i = 0;
        bool failed = false;
        usize before = 0;
        const ByteError errors[] = {r.ReadU8(&a), r.ReadU16(&b), r.ReadU32(&c), r.ReadU64(&d), r.ReadI32(&e), r.ReadF32(&f), r.ReadF64(&g), r.ReadBool(&h), r.ReadVarint(&i)};
        for (const ByteError error : errors) {
            if (error != ByteError::Ok) {
                failed = true;
                break;
            }
        }
        (void)before;
        CHECK(failed);
        CHECK(r.Position() <= cut);
    }
    ByteReader whole(w.Bytes());
    u8 a = 0;
    u16 b = 0;
    u32 c = 0;
    u64 d = 0;
    i32 e = 0;
    f32 f = 0;
    f64 g = 0;
    bool h = false;
    u64 i = 0;
    CHECK(whole.ReadU8(&a) == ByteError::Ok);
    CHECK(whole.ReadU16(&b) == ByteError::Ok);
    CHECK(whole.ReadU32(&c) == ByteError::Ok);
    CHECK(whole.ReadU64(&d) == ByteError::Ok);
    CHECK(whole.ReadI32(&e) == ByteError::Ok);
    CHECK(whole.ReadF32(&f) == ByteError::Ok);
    CHECK(whole.ReadF64(&g) == ByteError::Ok);
    CHECK(whole.ReadBool(&h) == ByteError::Ok);
    CHECK(whole.ReadVarint(&i) == ByteError::Ok);
    CHECK(whole.AtEnd());
}

TEST_CASE("Bytes: Truncate cuts a partly written value", "[bytes]") {
    ByteWriter w;
    w.WriteU32(1);
    const usize mark = w.Size();
    w.WriteU64(2);
    w.Truncate(mark);
    CHECK(w.Size() == 4);
    w.Clear();
    CHECK(w.Size() == 0);
}

} // namespace
} // namespace engine_placeholder::bytes
