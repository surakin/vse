#pragma once

#include <catch2/catch_test_macros.hpp>

#include "io/file.h"
#include "test_text.h"

namespace io_test {

using namespace engine_placeholder;

/// A `make(StringView content, bool writable)` callable returns an open `io::FilePtr` holding `content`; `writable` asks for
/// a file that also accepts writes (a backend that cannot write is only run through the read half).

inline engine_test::Text PatternBytes(size_t n) {
    engine_test::Text s = engine_test::MakeText();
    for (size_t i = 0; i < n; ++i) s.Append(static_cast<char>((i * 31 + 7) & 0xFF));
    return s;
}

/// The checks every `io::File` backend must pass for reading. Runs inside a Catch2 test case.
template <typename Maker>
inline void RunReadConformance(Maker&& make) {
    const engine_test::Text content = PatternBytes(100000);
    const StringView whole = content.View(); // larger than the helpers' chunk
    {
        io::FilePtr f = make(whole, false);
        REQUIRE(f);
        // Offset reads, in the middle and across no boundary.
        char buf[64];
        usize n = 0;
        REQUIRE(f->Read(buf, 64, 1000, &n).Ok());
        REQUIRE(n > 0); // a short read is allowed, zero is the end
        CHECK(StringView(buf, n) == whole.Substring(1000, n));
        // Exact reads loop over short reads.
        Vector<char> exact = engine_test::Chars(5000);
        REQUIRE(io::ReadExact(*f, exact.Data(), exact.Size(), 777).Ok());
        CHECK(StringView(exact.Data(), exact.Size()) == whole.Substring(777, 5000));
        // The end: a read that starts at the end returns zero bytes, one that starts past it too.
        REQUIRE(f->Read(buf, 64, whole.Length(), &n).Ok());
        CHECK(n == 0);
        REQUIRE(f->Read(buf, 64, whole.Length() + 1000, &n).Ok());
        CHECK(n == 0);
        // A read that crosses the end returns what exists; ReadExact reports EndOfFile.
        Vector<char> tail = engine_test::Chars(100);
        CHECK(io::ReadExact(*f, tail.Data(), tail.Size(), whole.Length() - 50).Is(io::Error::EndOfFile));
        // A zero-length read succeeds with zero bytes.
        REQUIRE(f->Read(buf, 0, 0, &n).Ok());
        CHECK(n == 0);
        // ReadAll never trusts the size.
        Vector<u8> all;
        REQUIRE(io::ReadAll(*f, &all).Ok());
        CHECK(StringView(reinterpret_cast<const char*>(all.Data()), all.Size()) == whole);
        CHECK(io::ReadAll(*f, &all, 10).Is(io::Error::TooLong));
        // Stream reads advance a shared position; Seek moves it.
        if ((f->Capabilities() & io::kCapSeekable) != 0) {
            REQUIRE(f->Seek(10).Ok());
            REQUIRE(io::ReadExact(*f, buf, 4, 10).Ok());
            usize got = 0;
            REQUIRE(f->Read(buf, 4, &got).Ok());
            CHECK(f->Tell() == 10 + got);
            CHECK(StringView(buf, got) == whole.Substring(10, got));
        }
        u64 size = 0;
        if ((f->Capabilities() & io::kCapSizeKnown) != 0) {
            REQUIRE(f->Size(&size).Ok());
            CHECK(size == whole.Length());
        }
        // Close is idempotent; use after close fails with Invalid.
        CHECK(f->Close().Ok());
        CHECK(f->Close().Ok());
        CHECK(f->Read(buf, 4, 0, &n).Is(io::Error::Invalid));
        CHECK(f->Size(&size).Is(io::Error::Invalid));
    }
    {
        io::FilePtr empty = make(StringView(), false);
        REQUIRE(empty);
        char buf[8];
        usize n = 99;
        REQUIRE(empty->Read(buf, 8, 0, &n).Ok());
        CHECK(n == 0);
        Vector<u8> all;
        REQUIRE(io::ReadAll(*empty, &all).Ok());
        CHECK(all.Size() == 0);
        CHECK(io::ReadExact(*empty, buf, 1, 0).Is(io::Error::EndOfFile));
    }
    {
        // A buffer larger than anything internal.
        const engine_test::Text big = PatternBytes(3 * 1024 * 1024 + 5);
        io::FilePtr f = make(big.View(), false);
        REQUIRE(f);
        Vector<char> got = engine_test::Chars(big.Length());
        REQUIRE(io::ReadExact(*f, got.Data(), got.Size(), 0).Ok());
        CHECK(StringView(got.Data(), got.Size()) == big.View());
    }
}

/// The checks every writable `io::File` backend must pass.
template <typename Maker>
inline void RunWriteConformance(Maker&& make) {
    io::FilePtr f = make(StringView("0123456789"), true);
    REQUIRE(f);
    CHECK((f->Capabilities() & io::kCapWritable) != 0);
    // Overwrite in the middle, extend past the end.
    REQUIRE(f->Write("ABC", 3, 2).Ok());
    REQUIRE(f->Write("XYZ", 3, 12).Ok());
    Vector<char> got = engine_test::Chars(15);
    REQUIRE(io::ReadExact(*f, got.Data(), 12, 0).Ok());
    CHECK(StringView(got.Data(), 10) == StringView("01ABC56789"));
    REQUIRE(io::ReadExact(*f, got.Data() + 12, 3, 12).Ok());
    CHECK(StringView(got.Data() + 12, 3) == StringView("XYZ"));
    // The gap reads as zeros.
    char gap[2];
    REQUIRE(io::ReadExact(*f, gap, 2, 10).Ok());
    CHECK((gap[0] == 0 && gap[1] == 0));
    // A large write is complete or an error.
    const engine_test::Text big = PatternBytes(2 * 1024 * 1024 + 3);
    REQUIRE(f->Write(big.CStr(), big.Length(), 0).Ok());
    Vector<char> back = engine_test::Chars(big.Length());
    REQUIRE(io::ReadExact(*f, back.Data(), back.Size(), 0).Ok());
    CHECK(StringView(back.Data(), back.Size()) == big.View());
    // Truncate shrinks and extends with zeros.
    REQUIRE(f->Truncate(5).Ok());
    u64 size = 0;
    if ((f->Capabilities() & io::kCapSizeKnown) != 0) {
        REQUIRE(f->Size(&size).Ok());
        CHECK(size == 5);
    }
    REQUIRE(f->Truncate(8).Ok());
    char tail[8];
    REQUIRE(io::ReadExact(*f, tail, 8, 0).Ok());
    CHECK(StringView(tail, 5) == StringView(back.Data(), 5));
    CHECK((tail[5] == 0 && tail[7] == 0));
    // Stream writes advance the position.
    REQUIRE(f->Seek(0).Ok());
    REQUIRE(f->Write("hello", 5).Ok());
    CHECK(f->Tell() == 5);
    REQUIRE(f->Write("!!", 2).Ok());
    char head[7];
    REQUIRE(io::ReadExact(*f, head, 7, 0).Ok());
    CHECK(StringView(head, 7) == StringView("hello!!"));
    // Flush and Sync succeed (or Sync says Unsupported).
    CHECK(f->Flush().Ok());
    const io::Status sync = f->Sync();
    CHECK((sync.Ok() || sync.Is(io::Error::Unsupported)));
    // Close, then everything fails.
    CHECK(f->Close().Ok());
    CHECK(f->Write("x", 1, 0).Is(io::Error::Invalid));
    CHECK(f->Truncate(0).Is(io::Error::Invalid));
    CHECK(f->Flush().Is(io::Error::Invalid));
}

} // namespace io_test
