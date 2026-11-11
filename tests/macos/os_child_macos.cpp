// The child of tests/macos/os_macos_test.cpp (Phase 26 Day 301): reads its arguments through
// os::CommandLine::FromNative and writes each back on its own line with os::Write, then one line through the log, so
// the parent can compare the bytes that crossed the pipe.
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "os/command_line.h"
#include "os/os.h"

using namespace engine_placeholder;

int main(int argc, char** argv)
{
    os::StashNativeCommandLine(argc, argv);
    ArenaAllocator arena(1 << 16);
    const auto command_line = os::CommandLine<ArenaAllocator>::FromNative(arena);
    for (usize i = 0; i < command_line.ArgCount(); ++i)
    {
        const StringView arg = command_line.Arg(i);
        os::Write(1, arg.Data(), arg.Length());
        os::Write(1, "\n", 1);
    }
    Log(LogCore, LogVerbosity::Warn,
        "gr\xC3\xBC\xC3\x9F"
        "e \xE6\x97\xA5\xE6\x9C\xAC \xE2\x9C\x93");  // grüße 日本 ✓
    return 0;
}
