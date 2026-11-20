#include <catch2/catch_test_macros.hpp>

#include "mem/arena_allocator.h"
#include "string/grapheme.h"
#include "string/string.h"
#include "string/utf8.h"

using vse::ArenaAllocator;
using vse::CodepointCount;
using vse::GraphemeClusterCount;
using vse::GraphemeClusters;
using vse::StringBuilder;
using vse::StringView;

TEST_CASE("plain ASCII: grapheme count == codepoint count == byte count", "[string][grapheme]")
{
    StringView sv("Hello!");
    REQUIRE((GraphemeClusterCount(sv) == CodepointCount(sv) && GraphemeClusterCount(sv) == sv.Length()));
}

TEST_CASE("'e' + combining acute is one grapheme cluster", "[string][grapheme]")
{
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendCodepoint(0x0065);  // 'e'
    sb.AppendCodepoint(0x0301);  // combining acute accent
    StringView sv = sb.View();

    REQUIRE(GraphemeClusterCount(sv) == 1);
    for (StringView cluster : GraphemeClusters(sv))
    {
        REQUIRE(cluster.Length() == sv.Length());
    }
}

TEST_CASE("decomposed Hangul L+V jamo is one grapheme cluster", "[string][grapheme]")
{
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendCodepoint(0x1100);  // Hangul choseong (L) jamo
    sb.AppendCodepoint(0x1161);  // Hangul jungseong (V) jamo
    StringView sv = sb.View();

    REQUIRE(GraphemeClusterCount(sv) == 1);
    for (StringView cluster : GraphemeClusters(sv))
    {
        REQUIRE(cluster.Length() == sv.Length());
    }
}

TEST_CASE("CRLF is one grapheme cluster not two", "[string][grapheme]")
{
    StringView sv("\r\n");
    REQUIRE(GraphemeClusterCount(sv) == 1);
}

TEST_CASE("emoji ZWJ sequence (man-ZWJ-woman) is one grapheme cluster", "[string][grapheme]")
{
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendCodepoint(0x1F468);  // MAN
    sb.AppendCodepoint(0x200D);   // ZERO WIDTH JOINER
    sb.AppendCodepoint(0x1F469);  // WOMAN
    StringView sv = sb.View();

    REQUIRE(GraphemeClusterCount(sv) == 1);
}

TEST_CASE("regional-indicator pair (US flag) is one grapheme cluster", "[string][grapheme]")
{
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendCodepoint(0x1F1FA);  // REGIONAL INDICATOR SYMBOL LETTER U
    sb.AppendCodepoint(0x1F1F8);  // REGIONAL INDICATOR SYMBOL LETTER S
    StringView sv = sb.View();

    REQUIRE(GraphemeClusterCount(sv) == 1);
}

TEST_CASE("three regional indicators split into a pair + a singleton", "[string][grapheme]")
{
    ArenaAllocator arena(4096);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.AppendCodepoint(0x1F1FA);  // U
    sb.AppendCodepoint(0x1F1F8);  // S
    sb.AppendCodepoint(0x1F1EB);  // F
    StringView sv = sb.View();

    REQUIRE(GraphemeClusterCount(sv) == 2);
}
