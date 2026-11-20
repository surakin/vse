#include <catch2/catch_test_macros.hpp>

#include "io/file.h"
#include "io/file_system.h"
#include "mem/arena_allocator.h"
#include "types.h"
#include "build_defs.h"

using vse::ArenaAllocator;
using vse::u8;
using vse::usize;
namespace io = vse::io;

// The running test program, read through its path: ELF on Linux, a PE image (`MZ`) on Windows (Phase 13 Day 158),
// Mach-O on macOS (Phase 21 Day 293: a 64-bit image, little-endian 0xFEEDFACF, or a universal one, 0xCAFEBABE).
TEST_CASE("io::ReadFile reads the running program and finds its executable magic number", "[file_io][io]")
{
    ArenaAllocator arena(128 * 1024 *
                         1024);  // the running test binary (Catch2 statically linked) is well into the tens of MB
    usize size = 0;
    u8* buffer = nullptr;
    io::Path self;
    REQUIRE(io::ExecutablePath(&self).Ok());
    const io::Status status = io::ReadFile(self, arena, &buffer, &size);

    REQUIRE(status.Ok());
    REQUIRE(buffer != nullptr);
    REQUIRE(size >= 4);
#if ENGINE_PLATFORM_WINDOWS
    REQUIRE(buffer[0] == 'M');
    REQUIRE(buffer[1] == 'Z');
#elif ENGINE_PLATFORM_MACOS
    const bool thin = buffer[0] == 0xCF && buffer[1] == 0xFA && buffer[2] == 0xED && buffer[3] == 0xFE;
    const bool universal = buffer[0] == 0xCA && buffer[1] == 0xFE && buffer[2] == 0xBA && buffer[3] == 0xBE;
    REQUIRE((thin || universal));
#else
    REQUIRE(buffer[0] == 0x7F);
    REQUIRE(buffer[1] == 'E');
    REQUIRE(buffer[2] == 'L');
    REQUIRE(buffer[3] == 'F');
#endif
}

TEST_CASE("io::ReadFile reports NotFound for a nonexistent file", "[file_io][io]")
{
    ArenaAllocator arena(1024);
    usize size = 0;
    u8* buffer = nullptr;
    const io::Status status = io::ReadFile(io::Path("/nonexistent/path/engine_test_should_not_exist"), arena, &buffer,
                                           &size);
    REQUIRE(status.Is(io::Error::NotFound));
    REQUIRE(buffer == nullptr);
}

TEST_CASE("io::File::Seek repositions the stream for subsequent reads", "[file_io][io]")
{
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
