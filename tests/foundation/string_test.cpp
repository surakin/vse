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
