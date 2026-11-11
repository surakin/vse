// Test of the os console and process wrappers: output goes where the C library's streams go; the environment lookup
// matches getenv.
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "os/console.h"

using namespace engine_placeholder;

namespace
{

// Runs `body` with standard output (fd 1) or standard error (fd 2) redirected to a temporary file and returns what was
// written.
template <class F> void Capture(int fd, char* out, usize capacity, F body)
{
    std::fflush(stdout);
    std::fflush(stderr);
    char path[] = "/tmp/engine_console_XXXXXX";
    const int tmp = mkstemp(path);
    REQUIRE(tmp >= 0);
    const int saved = dup(fd);
    dup2(tmp, fd);
    body();
    std::fflush(stdout);
    std::fflush(stderr);
    dup2(saved, fd);
    close(saved);
    lseek(tmp, 0, SEEK_SET);
    const long n = read(tmp, out, capacity - 1);
    out[n < 0 ? 0 : n] = '\0';
    close(tmp);
    unlink(path);
}

}  // namespace

TEST_CASE("os::Print and friends write to standard output", "[cstr][os]")
{
    char text[256];
    Capture(1, text, sizeof(text),
            []
            {
                os::Print("a%d|", 1);
                os::PrintText("b|");
                os::PrintLine("c");
                os::PrintBytes("dd", 2);
                os::Flush();
            });
    CHECK(std::strcmp(text, "a1|b|c\ndd") == 0);
}

TEST_CASE("os::FlushAll empties every C stream's buffer, not only the console's", "[cstr][os]")
{
    char path[] = "/tmp/engine_flush_all_XXXXXX";
    const int tmp = mkstemp(path);
    REQUIRE(tmp >= 0);
    FILE* stream = fdopen(dup(tmp), "w");  // a stream on a file is fully buffered
    REQUIRE(stream != nullptr);
    std::fputs("held", stream);
    char before[16] = {};
    CHECK(pread(tmp, before, sizeof(before) - 1, 0) == 0);  // still in the stream's buffer
    os::FlushAll();
    char after[16] = {};
    CHECK(pread(tmp, after, sizeof(after) - 1, 0) == 4);
    CHECK(std::strcmp(after, "held") == 0);
    std::fclose(stream);
    close(tmp);
    unlink(path);
}

TEST_CASE("os::PrintError and friends write to standard error", "[cstr][os]")
{
    char text[256];
    Capture(2, text, sizeof(text),
            []
            {
                os::PrintError("e%s|", "rr");
                os::PrintErrorText("t|");
                os::PrintErrorBytes("bb", 2);
                os::FlushError();
            });
    CHECK(std::strcmp(text, "err|t|bb") == 0);
}

TEST_CASE("os::GetEnvironment matches getenv", "[cstr][os]")
{
    setenv("ENGINE_CONSOLE_TEST_VALUE", "present", 1);
    CHECK(std::strcmp(os::GetEnvironment("ENGINE_CONSOLE_TEST_VALUE"), "present") == 0);
    unsetenv("ENGINE_CONSOLE_TEST_VALUE");
    CHECK(os::GetEnvironment("ENGINE_CONSOLE_TEST_VALUE") == nullptr);
    CHECK(os::GetEnvironment("PATH") == std::getenv("PATH"));
}
