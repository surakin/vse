#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "containers/hash_map.h"
#include "mem/heap_allocator.h"
#include "int128.h"
#include "build_defs.h"

using namespace engine_placeholder;

// The oracle is the compiler's own type, used directly (not through ENGINE_HAS_NATIVE_INT128, which the forced-portable build turns off).
#if defined(__SIZEOF_INT128__)
#define TYPES_HAVE_ORACLE 1
__extension__ typedef unsigned __int128 OracleU;
__extension__ typedef __int128 OracleI;

namespace {

OracleU Oracle(u128 v) { return (static_cast<OracleU>(v.hi) << 64) | v.lo; }
u128 FromOracle(OracleU v) { return u128::FromParts(static_cast<u64>(v >> 64), static_cast<u64>(v)); }
OracleI OracleS(i128 v) { return static_cast<OracleI>(Oracle(v.ToBits())); }
i128 FromOracleS(OracleI v) { return i128(FromOracle(static_cast<OracleU>(v))); }

struct Rng {
    u64 s;
    u64 Next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
};

// Values biased toward the edges and toward every magnitude.
OracleU RandomValue(Rng& rng) {
    switch (rng.Next() % 16) {
    case 0: return 0;
    case 1: return 1;
    case 2: return ~static_cast<OracleU>(0);
    case 3: return static_cast<OracleU>(1) << 127;
    case 4: return (static_cast<OracleU>(1) << 64) - 1;
    case 5: return static_cast<OracleU>(1) << 64;
    case 6: return (static_cast<OracleU>(1) << 127) - 1;
    default: break;
    }
    OracleU v = (static_cast<OracleU>(rng.Next()) << 64) | rng.Next();
    const unsigned shift = static_cast<unsigned>(rng.Next() % 130);
    return shift >= 128 ? 0 : v >> shift;
}

#if ENGINE_SANITIZED
constexpr long kRandomOps = 1'000'000;
#else
constexpr long kRandomOps = 10'000'000;
#endif

} // namespace

TEST_CASE("u128: the public operators match unsigned __int128 on random values", "[types][int128]") {
    Rng rng{88172645463325252ull};
    long bad = 0;
    for (long i = 0; i < kRandomOps; ++i) {
        const OracleU a = RandomValue(rng), b = RandomValue(rng);
        const u128 pa = FromOracle(a), pb = FromOracle(b);
        const u32 shift = static_cast<u32>(rng.Next() % 130);
        bad += Oracle(pa + pb) != a + b;
        bad += Oracle(pa - pb) != a - b;
        bad += Oracle(pa * pb) != a * b;
        bad += Oracle(pa << shift) != (shift >= 128 ? 0 : a << shift);
        bad += Oracle(pa >> shift) != (shift >= 128 ? 0 : a >> shift);
        bad += Oracle(pa & pb) != (a & b);
        bad += Oracle(pa | pb) != (a | b);
        bad += Oracle(pa ^ pb) != (a ^ b);
        bad += (pa < pb) != (a < b);
        bad += (pa == pb) != (a == b);
        if (b != 0) {
            bad += Oracle(pa / pb) != a / b;
            bad += Oracle(pa % pb) != a % b;
        }
    }
    CHECK(bad == 0);
}

TEST_CASE("u128: the portable multiply and divide match unsigned __int128 whatever the build uses", "[types][int128]") {
    Rng rng{1234567ull};
    long bad = 0;
    for (long i = 0; i < kRandomOps; ++i) {
        const OracleU a = RandomValue(rng), b = RandomValue(rng);
        const detail128::Parts pa{static_cast<u64>(a), static_cast<u64>(a >> 64)}, pb{static_cast<u64>(b), static_cast<u64>(b >> 64)};
        const detail128::Parts product = detail128::PortableMul(pa, pb);
        bad += ((static_cast<OracleU>(product.hi) << 64) | product.lo) != a * b;
        if (b != 0) {
            detail128::Parts q, r;
            detail128::PortableDivMod(pa, pb, &q, &r);
            bad += ((static_cast<OracleU>(q.hi) << 64) | q.lo) != a / b;
            bad += ((static_cast<OracleU>(r.hi) << 64) | r.lo) != a % b;
        }
    }
    CHECK(bad == 0);
}

TEST_CASE("u128 and i128: the conversion to f64 is correctly rounded like __int128's", "[types][int128]") {
    Rng rng{424242ull};
    int bad = 0;
    for (int i = 0; i < 200000; ++i) {
        const OracleU a = RandomValue(rng);
        bad += static_cast<f64>(FromOracle(a)) != static_cast<f64>(a);
        bad += static_cast<f64>(FromOracleS(static_cast<OracleI>(a))) != static_cast<f64>(static_cast<OracleI>(a));
    }
    CHECK(bad == 0);
    CHECK(static_cast<f64>(u128::FromParts(1, 0x8000000000000001ull)) == static_cast<f64>(Oracle(u128::FromParts(1, 0x8000000000000001ull))));
}

TEST_CASE("i128: the public operators match __int128 on random values", "[types][int128]") {
    Rng rng{99991ull};
    long bad = 0;
    for (long i = 0; i < kRandomOps / 4; ++i) {
        const OracleI a = static_cast<OracleI>(RandomValue(rng)), b = static_cast<OracleI>(RandomValue(rng));
        const i128 pa = FromOracleS(a), pb = FromOracleS(b);
        const u32 shift = static_cast<u32>(rng.Next() % 130);
        bad += OracleS(pa + pb) != static_cast<OracleI>(static_cast<OracleU>(a) + static_cast<OracleU>(b));
        bad += OracleS(pa - pb) != static_cast<OracleI>(static_cast<OracleU>(a) - static_cast<OracleU>(b));
        bad += OracleS(pa * pb) != static_cast<OracleI>(static_cast<OracleU>(a) * static_cast<OracleU>(b));
        bad += (pa < pb) != (a < b);
        bad += (pa == pb) != (a == b);
        bad += OracleS(pa >> shift) != (shift >= 128 ? (a < 0 ? static_cast<OracleI>(-1) : 0) : a >> shift);
        bad += OracleS(pa << shift) != (shift >= 128 ? 0 : static_cast<OracleI>(static_cast<OracleU>(a) << shift));
        if (b != 0 && !(a == static_cast<OracleI>(static_cast<OracleU>(1) << 127) && b == -1)) { // the minimum divided by -1 is checked on its own
            bad += OracleS(pa / pb) != a / b;
            bad += OracleS(pa % pb) != a % b;
        }
    }
    CHECK(bad == 0);
}
#endif

TEST_CASE("u128: edge values, shifts and wrapping", "[types][int128]") {
    const u128 max = u128::Max();
    CHECK(max + u128(1u) == u128());
    CHECK(u128() - u128(1u) == max);
    CHECK(-u128(1u) == max);
    CHECK(max * max == u128(1u));
    CHECK((max >> 127) == u128(1u));
    CHECK((u128(1u) << 127) == u128::FromParts(u64{1} << 63, 0));
    for (u32 count : {0u, 1u, 63u, 64u, 65u, 127u}) {
        CHECK(((u128(1u) << count) >> count) == u128(1u));
    }
    CHECK((u128(1u) << 128) == u128());
    CHECK((max >> 128) == u128());
    CHECK((max << 200) == u128());
    CHECK(u128::FromParts(1, 0) > u128::FromParts(0, ~u64{0}));
    u128 v(5u);
    CHECK(v++ == u128(5u));
    CHECK(++v == u128(7u));
    CHECK(v-- == u128(7u));
    CHECK(--v == u128(5u));
    CHECK(!u128());
    CHECK_FALSE(!u128(1u));
    CHECK(static_cast<u32>(u128::FromParts(7, 0x1'0000'0005ull)) == 5u);
    CHECK(static_cast<f64>(u128::FromParts(1, 0)) == 18446744073709551616.0);
}

TEST_CASE("u128: bit queries, byte swap and hashing", "[types][int128]") {
    CHECK(u128().CountLeadingZeros() == 128);
    CHECK(u128(1u).CountLeadingZeros() == 127);
    CHECK(u128::Max().CountLeadingZeros() == 0);
    CHECK(u128().CountTrailingZeros() == 128);
    CHECK((u128(1u) << 100).CountTrailingZeros() == 100);
    CHECK(u128::Max().PopCount() == 128);
    CHECK(u128::FromParts(3, 1).PopCount() == 3);
    CHECK(u128::FromParts(0x0102030405060708ull, 0x090a0b0c0d0e0f10ull).ByteSwap() == u128::FromParts(0x100f0e0d0c0b0a09ull, 0x0807060504030201ull));
    // Sequential values hash to distinct values (no collisions in 100k), and the HashMap specialization works.
    HashMap<u128, int, HeapAllocator> map(HeapAllocator::Default());
    for (u32 i = 0; i < 100000; ++i) map.Insert(u128(i) * u128::FromParts(1, 3), static_cast<int>(i));
    CHECK(map.Size() == 100000);
    int* found = map.Find(u128(77u) * u128::FromParts(1, 3));
    REQUIRE(found != nullptr);
    CHECK(*found == 77);
}

TEST_CASE("u128: text in bases 2, 10 and 16", "[types][int128]") {
    char buf[u128::kMaxTextBytes];
    REQUIRE(u128().ToText(buf, sizeof(buf)) == 1);
    CHECK(std::strcmp(buf, "0") == 0);
    REQUIRE(u128::Max().ToText(buf, sizeof(buf)) == 39);
    CHECK(std::strcmp(buf, "340282366920938463463374607431768211455") == 0);
    REQUIRE(u128::Max().ToText(buf, sizeof(buf), 16) == 32);
    CHECK(std::strcmp(buf, "ffffffffffffffffffffffffffffffff") == 0);
    REQUIRE(u128::Max().ToText(buf, sizeof(buf), 2) == 128);
    CHECK(buf[0] == '1');
    CHECK(std::strlen(buf) == 128);
    CHECK((u128(1u) << 64).ToText(buf, sizeof(buf)) == 20);
    CHECK(std::strcmp(buf, "18446744073709551616") == 0);

    // A buffer too small, and a bad base, give an empty string.
    char small[4] = {'x', 'x', 'x', 'x'};
    CHECK(u128(12345u).ToText(small, sizeof(small)) == 0);
    CHECK(small[0] == '\0');
    CHECK(u128(1u).ToText(buf, sizeof(buf), 1) == 0);
    CHECK(u128(1u).ToText(buf, sizeof(buf), 17) == 0);
    CHECK(u128(999u).ToText(small, 4) == 3); // exactly fits with the NUL

    // Round trips.
    for (u32 base : {2u, 10u, 16u}) {
        for (u128 v : {u128(), u128(1u), u128(255u), u128::FromParts(1, 0), u128::FromParts(0x123456789abcdefull, 0xfedcba9876543210ull), u128::Max()}) {
            const usize n = v.ToText(buf, sizeof(buf), base);
            REQUIRE(n > 0);
            u128 back;
            REQUIRE(u128::FromText(StringView(buf, n), base, &back));
            CHECK(back == v);
        }
    }
    u128 out(42u);
    CHECK(u128::FromText("DEADbeef", 16, &out));
    CHECK(out == u128(0xdeadbeefu));
    // Failures leave the output alone.
    out = u128(42u);
    CHECK_FALSE(u128::FromText("", 10, &out));
    CHECK_FALSE(u128::FromText("12a", 10, &out));
    CHECK_FALSE(u128::FromText("-1", 10, &out));
    CHECK_FALSE(u128::FromText("2", 2, &out));
    CHECK_FALSE(u128::FromText("340282366920938463463374607431768211456", 10, &out)); // 2^128
    CHECK_FALSE(u128::FromText("1ffffffffffffffffffffffffffffffff", 16, &out));
    CHECK_FALSE(u128::FromText("1", 1, &out));
    CHECK(out == u128(42u));
    CHECK(u128::FromText("340282366920938463463374607431768211455", 10, &out));
    CHECK(out == u128::Max());
}

TEST_CASE("i128: edge values, wrapping, signed division and arithmetic shifts", "[types][int128]") {
    const i128 min = i128::Min(), max = i128::Max();
    CHECK(min < max);
    CHECK(min + i128(1) > min);
    CHECK(max + i128(1) == min);          // wraps (defined here)
    CHECK(min - i128(1) == max);
    CHECK(-min == min);                   // the minimum negates to itself
    CHECK(min / i128(-1) == min);         // and divides to itself, remainder 0
    CHECK(min % i128(-1) == i128());
    CHECK(i128(-7) / i128(2) == i128(-3)); // truncates toward zero
    CHECK(i128(-7) % i128(2) == i128(-1)); // the remainder has the dividend's sign
    CHECK(i128(7) / i128(-2) == i128(-3));
    CHECK(i128(7) % i128(-2) == i128(1));
    CHECK((i128(-1) >> 1) == i128(-1));
    CHECK((i128(-1) >> 200) == i128(-1));
    CHECK((i128(5) >> 200) == i128());
    CHECK((i128(-8) >> 2) == i128(-2));
    CHECK((i128(1) << 127) == min);
    CHECK(i128(-1) < i128(0));
    CHECK(i128(0) > i128(-5));
    CHECK(i128(-5) < i128(-4));
    CHECK(i128(i8(-3)).IsNegative());
    CHECK(i128(-3).Magnitude() == u128(3u));
    CHECK(min.Magnitude() == u128(1u) << 127);
    CHECK(static_cast<f64>(i128(-3)) == -3.0);
    CHECK(i128(-1).ToBits() == u128::Max());
    CHECK(i128(u64{5}) == i128(5));
    i128 v(-2);
    CHECK(++v == i128(-1));
    CHECK(++v == i128(0));
    CHECK(--v == i128(-1));
}

TEST_CASE("i128: text", "[types][int128]") {
    char buf[i128::kMaxTextBytes];
    REQUIRE(i128::Min().ToText(buf, sizeof(buf)) == 40);
    CHECK(std::strcmp(buf, "-170141183460469231731687303715884105728") == 0);
    REQUIRE(i128::Max().ToText(buf, sizeof(buf)) == 39);
    CHECK(std::strcmp(buf, "170141183460469231731687303715884105727") == 0);
    REQUIRE(i128(-255).ToText(buf, sizeof(buf), 16) == 3);
    CHECK(std::strcmp(buf, "-ff") == 0);
    REQUIRE(i128().ToText(buf, sizeof(buf)) == 1);
    for (i128 v : {i128(), i128(1), i128(-1), i128::Min(), i128::Max(), i128(-1234567890123456789ll)}) {
        const usize n = v.ToText(buf, sizeof(buf));
        i128 back;
        REQUIRE(i128::FromText(StringView(buf, n), 10, &back));
        CHECK(back == v);
    }
    i128 out(9);
    CHECK(i128::FromText("+12", 10, &out));
    CHECK(out == i128(12));
    CHECK(i128::FromText("-80000000000000000000000000000000", 16, &out)); // the minimum
    CHECK(out == i128::Min());
    out = i128(9);
    CHECK_FALSE(i128::FromText("170141183460469231731687303715884105728", 10, &out)); // 2^127
    CHECK_FALSE(i128::FromText("-170141183460469231731687303715884105729", 10, &out));
    CHECK_FALSE(i128::FromText("-", 10, &out));
    CHECK_FALSE(i128::FromText("", 10, &out));
    CHECK_FALSE(i128::FromText("--1", 10, &out));
    CHECK(out == i128(9));
}

#ifdef ENGINE_FORCE_PORTABLE_INT128
static_assert(ENGINE_HAS_NATIVE_INT128 == 0, "the portable executable must use the portable operators");
#endif

TEST_CASE("The 128-bit types have a fixed size, alignment and layout", "[types][int128]") {
    STATIC_REQUIRE(sizeof(u128) == 16);
    STATIC_REQUIRE(alignof(u128) == 16);
    STATIC_REQUIRE(sizeof(i128) == 16);
    const u128 v = u128::FromParts(0x0102030405060708ull, 0x090a0b0c0d0e0f10ull);
    u64 words[2];
    std::memcpy(words, &v, 16);
    CHECK(words[0] == 0x090a0b0c0d0e0f10ull); // low half first
    CHECK(words[1] == 0x0102030405060708ull);
    // Constant evaluation of the parts that are constexpr.
    constexpr u128 c = u128(7u) + u128(8u);
    STATIC_REQUIRE(c == u128(15u));
    constexpr i128 n = -i128(5);
    STATIC_REQUIRE(n < i128(0));
}

TEST_CASE("u128 and i128: the three-way comparison agrees with the six operators", "[types][int128]") {
    // The comparisons with 0 are in double parentheses so Catch2 does not decompose them: MSVC rejects passing the literal 0 through
    // Catch2's operator templates to the ordering types' literal-zero comparison (C7595, found on Phase 13 Day 149).
    const u128 small = u128::FromParts(0, 1), big = u128::FromParts(1, 0);
    CHECK(((small <=> big) < 0));
    CHECK(((big <=> small) > 0));
    CHECK(((small <=> small) == 0));
    const i128 neg(i64{-5}), pos(i64{3});
    CHECK(((neg <=> pos) < 0));
    CHECK(((pos <=> neg) > 0));
    CHECK(((neg <=> neg) == 0));
}
