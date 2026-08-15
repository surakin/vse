#include <fcntl.h>

#include <catch2/catch_test_macros.hpp>

#include "mem/arena_allocator.h"
#include "os/file_util.h"
#include "os/os.h"
#include "string/string.h"
#include "types.h"

using engine_placeholder::ArenaAllocator;
using engine_placeholder::StringView;
using engine_placeholder::u8;
using engine_placeholder::usize;
using engine_placeholder::os::Close;
using engine_placeholder::os::Lseek;
using engine_placeholder::os::Open;
using engine_placeholder::os::Read;
using engine_placeholder::os::ReadFileToArena;
using engine_placeholder::os::Write;

TEST_CASE("os::Lseek repositions the file offset for subsequent reads", "[file_io][os]") {
    const char* path = "/tmp/engine_test_lseek.txt";
    int fd = Open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const char content[] = "0123456789";
    Write(fd, content, sizeof(content) - 1);
    Close(fd);

    fd = Open(path, O_RDONLY);
    long pos = Lseek(fd, 5, SEEK_SET);
    REQUIRE(pos == 5);
    char buf[5] = {};
    long n = Read(fd, buf, 5);
    Close(fd);

    REQUIRE(n == 5);
    REQUIRE(StringView(buf, 5) == StringView("56789"));
}

TEST_CASE("ReadFileToArena reads /proc/self/exe and finds the ELF magic number", "[file_io][os]") {
    ArenaAllocator arena(64 * 1024 * 1024); // the running test binary (Catch2 statically linked) is well into the tens of MB
    usize size = 0;
    u8* buffer = ReadFileToArena("/proc/self/exe", arena, size);

    REQUIRE(buffer != nullptr);
    REQUIRE(size >= 4);
    REQUIRE(buffer[0] == 0x7F);
    REQUIRE(buffer[1] == 'E');
    REQUIRE(buffer[2] == 'L');
    REQUIRE(buffer[3] == 'F');
}

TEST_CASE("ReadFileToArena returns nullptr for a nonexistent file", "[file_io][os]") {
    ArenaAllocator arena(1024);
    usize size = 0;
    u8* buffer = ReadFileToArena("/nonexistent/path/engine_test_should_not_exist", arena, size);
    REQUIRE(buffer == nullptr);
}
