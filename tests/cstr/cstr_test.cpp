// Oracle test of the cstr module (mem operations, C strings, formatting, numbers): each wrapper is compared with the C library function
// it forwards to over generated inputs. Includes the standard headers on purpose (the gate lists oracle files by name).
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"
#include "cstr/varargs.h"

using namespace engine_placeholder;

namespace {

int Sign(int v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }

int CallV(char* buffer, usize capacity, const char* format, ...) {
    VarArgs args;
    ENGINE_VA_START(args, format);
    const int n = VFormatTo(buffer, capacity, format, args);
    ENGINE_VA_END(args);
    return n;
}

} // namespace

TEST_CASE("Mem operations match the C library, including overlap and zero sizes", "[cstr]") {
    byte a[64], b[64], c[64];
    for (usize i = 0; i < 64; ++i) a[i] = static_cast<byte>(i * 7 + 1);
    for (usize n : {usize(0), usize(1), usize(7), usize(16), usize(33), usize(64)}) {
        MemSet(b, 0, 64);
        std::memset(c, 0, 64);
        CHECK(MemCopy(b, a, n) == b);
        std::memcpy(c, a, n);
        CHECK(std::memcmp(b, c, 64) == 0);
        CHECK(Sign(MemCompare(a, b, n)) == Sign(std::memcmp(a, b, n)));
        CHECK(MemCompare(a, b, n) == 0);
    }
    b[10] ^= 0x55;
    CHECK(Sign(MemCompare(a, b, 64)) == Sign(std::memcmp(a, b, 64)));
    CHECK(MemCompare(a, b, 10) == 0);

    // Overlapping moves, both directions.
    for (usize shift : {usize(1), usize(3), usize(31)}) {
        byte x[64], y[64];
        std::memcpy(x, a, 64);
        std::memcpy(y, a, 64);
        MemMove(x + shift, x, 64 - shift);
        std::memmove(y + shift, y, 64 - shift);
        CHECK(std::memcmp(x, y, 64) == 0);
        MemMove(x, x + shift, 64 - shift);
        std::memmove(y, y + shift, 64 - shift);
        CHECK(std::memcmp(x, y, 64) == 0);
    }

    CHECK(MemFind(a, a[20], 64) == std::memchr(a, a[20], 64));
    CHECK(MemFind(a, 0x7f, 0) == nullptr);
    CHECK(MemSet(nullptr, 0, 0) == nullptr);
    CHECK(MemCopy(nullptr, nullptr, 0) == nullptr);
    CHECK(MemCompare(nullptr, nullptr, 0) == 0);
}

TEST_CASE("The C heap wrappers allocate, zero, resize and free", "[cstr]") {
    byte* block = static_cast<byte*>(MemAllocate(100));
    REQUIRE(block != nullptr);
    MemSet(block, 0xAB, 100);
    block = static_cast<byte*>(MemReallocate(block, 400));
    REQUIRE(block != nullptr);
    for (usize i = 0; i < 100; ++i) REQUIRE(block[i] == 0xAB);
    MemFree(block);
    byte* zeroed = static_cast<byte*>(MemAllocateZeroed(50, 4));
    REQUIRE(zeroed != nullptr);
    for (usize i = 0; i < 200; ++i) REQUIRE(zeroed[i] == 0);
    MemFree(zeroed);
    MemFree(nullptr);
}

TEST_CASE("C string functions match the C library", "[cstr]") {
    const char* samples[] = {"", "a", "abc", "abd", "abcd", "ABC", "hello world", "hello", "\xE9x", "x\xE9"};
    for (const char* a : samples) {
        CHECK(CStrLength(a) == std::strlen(a));
        for (const char* b : samples) {
            CHECK(Sign(CStrCompare(a, b)) == Sign(std::strcmp(a, b)));
            CHECK(CStrFind(a, b) == std::strstr(a, b));
            for (usize n : {usize(0), usize(1), usize(2), usize(5)}) {
                CHECK(Sign(CStrCompareN(a, b, n)) == Sign(std::strncmp(a, b, n)));
            }
        }
        for (int c : {int('a'), int('l'), int('z'), 0}) {
            CHECK(CStrFindChar(a, c) == std::strchr(a, c));
            CHECK(CStrFindLastChar(a, c) == std::strrchr(a, c));
        }
        CHECK(CStrFindAny(a, "ow") == std::strpbrk(a, "ow"));
    }
    char x[32], y[32];
    CStrCopy(x, "copy me");
    std::strcpy(y, "copy me");
    CHECK(std::strcmp(x, y) == 0);
    CStrAppend(x, "!!");
    std::strcat(y, "!!");
    CHECK(std::strcmp(x, y) == 0);
    MemSet(x, 'z', 32);
    MemSet(y, 'z', 32);
    CStrCopyN(x, "ab", 6);
    std::strncpy(y, "ab", 6);
    CHECK(std::memcmp(x, y, 32) == 0);
    char* mutable_found = CStrFind(x, "b");
    CHECK(mutable_found == x + 1);
    char* mutable_char = CStrFindChar(x, 'b');
    CHECK(mutable_char == x + 1);
}

TEST_CASE("CStrCopyTo always terminates and reports the full length", "[cstr]") {
    char buf[8];
    for (usize capacity = 0; capacity <= 8; ++capacity) {
        MemSet(buf, 'q', 8);
        const usize full = CStrCopyTo(buf, capacity, "hello");
        CHECK(full == 5);
        if (capacity == 0) {
            CHECK(buf[0] == 'q');
        } else {
            const usize kept = full < capacity ? full : capacity - 1;
            CHECK(buf[kept] == '\0');
            CHECK(std::strncmp(buf, "hello", kept) == 0);
            if (capacity < 8) CHECK(buf[capacity] == 'q');
        }
    }
}

TEST_CASE("Wide string functions match the C library", "[cstr]") {
    const wchar_t* a = L"hello wide";
    const wchar_t* b = L"hello";
    CHECK(WStrLength(a) == std::wcslen(a));
    CHECK(Sign(WStrCompare(a, b)) == Sign(std::wcscmp(a, b)));
    CHECK(WStrCompareN(a, b, 5) == 0);
    CHECK(WStrFind(a, L"wide") == std::wcsstr(a, L"wide"));
    CHECK(WStrFindLastChar(a, L'e') == std::wcsrchr(a, L'e'));
    wchar_t buffer[32] = L"ab";
    WStrAppend(buffer, L"cd");
    CHECK(std::wcscmp(buffer, L"abcd") == 0);
    wchar_t copy[8] = {};
    WMemCopy(copy, buffer, 5);
    CHECK(std::wcscmp(copy, L"abcd") == 0);
    WMemMove(copy + 1, copy, 4);
    CHECK(std::wcscmp(copy, L"aabcd") == 0);
}

namespace {
// The oracle goes through vsnprintf so GCC cannot see the constant arguments and warn (-Wformat-truncation) about the small capacities this test tries on purpose.
int OracleSnprintf(char* buffer, usize capacity, const char* format, ...) {
    VarArgs args;
    ENGINE_VA_START(args, format);
    const int length = std::vsnprintf(buffer, capacity, format, args);
    ENGINE_VA_END(args);
    return length;
}
} // namespace

TEST_CASE("FormatTo matches snprintf for every conversion the tree uses and every capacity", "[cstr]") {
    char got[64], want[64];
    for (usize capacity = 0; capacity <= 20; ++capacity) {
        MemSet(got, 'g', 64);
        MemSet(want, 'g', 64);
        const int n = FormatTo(got, capacity, "%d|%s|%5.2f|%x|%llu", -42, "txt", 3.14159, 255u, 123456789012ull);
        const int m = OracleSnprintf(want, capacity, "%d|%s|%5.2f|%x|%llu", -42, "txt", 3.14159, 255u, 123456789012ull);
        CHECK(n == m);
        CHECK(std::memcmp(got, want, 64) == 0);
    }
    CHECK(FormatTo(nullptr, 0, "%s", "length only") == 11);
    char buf[16];
    CHECK(CallV(buf, sizeof(buf), "%c%c%c", 'a', 'b', 'c') == 3);
    CHECK(std::strcmp(buf, "abc") == 0);
    CHECK(CallV(buf, 4, "%s", "truncated") == 9);
    CHECK(std::strcmp(buf, "tru") == 0);

    wchar_t wide[16];
    CHECK(WFormatTo(wide, 16, L"%ls-%d", L"w", 7) == 3);
    CHECK(std::wcscmp(wide, L"w-7") == 0);

    int a = 0, b = 0;
    CHECK(ScanFrom("12 34", "%d %d", &a, &b) == 2);
    CHECK(a == 12);
    CHECK(b == 34);
    CHECK(ScanFrom("x", "%d", &a) == 0);
}

TEST_CASE("Number conversion matches the C library", "[cstr]") {
    CHECK(CStrToInt("  -17abc") == std::atoi("  -17abc"));
    CHECK(CStrToInt("none") == 0);
    CHECK(CStrToLong("123456789012") == std::atol("123456789012"));
    char* end1 = nullptr;
    char* end2 = nullptr;
    CHECK(CStrToULong("0x1F rest", &end1, 16) == std::strtoul("0x1F rest", &end2, 16));
    CHECK(end1 == end2);
    CHECK(CStrToULongLong("18446744073709551615", nullptr, 10) == 18446744073709551615ull);
    CHECK(CStrToLong("-5", nullptr, 10) == -5);
    CHECK(CStrToLongLong("-9", nullptr, 10) == -9);
    CHECK(CStrToDouble("2.5e3x", &end1) == std::strtod("2.5e3x", &end2));
    CHECK(end1 == end2);
    CHECK(CStrToDouble("1.25") == 1.25);
    CHECK(CStrToFloat("0.5", nullptr) == 0.5f);

    char buffer[40];
    for (double v : {0.0, 1.0, 0.1, 3.14159, 1e300, -2.5e-7}) {
        const FormatShortestResult r = FormatShortest(buffer, buffer + sizeof(buffer) - 1, v);
        REQUIRE(r.ok);
        *r.end = '\0';
        CHECK(std::strtod(buffer, nullptr) == v);
    }
    char tiny[2];
    CHECK(!FormatShortest(tiny, tiny + 1, 12345.678).ok);
}

TEST_CASE("ParseDoubleExact reads plain decimals only", "[cstr]") {
    const char* ok = "12.5e1x";
    ParseDoubleResult r = ParseDoubleExact(ok, ok + 7);
    CHECK(r.ok);
    CHECK(r.value == 125.0);
    CHECK(r.end == ok + 6);
    for (const char* bad : {"+1", " 1", "", "abc"}) {
        const ParseDoubleResult b = ParseDoubleExact(bad, bad + CStrLength(bad));
        CHECK(!b.ok);
    }
    const char* const hex_text = "0x10";
    const ParseDoubleResult hex = ParseDoubleExact(hex_text, hex_text + 4);
    CHECK(hex.ok);
    CHECK(hex.value == 0.0); // reads the "0"; the caller sees that the end is not the end of the text
}

namespace {
int CompareInts(const void* a, const void* b) {
    const int x = *static_cast<const int*>(a);
    const int y = *static_cast<const int*>(b);
    return x < y ? -1 : (x > y ? 1 : 0);
}
} // namespace

TEST_CASE("SortRaw orders raw elements", "[cstr]") {
    int values[] = {5, 3, 9, -1, 3, 0};
    SortRaw(values, 6, sizeof(int), &CompareInts);
    for (int i = 1; i < 6; ++i) CHECK(values[i - 1] <= values[i]);
    CHECK(values[0] == -1);
    CHECK(values[5] == 9);
}
