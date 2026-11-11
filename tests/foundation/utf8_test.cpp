#include <catch2/catch_test_macros.hpp>

#include "mem/arena_allocator.h"
#include "string/string.h"
#include "string/utf8.h"
#include "string/utf8_codec.h"
#include "types.h"

using engine_placeholder::ArenaAllocator;
using engine_placeholder::CodepointCount;
using engine_placeholder::DecodedCodepoint;
using engine_placeholder::DecodeUtf8At;
using engine_placeholder::EncodeUtf8;
using engine_placeholder::IsValidUtf8;
using engine_placeholder::StringBuilder;
using engine_placeholder::StringView;
using engine_placeholder::u32;
using engine_placeholder::usize;
using engine_placeholder::Utf8Codepoints;

TEST_CASE("EncodeUtf8/DecodeUtf8At round-trip at every length-transition boundary", "[string][utf8]")
{
    // Boundary-value round trips at every UTF-8 length transition.
    const u32 kBoundaries[] = {
        0x007F,   0x0080,   // 1 <-> 2 byte
        0x07FF,   0x0800,   // 2 <-> 3 byte
        0xFFFF,   0x10000,  // 3 <-> 4 byte
        0x10FFFF,           // max valid codepoint
    };
    bool all_round_trip_ok = true;
    for (u32 cp : kBoundaries)
    {
        char buf[4] = {};
        usize encoded_len = EncodeUtf8(cp, buf);
        bool valid = false;
        DecodedCodepoint decoded = DecodeUtf8At(buf, encoded_len, 0, &valid);
        if (!valid || decoded.codepoint != cp || decoded.byte_length != encoded_len)
        {
            all_round_trip_ok = false;
            break;
        }
    }
    REQUIRE(all_round_trip_ok);
}

TEST_CASE("Utf8Codepoints iterates mixed ASCII/2/3/4-byte content in order", "[string][utf8]")
{
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.Append("A");               // ASCII, 1 byte
    sb.AppendCodepoint(0x00E9);   // 'é', 2 bytes
    sb.AppendCodepoint(0x4E2D);   // '中', 3 bytes
    sb.AppendCodepoint(0x1F600);  // '😀', 4 bytes
    sb.Append("Z");               // ASCII, 1 byte

    StringView sv = sb.View();
    REQUIRE(CodepointCount(sv) == 5);

    const u32 expected[] = {'A', 0x00E9, 0x4E2D, 0x1F600, 'Z'};
    usize i = 0;
    bool sequence_ok = true;
    for (u32 cp : Utf8Codepoints(sv))
    {
        if (i >= 5 || cp != expected[i])
        {
            sequence_ok = false;
            break;
        }
        ++i;
    }
    REQUIRE((sequence_ok && i == 5));
}

TEST_CASE("IsValidUtf8 rejects malformed byte sequences", "[string][utf8]")
{
    SECTION("truncated lead byte at end-of-buffer")
    {
        const char truncated[] = {static_cast<char>(0xC2)};
        REQUIRE_FALSE(IsValidUtf8(StringView(truncated, 1)));
    }
    SECTION("lone continuation byte with no lead byte")
    {
        const char lone_continuation[] = {static_cast<char>(0x80)};
        REQUIRE_FALSE(IsValidUtf8(StringView(lone_continuation, 1)));
    }
    SECTION("overlong encoding of 'A' (0x41) as a 2-byte sequence")
    {
        const char overlong[] = {static_cast<char>(0xC1), static_cast<char>(0x81)};
        REQUIRE_FALSE(IsValidUtf8(StringView(overlong, 2)));
    }
    SECTION("UTF-8-encoded surrogate (an attempt to encode U+D800)")
    {
        const char surrogate[] = {static_cast<char>(0xED), static_cast<char>(0xA0), static_cast<char>(0x80)};
        REQUIRE_FALSE(IsValidUtf8(StringView(surrogate, 3)));
    }
}
