#include <catch2/catch_test_macros.hpp>

#include "io/file.h"
#include "io/file_system.h"
#include "mem/arena_allocator.h"
#include "types.h"

using engine_placeholder::ArenaAllocator;
using engine_placeholder::u8;
using engine_placeholder::usize;
namespace io = engine_placeholder::io;

TEST_CASE("io::ReadFile reads /proc/self/exe and finds the ELF magic number", "[file_io][io]") {
    ArenaAllocator arena(128 * 1024 * 1024); // the running test binary (Catch2 statically linked) is well into the tens of MB
    usize size = 0;
    u8* buffer = nullptr;
    const io::Status status = io::ReadFile(io::Path("/proc/self/exe"), arena, &buffer, &size);

    REQUIRE(status.Ok());
    REQUIRE(buffer != nullptr);
    REQUIRE(size >= 4);
    REQUIRE(buffer[0] == 0x7F);
    REQUIRE(buffer[1] == 'E');
    REQUIRE(buffer[2] == 'L');
    REQUIRE(buffer[3] == 'F');
}

TEST_CASE("io::ReadFile reports NotFound for a nonexistent file", "[file_io][io]") {
    ArenaAllocator arena(1024);
    usize size = 0;
    u8* buffer = nullptr;
    const io::Status status = io::ReadFile(io::Path("/nonexistent/path/engine_test_should_not_exist"), arena, &buffer, &size);
    REQUIRE(status.Is(io::Error::NotFound));
    REQUIRE(buffer == nullptr);
}

TEST_CASE("io::File::Seek repositions the stream for subsequent reads", "[file_io][io]") {
    io::Path dir;
    REQUIRE(io::TempDirectory(&dir).Ok());
    io::Path scratch;
    REQUIRE(io::CreateTempDirectory(dir, "file_io", &scratch).Ok());
    const io::Path path = scratch.Join("seek.txt");
    REQUIRE(io::WriteFile(path, "0123456789", 10).Ok());

    io::FilePtr file;
    REQUIRE(io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &file).Ok());
    REQUIRE(file->Seek(5).Ok());
    char buf[5] = {};
    usize n = 0;
    REQUIRE(file->Read(buf, 5, &n).Ok());
    REQUIRE(n == 5);
    REQUIRE(buf[0] == '5');
    REQUIRE(buf[4] == '9');
    REQUIRE(file->Tell() == 10);
    file.Reset();
    io::RemoveAll(scratch);
}
