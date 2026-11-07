// The sharing rule of io::File (Phase 13 Day 151; engine/foundation/io/BACKEND.md, "Sharing"): a file can be open any number of times, in
// any modes, at once, and every open sees the others' writes. Linux has no mandatory locks; the Windows backend opens with every share
// flag, so the same rule holds on both platforms.
#include <catch2/catch_test_macros.hpp>

#include "io/file.h"
#include "temp_dir.h"
#include "test_text.h"

using namespace engine_placeholder;
using io::OpenMode;
using io_test::TempDir;

namespace {

const char* ModeName(OpenMode mode) {
    switch (mode) {
    case OpenMode::Read: return "Read";
    case OpenMode::Write: return "Write";
    case OpenMode::Append: return "Append";
    case OpenMode::ReadWrite: return "ReadWrite";
    }
    return "?";
}

bool Readable(OpenMode m) { return m == OpenMode::Read || m == OpenMode::ReadWrite; }
bool OffsetWritable(OpenMode m) { return m == OpenMode::Write || m == OpenMode::ReadWrite; }

} // namespace

TEST_CASE("io::File: a file opens twice in every pair of modes, and each open sees the other's writes", "[io][file][share]") {
    TempDir dir;
    const OpenMode modes[] = {OpenMode::Read, OpenMode::Write, OpenMode::Append, OpenMode::ReadWrite};
    int pair = 0;
    for (OpenMode first : modes) {
        for (OpenMode second : modes) {
            INFO("first " << ModeName(first) << ", second " << ModeName(second));
            const io::Path path = dir / engine_test::Format("shared%d", pair++).View();
            REQUIRE(io::WriteFile(path, "0123456789", 10).Ok());
            io::FilePtr a;
            io::FilePtr b;
            REQUIRE(io::File::Open(path, first, io::kOpenNone, &a).Ok());
            REQUIRE(io::File::Open(path, second, io::kOpenNone, &b).Ok());
            // A write through one open is read back through the other at once (no buffering in the layer).
            io::File* writer = OffsetWritable(first) ? a.Get() : (OffsetWritable(second) ? b.Get() : nullptr);
            io::File* reader = Readable(second) ? b.Get() : (Readable(first) ? a.Get() : nullptr);
            if (writer != nullptr && reader != nullptr && writer != reader) {
                REQUIRE(writer->Write("AB", 2, 0).Ok());
                char got[2] = {};
                REQUIRE(io::ReadExact(*reader, got, 2, 0).Ok());
                CHECK(StringView(got, 2) == StringView("AB"));
            }
            // Appends from both opens all land.
            if (first == OpenMode::Append && second == OpenMode::Append) {
                REQUIRE(a->Append("x", 1).Ok());
                REQUIRE(b->Append("y", 1).Ok());
                REQUIRE(a->Close().Ok());
                REQUIRE(b->Close().Ok());
                CHECK(io_test::ReadText(path) == "0123456789xy");
            }
            CHECK(a->Close().Ok());
            CHECK(b->Close().Ok());
        }
    }
}
