#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "containers/span.h"

using namespace engine_placeholder;

namespace {

usize Sum(ByteSpan bytes) {
    usize total = 0;
    for (byte b : bytes) total += b;
    return total;
}

usize FillAscending(MutableByteSpan bytes) {
    for (usize i = 0; i < bytes.Size(); ++i) bytes[i] = static_cast<byte>(i + 1);
    return bytes.Size();
}

} // namespace

TEST_CASE("Span: an empty span and the basic accessors", "[types][span]") {
    Span<const int> empty;
    CHECK(empty.Empty());
    CHECK(empty.Size() == 0);
    CHECK(empty.Data() == nullptr);
    int values[4] = {1, 2, 3, 4};
    Span<int> s(values, 4);
    CHECK(s.Size() == 4);
    CHECK(s.SizeBytes() == 4 * sizeof(int));
    CHECK(s[2] == 3);
    s[2] = 30;
    CHECK(values[2] == 30);
    int total = 0;
    for (int v : s) total += v;
    CHECK(total == 1 + 2 + 30 + 4);
}

TEST_CASE("Span: conversions from Vector, Array, C arrays and other spans", "[types][span]") {
    Vector<byte> v;
    v.Resize(8);
    CHECK(FillAscending(v) == 8);          // Vector -> MutableByteSpan
    CHECK(Sum(v) == 36);                   // Vector -> ByteSpan
    const Vector<byte>& cv = v;
    CHECK(Sum(cv) == 36);                  // const Vector -> ByteSpan
    Array<byte, 4> a = {{1, 2, 3, 4}};
    CHECK(Sum(a) == 10);                   // Array -> ByteSpan
    const Array<byte, 4>& ca = a;
    CHECK(Sum(ca) == 10);
    CHECK(FillAscending(a) == 4);          // Array -> MutableByteSpan
    byte c[3] = {5, 6, 7};
    CHECK(Sum(c) == 18);                   // C array -> ByteSpan
    MutableByteSpan m(c, 3);
    ByteSpan r = m;                        // mutable -> const
    CHECK(Sum(r) == 18);
    // The conversions that must NOT compile: const to mutable.
    STATIC_REQUIRE_FALSE(std::is_convertible_v<ByteSpan, MutableByteSpan>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<const Vector<byte>&, MutableByteSpan>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<const Array<byte, 4>&, MutableByteSpan>);
    STATIC_REQUIRE(std::is_convertible_v<MutableByteSpan, ByteSpan>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ByteSpan>);
}

TEST_CASE("Span: First, Last and Subspan", "[types][span]") {
    byte data[6] = {10, 11, 12, 13, 14, 15};
    ByteSpan s(data, 6);
    CHECK(s.First(2).Size() == 2);
    CHECK(s.First(2)[1] == 11);
    CHECK(s.Last(2)[0] == 14);
    CHECK(s.Subspan(1, 3)[0] == 11);
    CHECK(s.Subspan(1, 3).Size() == 3);
    CHECK(s.Subspan(4).Size() == 2);
    CHECK(s.Subspan(6).Empty());       // the end is a valid offset
    CHECK(s.Subspan(6, 0).Empty());
    CHECK(s.First(0).Empty());
    CHECK(s.Last(6).Data() == s.Data());
    CHECK(Sum(s.Subspan(2, 2)) == 12 + 13);
}

TEST_CASE("Span: bytes of values, text and structs", "[types][span]") {
    struct Pod {
        u32 a;
        u32 b;
    } pod{0x04030201u, 0x08070605u};
    ByteSpan bytes = AsBytes(pod);
    CHECK(bytes.Size() == 8);
    CHECK(bytes[0] == 1);
    CHECK(bytes[7] == 8);
    const u16 shorts[3] = {1, 2, 3};
    CHECK(AsBytes(Span<const u16>(shorts)).Size() == 6);
    ByteSpan text = AsByteSpan("hello");
    CHECK(text.Size() == 5);
    CHECK(text[0] == 'h');
    CHECK(ToStringView(text) == StringView("hello"));
    Vector<byte> v;
    v.Resize(3);
    FillAscending(v);
    CHECK(AsBytes(Span<const byte>(v)).Size() == 3);
}

TEST_CASE("byte is u8 and the standard widths hold", "[types]") {
    STATIC_REQUIRE(std::is_same_v<byte, u8>);
    STATIC_REQUIRE(std::is_same_v<byte, unsigned char>);
    STATIC_REQUIRE(sizeof(isize) == sizeof(usize));
    STATIC_REQUIRE(std::is_signed_v<isize>);
    STATIC_REQUIRE(std::is_unsigned_v<usize>);
    byte b = 0xff;
    CHECK(static_cast<int>(b) == 255);
}

TEST_CASE("String compares with views and converts to a view", "[types][span]") {
    String<HeapAllocator> text(HeapAllocator::Default(), StringView("abc"));
    CHECK(text == "abc");
    CHECK(text != "abd");
    const StringView view = text;
    CHECK(view.Length() == 3);
    String<HeapAllocator> same(HeapAllocator::Default(), StringView("abc"));
    CHECK(text == same);
}
