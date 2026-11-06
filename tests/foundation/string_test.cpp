#include <catch2/catch_test_macros.hpp>

#include "mem/arena_allocator.h"
#include "os/os.h"
#include "string/string.h"
#include "test_helpers.h"
#include "types.h"

using engine_placeholder::ArenaAllocator;
using engine_placeholder::String;
using engine_placeholder::StringBuilder;
using engine_placeholder::StringView;
using engine_placeholder::test::BytesEqual;
using engine_placeholder::u8;
using engine_placeholder::os::Write;

TEST_CASE("StringView: equality, comparison and substring", "[string][stringview]") {
    StringView a("hello");
    StringView b("hello");
    StringView c("world");
    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(a.Compare(c) < 0);
    REQUIRE(c.Compare(a) > 0);

    StringView sub = a.Substring(1, 3); // "ell"
    StringView expected_sub("ell");
    REQUIRE(sub == expected_sub);
}

TEST_CASE("String: Append builds expected content and stays null-terminated", "[string][string]") {
    ArenaAllocator arena(4096);
    String<ArenaAllocator> s(arena);
    REQUIRE(s.Length() == 0);

    s.Append(StringView("hello"));
    s.Append(' ');
    s.Append(StringView("world"));
    StringView expected("hello world");
    REQUIRE(s.View() == expected);
    REQUIRE(s.CStr()[s.Length()] == '\0');
}

TEST_CASE("StringBuilder: formats ints/floats/strings byte-for-byte correctly", "[string][string_builder]") {
    ArenaAllocator arena(1024 * 1024);
    StringBuilder<ArenaAllocator> sb(arena);
    sb.Append("answer=");
    sb.AppendInt(-42);
    sb.Append(" count=");
    sb.AppendUInt(7);
    sb.Append(" pi=");
    sb.AppendFloat(3.25, 2);
    sb.Append(" name=");
    sb.Append("engine");

    const char expected[] = "answer=-42 count=7 pi=3.25 name=engine";
    StringView actual = sb.View();
    bool matches = actual.Length() == sizeof(expected) - 1 &&
                   BytesEqual(reinterpret_cast<const u8*>(actual.Data()),
                              reinterpret_cast<const u8*>(expected), actual.Length());
    REQUIRE(matches);

    // Write the formatted result via os::Write, demonstrating the same
    // buffer already verified above going out through the Day 2 wrapper.
    Write(1, actual.Data(), actual.Length());
    const char newline[] = "\n";
    Write(1, newline, 1);
}

TEST_CASE("StringBuilder::AppendHex formats lowercase hex with optional zero padding", "[string]") {
    using namespace engine_placeholder;
    // Each call gets its own arena; the result is compared by value.
    auto hex = [](u64 v, int digits) {
        static ArenaAllocator arena(1 << 16);
        StringBuilder<ArenaAllocator> sb(arena);
        sb.AppendHex(v, digits);
        return sb.View();
    };
    REQUIRE(hex(0, 1) == StringView("0"));
    REQUIRE(hex(9, 1) == StringView("9"));
    REQUIRE(hex(10, 1) == StringView("a"));
    REQUIRE(hex(16, 1) == StringView("10"));   // mouse mask 16 is 0x10, not "16"
    REQUIRE(hex(0x1f, 1) == StringView("1f"));
    REQUIRE(hex(0xdeadbeefULL, 1) == StringView("deadbeef"));
    REQUIRE(hex(0xffffffffffffffffULL, 1) == StringView("ffffffffffffffff"));
    REQUIRE(hex(5, 4) == StringView("0005"));
    REQUIRE(hex(0, 3) == StringView("000"));
}

TEST_CASE("StringView: LastIndexOf, StartsWith, EndsWith and open-ended Substring", "[string][stringview]") {
    StringView a("a/b/c");
    REQUIRE(a.LastIndexOf('/') == 3);
    REQUIRE(a.LastIndexOf('x') == a.Length());
    REQUIRE(StringView().LastIndexOf('/') == 0);
    REQUIRE(a.StartsWith("a/b"));
    REQUIRE(a.StartsWith(""));
    REQUIRE_FALSE(a.StartsWith("a/b/c/d"));
    REQUIRE_FALSE(a.StartsWith("b"));
    REQUIRE(a.EndsWith("b/c"));
    REQUIRE(a.EndsWith(""));
    REQUIRE_FALSE(a.EndsWith("xa/b/c"));
    REQUIRE(a.Substring(2) == StringView("b/c"));
    REQUIRE(a.Substring(a.Length()).Empty());
}
