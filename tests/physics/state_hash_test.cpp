// The state hash helper (Phase 25 Day 374): FNV-1a on known inputs, the byte order, the zero and NaN rules.
#include <catch2/catch_test_macros.hpp>

#include "lang/limits.h"
#include "physics/state_hash.h"

using namespace engine_placeholder;
using namespace engine_placeholder::physics;

TEST_CASE("StateHasher: FNV-1a 64 on the published test vectors", "[physics][hash]")
{
    StateHasher empty;
    CHECK(empty.Value() == 0xcbf29ce484222325ull);
    StateHasher a;
    a.AddU8('a');
    CHECK(a.Value() == 0xaf63dc4c8601ec8cull);
    StateHasher foobar;
    const char* text = "foobar";
    for (usize i = 0; text[i] != '\0'; ++i)
    {
        foobar.AddU8(static_cast<u8>(text[i]));
    }
    CHECK(foobar.Value() == 0x85944171f73967e8ull);
}

TEST_CASE("StateHasher: words are fed least significant byte first", "[physics][hash]")
{
    StateHasher word;
    word.AddU32(0x04030201u);
    StateHasher bytes;
    for (u8 b = 1; b <= 4; ++b)
    {
        bytes.AddU8(b);
    }
    CHECK(word.Value() == bytes.Value());
    StateHasher wide;
    wide.AddU64(0x0807060504030201ull);
    for (u8 b = 5; b <= 8; ++b)
    {
        bytes.AddU8(b);
    }
    CHECK(wide.Value() == bytes.Value());
}

TEST_CASE("StateHasher: floats hash by bit pattern and negative zero is positive zero", "[physics][hash]")
{
    StateHasher one;
    one.AddF32(1.0f);
    StateHasher bits;
    bits.AddU32(0x3f800000u);
    CHECK(one.Value() == bits.Value());
    volatile f32 zero = 0.0f;
    const f32 negative_zero = -zero;
    REQUIRE(StateHasher::BitsOf(negative_zero) == 0x80000000u);  // the operand really is -0.0
    StateHasher pz;
    pz.AddF32(0.0f);
    StateHasher nz;
    nz.AddF32(negative_zero);
    CHECK(pz.Value() == nz.Value());
    CHECK(nz.Valid());
    StateHasher denormal_a;
    denormal_a.AddF32(Limits<f32>::DenormMin());
    StateHasher denormal_b;
    denormal_b.AddU32(1u);
    CHECK(denormal_a.Value() == denormal_b.Value());  // a denormal is not flushed
}

TEST_CASE("StateHasher: a NaN or an infinity makes the hash invalid", "[physics][hash]")
{
    StateHasher h;
    CHECK(h.Valid());
    h.AddF32(Limits<f32>::Infinity());
    CHECK_FALSE(h.Valid());
    StateHasher n;
    n.AddF32(Limits<f32>::QuietNaN());
    CHECK_FALSE(n.Valid());
    n.AddF32(1.0f);
    CHECK_FALSE(n.Valid());  // it stays invalid
}

TEST_CASE("StateHasher: the order of words changes the hash", "[physics][hash]")
{
    StateHasher ab;
    ab.AddU32(1);
    ab.AddU32(2);
    StateHasher ba;
    ba.AddU32(2);
    ba.AddU32(1);
    CHECK(ab.Value() != ba.Value());
}
