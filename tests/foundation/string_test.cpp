#include <catch2/catch_test_macros.hpp>

#include "mem/arena_allocator.h"
#include "os/os.h"
#include "string/string.h"
#include "test_helpers.h"
#include "types.h"

using vse::ArenaAllocator;
using vse::String;
using vse::StringBuilder;
using vse::StringView;
using vse::u8;
using vse::os::Write;
using vse::test::BytesEqual;

TEST_CASE("StringView: equality, comparison and substring", "[string][stringview]")
{
    StringView a("hello");
    StringView b("hello");
    StringView c("world");
    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(a.Compare(c) < 0);
    REQUIRE(c.Compare(a) > 0);

    StringView sub = a.Substring(1, 3);  // "ell"
    StringView expected_sub("ell");
    REQUIRE(sub == expected_sub);
}

TEST_CASE("String: Append builds expected content and stays null-terminated", "[string][string]")
{
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

// Container audit Day 12: an append grew twice (the bytes to an exact size, then the terminator doubled it), and the
// Windows window host's 128-byte scratch for this very line ran out (an abort, seen as input_probe failing on the PC).
TEST_CASE("String: an append grows once, so the window host's message fits its 128-byte arena", "[string]")
{
    ArenaAllocator arena(128);
    StringBuilder<ArenaAllocator> line(arena);
    line.Append("Window: no usable backend (").Append("wayland is a Linux backend").Append(')');
    CHECK(line.View() == StringView("Window: no usable backend (wayland is a Linux backend)"));
    CHECK(line.CStr()[line.View().Length()] == '\0');

    ArenaAllocator self_arena(4096);
    String<ArenaAllocator> s(self_arena);
    s.Append(StringView("abc"));
    s.Append(s.View());  // its own storage: copied from its offset after the growth
    s.Append(s.View().Substring(1, 2));
    CHECK(s.View() == StringView("abcabcbc"));
    CHECK(s.CStr()[s.Length()] == '\0');
}

TEST_CASE("StringBuilder: formats ints/floats/strings byte-for-byte correctly", "[string][string_builder]")
{
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
                   BytesEqual(reinterpret_cast<const u8*>(actual.Data()), reinterpret_cast<const u8*>(expected),
                              actual.Length());
    REQUIRE(matches);

    // Write the formatted result via os::Write, demonstrating the same
    // buffer already verified above going out through the Day 2 wrapper.
    Write(1, actual.Data(), actual.Length());
    const char newline[] = "\n";
    Write(1, newline, 1);
}

TEST_CASE("StringBuilder::AppendHex formats lowercase hex with optional zero padding", "[string]")
{
    using namespace vse;
    // Each call gets its own arena; the result is compared by value.
    auto hex = [](u64 v, int digits)
    {
        static ArenaAllocator arena(1 << 16);
        StringBuilder<ArenaAllocator> sb(arena);
        sb.AppendHex(v, digits);
        return sb.View();
    };
    REQUIRE(hex(0, 1) == StringView("0"));
    REQUIRE(hex(9, 1) == StringView("9"));
    REQUIRE(hex(10, 1) == StringView("a"));
    REQUIRE(hex(16, 1) == StringView("10"));  // mouse mask 16 is 0x10, not "16"
    REQUIRE(hex(0x1f, 1) == StringView("1f"));
    REQUIRE(hex(0xdeadbeefULL, 1) == StringView("deadbeef"));
    REQUIRE(hex(0xffffffffffffffffULL, 1) == StringView("ffffffffffffffff"));
    REQUIRE(hex(5, 4) == StringView("0005"));
    REQUIRE(hex(0, 3) == StringView("000"));
}

TEST_CASE("StringView: LastIndexOf, StartsWith, EndsWith and open-ended Substring", "[string][stringview]")
{
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

// Container audit series Day 3: bulk append, Reserve, Clear and range-for over the bytes.
TEST_CASE("String: bulk append, Reserve, Clear and range-for", "[string]")
{
    using namespace vse;
    String<HeapAllocator> s(HeapAllocator::Default());
    s.Reserve(1000);
    for (int i = 0; i < 100; ++i)
    {
        s.Append(StringView("0123456789"));
    }
    CHECK(s.Length() == 1000);
    CHECK(s.CStr()[1000] == '\0');
    usize digits = 0;
    for (char c : s)
    {
        digits += c >= '0' && c <= '9' ? 1 : 0;
    }
    CHECK(digits == 1000);
    usize in_view = 0;
    for (char c : StringView("abc"))
    {
        in_view += static_cast<usize>(c - 'a');
    }
    CHECK(in_view == 3);
    s.Clear();
    CHECK(s.Length() == 0);
    CHECK(s.View() == StringView(""));
    s.AppendRawBytes("xy", 2);
    CHECK(s == StringView("xy"));
    s.Append(s.View());  // a view of itself
    CHECK(s == StringView("xyxy"));
    StringBuilder<HeapAllocator> b(HeapAllocator::Default());
    b.Reserve(64).Append(StringView("n=")).AppendInt(42);
    CHECK(b.View() == StringView("n=42"));
}
