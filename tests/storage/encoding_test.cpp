// The component encoding (Phase 19 Day 256): every field kind round trips bit-exact, the bytes are golden, and every
// bad input is a Status.

#include <catch2/catch_test_macros.hpp>

#include "cstr/mem_ops.h"
#include "lang/limits.h"
#include "storage/component_registry.h"

namespace vse::storage
{
namespace
{

enum class Mode : u8
{
    A = 0,
    B = 1,
    C = 2
};
enum class Wide : i32
{
    Low = -5,
    High = 5
};

using Text = String<HeapAllocator>;

struct Everything
{
    u8 u8v = 0;
    u16 u16v = 0;
    u32 u32v = 0;
    u64 u64v = 0;
    i8 i8v = 0;
    i16 i16v = 0;
    i32 i32v = 0;
    i64 i64v = 0;
    f32 f32v = 0;
    f64 f64v = 0;
    bool flag = false;
    Vec2 v2;
    Vec3 v3;
    Vec4 v4;
    Quaternion q;
    Mat4 m;
    Aabb box;
    Mode mode = Mode::A;
    Wide wide = Wide::Low;
    Text name{HeapAllocator::Default()};
    Array<u16, 3> counts{};
    Array<Mode, 2> modes{};
    EntityRef ref;
};

struct Golden
{
    u16 a = 0;
    i32 b = 0;
    f32 c = 0;
    bool d = false;
    Mode mode = Mode::A;
    Text s{HeapAllocator::Default()};
    Array<u8, 2> arr{};
};

constexpr FieldOptions kModeRange = FieldOptions::Enum(0, 2);

Status RegisterAll(ComponentRegistry* r)
{
    ENGINE_TRY(r->Register<Everything>({.id = 1001,
                                        .name = "everything",
                                        .fields = {Field<&Everything::u8v>("u8"),
                                                   Field<&Everything::u16v>("u16"),
                                                   Field<&Everything::u32v>("u32"),
                                                   Field<&Everything::u64v>("u64"),
                                                   Field<&Everything::i8v>("i8"),
                                                   Field<&Everything::i16v>("i16"),
                                                   Field<&Everything::i32v>("i32"),
                                                   Field<&Everything::i64v>("i64"),
                                                   Field<&Everything::f32v>("f32"),
                                                   Field<&Everything::f64v>("f64"),
                                                   Field<&Everything::flag>("flag"),
                                                   Field<&Everything::v2>("v2"),
                                                   Field<&Everything::v3>("v3"),
                                                   Field<&Everything::v4>("v4"),
                                                   Field<&Everything::q>("q"),
                                                   Field<&Everything::m>("m"),
                                                   Field<&Everything::box>("box"),
                                                   Field<&Everything::mode>("mode", kModeRange),
                                                   Field<&Everything::wide>("wide", FieldOptions::Enum(-5, 5)),
                                                   Field<&Everything::name>("name", FieldOptions::Text(64)),
                                                   Field<&Everything::counts>("counts"),
                                                   Field<&Everything::modes>("modes", kModeRange),
                                                   Field<&Everything::ref>("ref")}}));
    return r->Register<Golden>(
        {.id = 1002,
         .name = "golden",
         .version = 3,
         .oldest_version = 3,
         .fields = {Field<&Golden::a>("a"), Field<&Golden::b>("b"), Field<&Golden::c>("c"), Field<&Golden::d>("d"),
                    Field<&Golden::mode>("mode", kModeRange), Field<&Golden::s>("s"), Field<&Golden::arr>("arr")}});
}

u32 BitsOf(f32 v)
{
    u32 b = 0;
    MemCopy(&b, &v, sizeof(b));
    return b;
}
u64 BitsOf(f64 v)
{
    u64 b = 0;
    MemCopy(&b, &v, sizeof(b));
    return b;
}
f32 F32(u32 bits)
{
    f32 v = 0;
    MemCopy(&v, &bits, sizeof(v));
    return v;
}
f64 F64(u64 bits)
{
    f64 v = 0;
    MemCopy(&v, &bits, sizeof(v));
    return v;
}

Everything Extreme()
{
    Everything e;
    e.u8v = 0xff;
    e.u16v = 0xffff;
    e.u32v = 0xffffffffu;
    e.u64v = 0xffffffffffffffffull;
    e.i8v = Limits<i8>::Min();
    e.i16v = Limits<i16>::Min();
    e.i32v = Limits<i32>::Min();
    e.i64v = Limits<i64>::Min();
    e.f32v = F32(0x7fc00123u);            // a NaN with a payload
    e.f64v = F64(0x8000000000000000ull);  // -0
    e.flag = true;
    e.v2 = Vec2(F32(0x00000001u), F32(0x80000000u));  // the smallest denormal, -0
    e.v3 = Vec3(F32(0x7f800000u), F32(0xff800000u), 1.5f);
    e.v4 = Vec4(1, 2, 3, F32(0x7f80beefu));
    e.q = Quaternion(0.1f, 0.2f, 0.3f, 0.4f);
    for (usize c = 0; c < 4; ++c)
    {
        e.m.col[c] = Vec4(static_cast<f32>(c), static_cast<f32>(c + 1), F32(0x007fffffu), -0.0f);
    }
    e.box.min = Vec3(-1, -2, -3);
    e.box.max = Vec3(4, 5, 6);
    e.mode = Mode::C;
    e.wide = Wide::High;
    e.name = Text(HeapAllocator::Default(), StringView("h\xc3\xa9llo \xe2\x82\xac \xf0\x9f\x98\x80 with\0nul", 24));
    e.counts[0] = 1;
    e.counts[1] = 0xffff;
    e.counts[2] = 7;
    e.modes[0] = Mode::B;
    e.modes[1] = Mode::C;
    e.ref.key.high = 0x0123456789abcdefull;
    e.ref.key.low = 0xfedcba9876543210ull;
    return e;
}

TEST_CASE("ByteWriter: WriteBytes of a view of its own bytes appends them as they were, also when the buffer grows",
          "[storage][encoding]")
{
    bytes::ByteWriter w;
    for (u32 i = 0; i < 4; ++i)
    {
        w.WriteU8(static_cast<u8>(i + 1));
    }
    for (u32 round = 0; round < 12; ++round)
    {  // doubles the content each time, so the buffer reallocates
        const usize before = w.Size();
        w.WriteBytes(w.Bytes());
        REQUIRE(w.Size() == before * 2);
        for (usize i = 0; i < before; ++i)
        {
            REQUIRE(w.Bytes()[before + i] == w.Bytes()[i]);
        }
    }
    CHECK(w.Bytes()[0] == 1);
    CHECK(w.Bytes()[3] == 4);
}

TEST_CASE("Encoding: every field kind round trips bit-exact", "[storage][encoding]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterAll(&registry).Ok());
    const RegisteredComponent* c = registry.FindById(1001);
    REQUIRE(c != nullptr);
    const Everything original = Extreme();
    bytes::ByteWriter w;
    REQUIRE(c->Encode(&original, w, CodecLimits()).Ok());

    Everything back;
    bytes::ByteReader r(w.Bytes());
    REQUIRE(c->Decode(r, &back, CodecLimits()).Ok());
    CHECK(r.AtEnd());
    CHECK(back.u8v == 0xff);
    CHECK(back.u16v == 0xffff);
    CHECK(back.u32v == 0xffffffffu);
    CHECK(back.u64v == 0xffffffffffffffffull);
    CHECK(back.i8v == Limits<i8>::Min());
    CHECK(back.i16v == Limits<i16>::Min());
    CHECK(back.i32v == Limits<i32>::Min());
    CHECK(back.i64v == Limits<i64>::Min());
    CHECK(BitsOf(back.f32v) == 0x7fc00123u);
    CHECK(BitsOf(back.f64v) == 0x8000000000000000ull);
    CHECK(back.flag);
    CHECK(BitsOf(back.v2.x) == 1u);
    CHECK(BitsOf(back.v2.y) == 0x80000000u);
    CHECK(BitsOf(back.v3.x) == 0x7f800000u);
    CHECK(BitsOf(back.v3.y) == 0xff800000u);
    CHECK(BitsOf(back.v4.w) == 0x7f80beefu);
    CHECK(back.q.w == 0.4f);
    CHECK(BitsOf(back.m.col[3].z) == 0x007fffffu);
    CHECK(BitsOf(back.m.col[2].w) == 0x80000000u);
    CHECK(back.box.max.z == 6.0f);
    CHECK(back.mode == Mode::C);
    CHECK(back.wide == Wide::High);
    CHECK(back.name.View() == original.name.View());
    CHECK(back.counts[1] == 0xffff);
    CHECK(back.modes[0] == Mode::B);
    CHECK(back.ref.key == original.ref.key);

    // The strongest check: the decoded value encodes to the same bytes.
    bytes::ByteWriter again;
    REQUIRE(c->Encode(&back, again, CodecLimits()).Ok());
    REQUIRE(again.Size() == w.Size());
    CHECK(MemCompare(again.Bytes().Data(), w.Bytes().Data(), w.Size()) == 0);
}

TEST_CASE("Encoding: the bytes of a fixed component are golden", "[storage][encoding]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterAll(&registry).Ok());
    const RegisteredComponent* c = registry.FindById(1002);
    REQUIRE(c != nullptr);
    CHECK(c->Version() == 3);
    Golden g;
    g.a = 0x1234;
    g.b = -2;
    g.c = 1.0f;
    g.d = true;
    g.mode = Mode::B;
    g.s = Text(HeapAllocator::Default(), StringView("hi"));
    g.arr[0] = 9;
    g.arr[1] = 8;
    bytes::ByteWriter w;
    REQUIRE(c->Encode(&g, w, CodecLimits()).Ok());
    // A change to this string is a change of the stored format: bump the format version and record the reason in the
    // day file.
    const u8 golden[] = {0x34, 0x12,              // a
                         0xfe, 0xff, 0xff, 0xff,  // b
                         0x00, 0x00, 0x80, 0x3f,  // c = 1.0f
                         0x01,                    // d
                         0x01,                    // mode
                         0x02, 'h',  'i',         // s
                         0x02, 0x09, 0x08};       // arr: count, elements
    REQUIRE(w.Size() == sizeof(golden));
    for (usize i = 0; i < sizeof(golden); ++i)
    {
        INFO("byte " << i);
        CHECK(static_cast<u8>(w.Bytes()[i]) == golden[i]);
    }
}

TEST_CASE("Encoding: every cut of a valid encoding is Corrupt, never a crash", "[storage][encoding]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterAll(&registry).Ok());
    const RegisteredComponent* c = registry.FindById(1001);
    const Everything original = Extreme();
    bytes::ByteWriter w;
    REQUIRE(c->Encode(&original, w, CodecLimits()).Ok());
    for (usize cut = 0; cut < w.Size(); ++cut)
    {
        Everything out;
        bytes::ByteReader r(ByteSpan(w.Bytes().Data(), cut));
        const Status s = c->Decode(r, &out, CodecLimits());
        INFO("cut " << cut);
        REQUIRE(s.Is(ErrorCode::Corrupt));
        CHECK(r.Position() <= cut);
    }
}

TEST_CASE("Encoding: values outside their declared form are refused on read", "[storage][encoding]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterAll(&registry).Ok());
    const RegisteredComponent* c = registry.FindById(1002);
    auto decode = [&](const Vector<byte>& bytes_in, const CodecLimits& limits)
    {
        Golden g;
        bytes::ByteReader r(ByteSpan(bytes_in.Data(), bytes_in.Size()));
        return c->Decode(r, &g, limits);
    };
    auto good = [&]()
    {
        Vector<byte> v;
        const u8 raw[] = {0x34, 0x12, 0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x80,
                          0x3f, 0x01, 0x01, 0x02, 'h',  'i',  0x02, 0x09, 0x08};
        for (const u8 b : raw)
        {
            v.Push(static_cast<byte>(b));
        }
        return v;
    };
    CHECK(decode(good(), CodecLimits()).Ok());
    {  // a bool that is neither 0 nor 1
        Vector<byte> v = good();
        v[10] = byte{2};
        CHECK(decode(v, CodecLimits()).Is(ErrorCode::Corrupt));
    }
    {  // an enum above its range
        Vector<byte> v = good();
        v[11] = byte{3};
        CHECK(decode(v, CodecLimits()).Is(ErrorCode::Corrupt));
    }
    {  // text that is not UTF-8
        Vector<byte> v = good();
        v[13] = byte{0xff};
        CHECK(decode(v, CodecLimits()).Is(ErrorCode::Corrupt));
    }
    {  // an array whose count is not the type's
        Vector<byte> v = good();
        v[15] = byte{3};
        CHECK(decode(v, CodecLimits()).Is(ErrorCode::Corrupt));
    }
    {  // a string length above the limit, and a length of a huge varint
        CodecLimits tight;
        tight.max_string_bytes = 1;
        CHECK(decode(good(), tight).Is(ErrorCode::LimitExceeded));
        Vector<byte> v = good();
        v[12] = byte{0xff};  // the length becomes a long varint that runs into the next bytes
        const Status s = decode(v, CodecLimits());
        CHECK(s.Failed());
    }
    {  // an array count above the element limit
        CodecLimits tight;
        tight.max_array_elements = 1;
        CHECK(decode(good(), tight).Is(ErrorCode::LimitExceeded));
    }
}

TEST_CASE("Encoding: a value that cannot be stored is refused on write and leaves the writer as it was",
          "[storage][encoding]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterAll(&registry).Ok());
    const RegisteredComponent* c = registry.FindById(1001);
    bytes::ByteWriter w;
    w.WriteU32(0xdeadbeef);
    {  // enum outside the range
        Everything e;
        e.mode = static_cast<Mode>(9);
        CHECK(c->Encode(&e, w, CodecLimits()).Is(ErrorCode::Invalid));
        CHECK(w.Size() == 4);
    }
    {  // a string longer than the field's bound
        Everything e;
        e.name = Text(HeapAllocator::Default(),
                      StringView("0123456789012345678901234567890123456789012345678901234567890123456789"));
        CHECK(c->Encode(&e, w, CodecLimits()).Is(ErrorCode::LimitExceeded));
        CHECK(w.Size() == 4);
    }
    {  // a string that is not UTF-8
        Everything e;
        e.name = Text(HeapAllocator::Default(), StringView("\xc3\x28"));
        CHECK(c->Encode(&e, w, CodecLimits()).Is(ErrorCode::Invalid));
        CHECK(w.Size() == 4);
    }
    {  // an array over the element limit
        CodecLimits tight;
        tight.max_array_elements = 2;
        Everything e;
        CHECK(c->Encode(&e, w, tight).Is(ErrorCode::LimitExceeded));
        CHECK(w.Size() == 4);
    }
}

TEST_CASE("Encoding: integers hold their extremes through the field codecs", "[storage][encoding]")
{
    struct Ints
    {
        i64 a = 0;
        u64 b = 0;
        i32 c = 0;
    };
    ComponentRegistry registry;
    REQUIRE(registry
                .Register<Ints>({.id = 2000,
                                 .name = "ints",
                                 .fields = {Field<&Ints::a>("a"), Field<&Ints::b>("b"), Field<&Ints::c>("c")}})
                .Ok());
    const RegisteredComponent* c = registry.FindById(2000);
    const i64 as[] = {0, 1, -1, Limits<i64>::Min(), Limits<i64>::Max()};
    for (const i64 a : as)
    {
        Ints in;
        in.a = a;
        in.b = static_cast<u64>(a);
        in.c = static_cast<i32>(a);
        bytes::ByteWriter w;
        REQUIRE(c->Encode(&in, w, CodecLimits()).Ok());
        CHECK(w.Size() == 20);
        Ints out;
        bytes::ByteReader r(w.Bytes());
        REQUIRE(c->Decode(r, &out, CodecLimits()).Ok());
        CHECK(out.a == in.a);
        CHECK(out.b == in.b);
        CHECK(out.c == in.c);
    }
}

}  // namespace
}  // namespace vse::storage
