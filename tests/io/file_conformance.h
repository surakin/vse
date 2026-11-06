#pragma once

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>

#include "io/file.h"

namespace io_test {

using namespace engine_placeholder;

/// Makes an open file holding `content`. `writable` asks for a file that also accepts writes (a backend that cannot write
/// is only run through the read half).
using FileMaker = std::function<io::FilePtr(const std::string& content, bool writable)>;

inline std::string PatternBytes(size_t n) {
    std::string s(n, '\0');
    for (size_t i = 0; i < n; ++i) s[i] = static_cast<char>((i * 31 + 7) & 0xFF);
    return s;
}

/// The checks every `io::File` backend must pass for reading. Runs inside a Catch2 test case.
inline void RunReadConformance(const FileMaker& make) {
    const std::string content = PatternBytes(100000); // larger than the helpers' chunk
    {
        io::FilePtr f = make(content, false);
        REQUIRE(f);
        // Offset reads, in the middle and across no boundary.
        char buf[64];
        usize n = 0;
        REQUIRE(f->Read(buf, 64, 1000, &n).Ok());
        REQUIRE(n > 0); // a short read is allowed, zero is the end
        CHECK(std::string(buf, n) == content.substr(1000, n));
        // Exact reads loop over short reads.
        std::string exact(5000, '\0');
        REQUIRE(io::ReadExact(*f, exact.data(), exact.size(), 777).Ok());
        CHECK(exact == content.substr(777, 5000));
        // The end: a read that starts at the end returns zero bytes, one that starts past it too.
        REQUIRE(f->Read(buf, 64, content.size(), &n).Ok());
        CHECK(n == 0);
        REQUIRE(f->Read(buf, 64, content.size() + 1000, &n).Ok());
        CHECK(n == 0);
        // A read that crosses the end returns what exists; ReadExact reports EndOfFile.
        std::string tail(100, '\0');
        CHECK(io::ReadExact(*f, tail.data(), tail.size(), content.size() - 50).Is(io::Error::EndOfFile));
        // A zero-length read succeeds with zero bytes.
        REQUIRE(f->Read(buf, 0, 0, &n).Ok());
        CHECK(n == 0);
        // ReadAll never trusts the size.
        Vector<u8> all;
        REQUIRE(io::ReadAll(*f, &all).Ok());
        CHECK(std::string(reinterpret_cast<const char*>(all.Data()), all.Size()) == content);
        CHECK(io::ReadAll(*f, &all, 10).Is(io::Error::TooLong));
        // Stream reads advance a shared position; Seek moves it.
        if ((f->Capabilities() & io::kCapSeekable) != 0) {
            REQUIRE(f->Seek(10).Ok());
            REQUIRE(io::ReadExact(*f, buf, 4, 10).Ok());
            usize got = 0;
            REQUIRE(f->Read(buf, 4, &got).Ok());
            CHECK(f->Tell() == 10 + got);
            CHECK(std::string(buf, got) == content.substr(10, got));
        }
        u64 size = 0;
        if ((f->Capabilities() & io::kCapSizeKnown) != 0) {
            REQUIRE(f->Size(&size).Ok());
            CHECK(size == content.size());
        }
        // Close is idempotent; use after close fails with Invalid.
        CHECK(f->Close().Ok());
        CHECK(f->Close().Ok());
        CHECK(f->Read(buf, 4, 0, &n).Is(io::Error::Invalid));
        CHECK(f->Size(&size).Is(io::Error::Invalid));
    }
    {
        io::FilePtr empty = make("", false);
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
        const std::string big = PatternBytes(3 * 1024 * 1024 + 5);
        io::FilePtr f = make(big, false);
        REQUIRE(f);
        std::string got(big.size(), '\0');
        REQUIRE(io::ReadExact(*f, got.data(), got.size(), 0).Ok());
        CHECK(got == big);
    }
}

/// The checks every writable `io::File` backend must pass.
inline void RunWriteConformance(const FileMaker& make) {
    io::FilePtr f = make("0123456789", true);
    REQUIRE(f);
    CHECK((f->Capabilities() & io::kCapWritable) != 0);
    // Overwrite in the middle, extend past the end.
    REQUIRE(f->Write("ABC", 3, 2).Ok());
    REQUIRE(f->Write("XYZ", 3, 12).Ok());
    std::string got(15, '\0');
    REQUIRE(io::ReadExact(*f, got.data(), 12, 0).Ok());
    CHECK(got.substr(0, 10) == "01ABC56789");
    REQUIRE(io::ReadExact(*f, got.data() + 12, 3, 12).Ok());
    CHECK(got.substr(12, 3) == "XYZ");
    // The gap reads as zeros.
    char gap[2];
    REQUIRE(io::ReadExact(*f, gap, 2, 10).Ok());
    CHECK((gap[0] == 0 && gap[1] == 0));
    // A large write is complete or an error.
    const std::string big = PatternBytes(2 * 1024 * 1024 + 3);
    REQUIRE(f->Write(big.data(), big.size(), 0).Ok());
    std::string back(big.size(), '\0');
    REQUIRE(io::ReadExact(*f, back.data(), back.size(), 0).Ok());
    CHECK(back == big);
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
    CHECK(std::string(tail, 5) == back.substr(0, 5));
    CHECK((tail[5] == 0 && tail[7] == 0));
    // Stream writes advance the position.
    REQUIRE(f->Seek(0).Ok());
    REQUIRE(f->Write("hello", 5).Ok());
    CHECK(f->Tell() == 5);
    REQUIRE(f->Write("!!", 2).Ok());
    char head[7];
    REQUIRE(io::ReadExact(*f, head, 7, 0).Ok());
    CHECK(std::string(head, 7) == "hello!!");
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
