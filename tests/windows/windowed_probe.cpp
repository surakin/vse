// A windowed program for tests/windows/win32_window_windows_test.cpp (Phase 13 Day 158): written with
// ENGINE_WINDOWED_PROGRAM, so it is a GUI program (`wWinMain`, subsystem 2). It opens no window. It prints, to its
// standard output:
//   arg <i> <hex>      each argument after the program name as EngineMain received it, hex UTF-8 bytes
//   stash <i> <hex>    the same arguments read back through os::CommandLine::FromNative
// and "stderr ok" to its standard error, then exits with 7, so a parent sees its output, its arguments and its exit
// code.
#include "os/console.h"

#include "mem/arena_allocator.h"
#include "os/command_line.h"
#include "os/os.h"
#include "platform/entry_point.h"
#include "types.h"

using namespace engine_placeholder;

namespace
{

void PrintHex(const char* label, int index, const char* text, usize length)
{
    os::Print("%s %d ", label, index);
    for (usize i = 0; i < length; ++i)
    {
        os::Print("%02x", static_cast<unsigned>(static_cast<u8>(text[i])));
    }
    os::Print("\n");
}

usize Length(const char* text)
{
    usize n = 0;
    while (text[n] != '\0')
    {
        ++n;
    }
    return n;
}

}  // namespace

int EngineMain(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        PrintHex("arg", i - 1, argv[i], Length(argv[i]));
    }
    ArenaAllocator arena(16 * 1024);
    const os::CommandLine<ArenaAllocator> line = os::CommandLine<ArenaAllocator>::FromNative(arena);
    for (usize i = 0; i < line.ArgCount(); ++i)
    {
        PrintHex("stash", static_cast<int>(i), line.Arg(i).Data(), line.Arg(i).Length());
    }
    os::Flush();
    os::Write(2, "stderr ok\n", 10);
    return 7;
}

ENGINE_WINDOWED_PROGRAM(EngineMain)
