// ParseDoubleExact's contract on every platform, and (Phase 26 Day 300) the parser macOS uses in place of
// std::from_chars, which Apple's libc++ lacks for double: detail::ParseDoubleWithoutFromChars. On Linux both exist, so
// they are compared on every input here; on macOS ParseDoubleExact is the replacement, so the contract cases and the
// shortest round trip test it directly.
#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"
#include "lang/limits.h"
#include "lang/scalar.h"

using namespace engine_placeholder;

namespace
{

u64 Bits(f64 value)
{
    u64 bits = 0;
    MemCopy(&bits, &value, sizeof bits);
    return bits;
}

f64 FromBits(u64 bits)
{
    f64 value = 0;
    MemCopy(&value, &bits, sizeof value);
    return value;
}

u64 NextRandom(u64& state)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

struct Case
{
    const char* text;
    bool ok;
    i64 end;  // characters consumed
    f64 value;
};

// Built at run time: a digit string longer than any buffer a parser might copy into, and the sticky-digit cases that
// only a correct treatment of digits beyond the 780th gets right.
struct LongText
{
    char text[2200];
};

LongText Repeat(const char* head, char fill, int count, const char* tail)
{
    LongText t{};
    usize n = 0;
    for (const char* p = head; *p != '\0'; ++p)
    {
        t.text[n++] = *p;
    }
    for (int i = 0; i < count; ++i)
    {
        t.text[n++] = fill;
    }
    for (const char* p = tail; *p != '\0'; ++p)
    {
        t.text[n++] = *p;
    }
    t.text[n] = '\0';
    return t;
}

constexpr f64 kInf = Limits<f64>::Infinity();

const Case kCases[] = {
    {"12.5e1x", true, 6, 125.0},
    {"1.", true, 2, 1.0},
    {".5", true, 2, 0.5},
    {"-.5", true, 3, -0.5},
    {".", false, 0, 0.0},
    {"-", false, 0, 0.0},
    {"", false, 0, 0.0},
    {"+1", false, 0, 0.0},
    {" 1", false, 0, 0.0},
    {"abc", false, 0, 0.0},
    {"1e", true, 1, 1.0},
    {"1e+", true, 1, 1.0},
    {"1e+5x", true, 4, 1e5},
    {"1E-2", true, 4, 0.01},
    {"0x10", true, 1, 0.0},
    {"0x1p3", true, 1, 0.0},
    {"007", true, 3, 7.0},
    {"0.000", true, 5, 0.0},
    {"-0", true, 2, -0.0},
    {"inf", true, 3, kInf},
    {"INFINITY", true, 8, kInf},
    {"infin", true, 3, kInf},
    {"-Inf", true, 4, -kInf},
    {"1e400", false, 5, 0.0},
    {"-1e400", false, 6, 0.0},
    {"1e-400", false, 6, 0.0},
    {"4.9406564584124654e-324", true, 23, 4.9406564584124654e-324},  // the smallest subnormal
    {"2.4703282292062328e-324", true, 23, 4.9406564584124654e-324},  // just above half of it: rounds up
    {"2.4703282292062327e-324", false, 23, 0.0},                     // just below half: rounds to zero, out of range
    {"1.7976931348623157e308", true, 22, 1.7976931348623157e308},    // the largest double
    {"1.7976931348623159e308", false, 22, 0.0},                      // rounds to infinity
    {"9007199254740993", true, 16, 9007199254740992.0},              // 2^53 + 1: halfway, ties to even
    {"9007199254740995", true, 16, 9007199254740996.0},              // halfway, ties to even (up)
    {"1e999999999999999999", false, 20, 0.0},                        // an exponent far beyond any range
    {"0e999999999999999999", true, 20, 0.0},
};

}  // namespace

TEST_CASE("ParseDoubleExact follows the from_chars contract on the edge cases", "[cstr]")
{
    for (const Case& c : kCases)
    {
        INFO(c.text);
        const ParseDoubleResult r = ParseDoubleExact(c.text, c.text + CStrLength(c.text));
        CHECK(r.ok == c.ok);
        CHECK(r.end - c.text == c.end);
        CHECK(Bits(r.value) == Bits(c.value));
    }
    for (const char* text : {"nan", "NaN", "nan(abc_1)", "-nan", "nan(", "nan(ab"})
    {
        INFO(text);
        const ParseDoubleResult r = ParseDoubleExact(text, text + CStrLength(text));
        CHECK(r.ok);
        CHECK(IsNaN(r.value));
        CHECK(SignBit(r.value) == (text[0] == '-'));
    }
    const char* const nan_seq = "nan(abc_1)rest";
    CHECK(ParseDoubleExact(nan_seq, nan_seq + 14).end == nan_seq + 10);
    const char* const nan_open = "nan(ab";
    CHECK(ParseDoubleExact(nan_open, nan_open + 6).end == nan_open + 3);
}

TEST_CASE("ParseDoubleExact keeps digits beyond the 780th and reads text without a terminator", "[cstr]")
{
    // 1 followed by 1,000 zeros, times 10^-1000: exactly 1.
    const LongText one = Repeat("1", '0', 1000, "e-1000");
    ParseDoubleResult r = ParseDoubleExact(one.text, one.text + CStrLength(one.text));
    CHECK(r.ok);
    CHECK(r.value == 1.0);
    CHECK(r.end == one.text + CStrLength(one.text));

    // 2^53 + 1 is halfway between two doubles and ties to even (down); any nonzero digit after 800 zeros puts it above
    // halfway (up).
    const LongText above = Repeat("9007199254740993.", '0', 800, "1");
    r = ParseDoubleExact(above.text, above.text + CStrLength(above.text));
    CHECK(r.ok);
    CHECK(r.value == 9007199254740994.0);
    // A sticky digit stands for what lies below the last kept digit, so it must stay there: 1 + 10^-801 is 1.0,
    // never 1.1.
    const LongText tiny_above_one = Repeat("1.", '0', 800, "1");
    r = ParseDoubleExact(tiny_above_one.text, tiny_above_one.text + CStrLength(tiny_above_one.text));
    CHECK(r.ok);
    CHECK(r.value == 1.0);
    const LongText exact = Repeat("9007199254740993.", '0', 800, "");
    r = ParseDoubleExact(exact.text, exact.text + CStrLength(exact.text));
    CHECK(r.value == 9007199254740992.0);

    // Leading zeros move the point: 0.(399 zeros)1 times 10^400 is 1.
    const LongText small = Repeat("0.", '0', 399, "1e400");
    r = ParseDoubleExact(small.text, small.text + CStrLength(small.text));
    CHECK(r.ok);
    CHECK(r.value == 1.0);

    // A point ten million digits away and an exponent of eight digits that cancel exactly: 1.0. Neither may be cut
    // short while the other is kept (Phase 26 Day 300 review: the exponent stopped growing past 1,000,000, so this read
    // as 0.1).
    static char far[10000016];
    usize n = 0;
    far[n++] = '0';
    far[n++] = '.';
    for (int i = 0; i < 10000000; ++i)
    {
        far[n++] = '0';
    }
    for (const char* tail = "1e10000001"; *tail != '\0'; ++tail)
    {
        far[n++] = *tail;
    }
    r = ParseDoubleExact(far, far + n);
    CHECK(r.ok);
    CHECK(r.value == 1.0);
    CHECK(r.end == far + n);

    // The range ends where the caller says, not at a terminator: "12345" read as "123".
    const char* const digits = "12345";
    r = ParseDoubleExact(digits, digits + 3);
    CHECK(r.value == 123.0);
    CHECK(r.end == digits + 3);
}

TEST_CASE("ParseDoubleExact reads back every shortest and every 40-digit form exactly", "[cstr]")
{
    u64 state = 0x9E3779B97F4A7C15ull;
    int mismatches = 0;
    char failed[64] = "";
    for (int i = 0; i < 200000; ++i)
    {
        f64 value = FromBits(NextRandom(state));
        if (IsNaN(value) || IsInf(value))
        {
            continue;
        }
        if (i % 4 == 1)
        {
            value = FromBits(NextRandom(state) & 0x000FFFFFFFFFFFFFull);  // subnormals
        }
        char text[64];
        const FormatShortestResult shortest = FormatShortest(text, text + sizeof text, value);
        REQUIRE(shortest.ok);
        const ParseDoubleResult r = ParseDoubleExact(text, shortest.end);
        char long_text[80];
        const int n = FormatTo(long_text, sizeof long_text, "%.40e", value);
        const ParseDoubleResult l = ParseDoubleExact(long_text, long_text + n);
        // A nonzero value read back as zero would be out of range; the shortest form of every finite double reads back
        // as it.
        if (!r.ok || r.end != shortest.end || Bits(r.value) != Bits(value) || !l.ok || Bits(l.value) != Bits(value))
        {
            if (mismatches++ == 0)
            {
                MemCopy(failed, text, static_cast<usize>(shortest.end - text));
            }
        }
    }
    INFO("first mismatch: " << failed);
    CHECK(mismatches == 0);
}

#if !ENGINE_PLATFORM_WINDOWS && !ENGINE_PLATFORM_MACOS && !ENGINE_PLATFORM_IOS
namespace
{
// The same result: ok, end and value (bit for bit; any NaN equals any NaN of the same sign).
bool Same(const ParseDoubleResult& a, const ParseDoubleResult& b)
{
    if (a.ok != b.ok || a.end != b.end)
    {
        return false;
    }
    if (IsNaN(a.value) || IsNaN(b.value))
    {
        return IsNaN(a.value) && IsNaN(b.value) && SignBit(a.value) == SignBit(b.value);
    }
    return Bits(a.value) == Bits(b.value);
}
}  // namespace

TEST_CASE("The parser macOS uses gives what std::from_chars gives (Linux)", "[cstr]")
{
    // Every contract case, and random strings over the characters the grammar cares about, through both parsers.
    for (const Case& c : kCases)
    {
        INFO(c.text);
        const usize n = CStrLength(c.text);
        CHECK(Same(detail::ParseDoubleWithoutFromChars(c.text, c.text + n), ParseDoubleExact(c.text, c.text + n)));
    }
    const char alphabet[] = "0123456789000.eE+-xinfaINFANy()_ ";
    u64 state = 0xD1B54A32D192ED03ull;
    int mismatches = 0;
    char failed[40] = "";
    for (int i = 0; i < 300000; ++i)
    {
        char text[32];
        const usize n = 1 + NextRandom(state) % 24;
        for (usize k = 0; k < n; ++k)
        {
            text[k] = alphabet[NextRandom(state) % (sizeof alphabet - 1)];
        }
        // Bias toward numbers: most strings start with a digit or a sign and a digit.
        if (i % 2 == 0)
        {
            text[0] = static_cast<char>('0' + NextRandom(state) % 10);
        }
        if (!Same(detail::ParseDoubleWithoutFromChars(text, text + n), ParseDoubleExact(text, text + n)))
        {
            if (mismatches++ == 0)
            {
                MemCopy(failed, text, n);
            }
        }
    }
    INFO("first mismatch: " << failed);
    CHECK(mismatches == 0);
}
#endif
