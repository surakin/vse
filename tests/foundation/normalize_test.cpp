#include <catch2/catch_test_macros.hpp>

#include "mem/arena_allocator.h"
#include "string/normalize.h"
#include "string/string.h"
#include "string/utf8.h"
#include "types.h"

using engine_placeholder::ArenaAllocator;
using engine_placeholder::CodepointCount;
using engine_placeholder::NormalizeNFC;
using engine_placeholder::NormalizeNFD;
using engine_placeholder::String;
using engine_placeholder::StringBuilder;
using engine_placeholder::StringView;
using engine_placeholder::u32;
using engine_placeholder::usize;
using engine_placeholder::Utf8Codepoints;

namespace
{

u32 NthCodepoint(StringView sv, usize n)
{
    usize i = 0;
    for (u32 cp : Utf8Codepoints(sv))
    {
        if (i == n)
        {
            return cp;
        }
        ++i;
    }
    return 0;
}

}  // namespace

TEST_CASE("NFD decomposes precomposed 'é' into 'e' + combining acute", "[string][normalize]")
{
    ArenaAllocator src_arena(4096);
    StringBuilder<ArenaAllocator> sb(src_arena);
    sb.AppendCodepoint(0x00E9);  // precomposed 'é'
    StringView source = sb.View();

    ArenaAllocator out_arena(4096);
    String<ArenaAllocator> nfd = NormalizeNFD(out_arena, source);

    REQUIRE(CodepointCount(nfd.View()) == 2);
    REQUIRE((NthCodepoint(nfd.View(), 0) == 0x0065 && NthCodepoint(nfd.View(), 1) == 0x0301));
}

TEST_CASE("NFC composes 'e' + combining acute into precomposed 'é'", "[string][normalize]")
{
    ArenaAllocator src_arena(4096);
    StringBuilder<ArenaAllocator> sb(src_arena);
    sb.AppendCodepoint(0x0065);  // 'e'
    sb.AppendCodepoint(0x0301);  // combining acute
    StringView source = sb.View();

    ArenaAllocator out_arena(4096);
    String<ArenaAllocator> nfc = NormalizeNFC(out_arena, source);

    REQUIRE((CodepointCount(nfc.View()) == 1 && NthCodepoint(nfc.View(), 0) == 0x00E9));
}

TEST_CASE("NFD/NFC round-trip and idempotency hold for mixed precomposed/decomposed input", "[string][normalize]")
{
    ArenaAllocator src_arena(4096);
    StringBuilder<ArenaAllocator> sb(src_arena);
    sb.Append("caf");
    sb.AppendCodepoint(0x00E9);  // precomposed 'é'
    sb.Append(" ");
    sb.AppendCodepoint(0x0065);  // already-decomposed 'e' + combining acute
    sb.AppendCodepoint(0x0301);
    StringView source = sb.View();

    ArenaAllocator out_arena(1024 * 1024);
    String<ArenaAllocator> nfc1 = NormalizeNFC(out_arena, source);
    String<ArenaAllocator> nfd1 = NormalizeNFD(out_arena, source);
    String<ArenaAllocator> nfd_of_nfc1 = NormalizeNFD(out_arena, nfc1.View());
    String<ArenaAllocator> nfc_of_nfc1 = NormalizeNFC(out_arena, nfc1.View());
    String<ArenaAllocator> nfd_of_nfd1 = NormalizeNFD(out_arena, nfd1.View());

    REQUIRE(nfd_of_nfc1.View() == nfd1.View());
    REQUIRE(nfc_of_nfc1.View() == nfc1.View());
    REQUIRE(nfd_of_nfd1.View() == nfd1.View());
}

TEST_CASE("NFD(U+AC00) decomposes via the algorithmic Hangul path and NFC recomposes it", "[string][normalize]")
{
    ArenaAllocator src_arena(4096);
    StringBuilder<ArenaAllocator> sb(src_arena);
    sb.AppendCodepoint(0xAC00);  // precomposed Hangul syllable GA
    StringView source = sb.View();

    ArenaAllocator out_arena(4096);
    String<ArenaAllocator> nfd = NormalizeNFD(out_arena, source);
    REQUIRE(CodepointCount(nfd.View()) == 2);
    REQUIRE((NthCodepoint(nfd.View(), 0) == 0x1100 && NthCodepoint(nfd.View(), 1) == 0x1161));

    String<ArenaAllocator> nfc = NormalizeNFC(out_arena, nfd.View());
    REQUIRE(nfc.View() == source);
}

TEST_CASE("composition-excluded U+0958 does not recompose from its decomposition", "[string][normalize]")
{
    // U+0958 (DEVANAGARI LETTER QA) canonically decomposes to U+0915 +
    // U+093C but is on the composition-exclusions list, so NFC must not
    // recompose it back.
    ArenaAllocator src_arena(4096);
    StringBuilder<ArenaAllocator> sb(src_arena);
    sb.AppendCodepoint(0x0958);
    StringView source = sb.View();

    ArenaAllocator out_arena(4096);
    String<ArenaAllocator> nfd = NormalizeNFD(out_arena, source);
    REQUIRE(CodepointCount(nfd.View()) == 2);
    REQUIRE((NthCodepoint(nfd.View(), 0) == 0x0915 && NthCodepoint(nfd.View(), 1) == 0x093C));

    String<ArenaAllocator> nfc = NormalizeNFC(out_arena, nfd.View());
    REQUIRE((nfc.View() == nfd.View() && nfc.View() != source));
}

TEST_CASE("Canonical ordering of a long run of combining marks is stable and the same as for a short one",
          "[normalize]")
{
    // Container audit Day 11: a run longer than 32 marks is ordered by a counting sort (it was an insertion sort,
    // quadratic on hostile input). U+0316 has class 220, U+0301 class 230.
    using namespace engine_placeholder;
    ArenaAllocator arena(1 << 20);
    Vector<u32, ArenaAllocator> codepoints(arena);
    codepoints.Push(0x61);  // a starter
    const u32 n = 5000;
    for (u32 i = 0; i < n; ++i)
    {
        codepoints.Push(i % 3 == 0 ? 0x0316u : 0x0301u);
    }
    Detail_CanonicalOrder(codepoints);
    REQUIRE(codepoints.Size() == n + 1);
    CHECK(codepoints[0] == 0x61);
    const u32 low = (n + 2) / 3;  // the class-220 marks: i = 0, 3, 6, ...
    for (u32 i = 1; i <= n; ++i)
    {
        CHECK(codepoints[i] == (i <= low ? 0x0316u : 0x0301u));
    }
}
