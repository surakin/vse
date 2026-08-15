// Real entry point for the `engine_main`/`engine_main_asan` executables.
// Foundation-layer verification lives in /tests/foundation (Catch2,
// discovered via `ctest`) — this file has no test logic of its own.
#include "mem/arena_allocator.h"
#include "os/command_line.h"
#include "os/os.h"
#include "string/string.h"

using namespace engine_placeholder;

namespace {

void WriteLine(StringView label, StringView value) {
    ArenaAllocator scratch(256);
    StringBuilder<ArenaAllocator> line(scratch);
    line.Append(label).Append(value).Append('\n');
    os::Write(1, line.CStr(), line.View().Length());
}

} // namespace

int main(int argc, char** argv) {
    os::StashNativeCommandLine(argc, argv);

    ArenaAllocator cmd_line_arena(4096);
    os::CommandLine<ArenaAllocator> cmd_line = os::CommandLine<ArenaAllocator>::FromNative(cmd_line_arena);

    WriteLine("program: ", cmd_line.ProgramName());
    for (usize i = 0; i < cmd_line.ArgCount(); ++i) {
        WriteLine("arg: ", cmd_line.Arg(i));
    }
    for (usize i = 0; i < cmd_line.FlagCount(); ++i) {
        ArenaAllocator scratch(256);
        StringBuilder<ArenaAllocator> line(scratch);
        line.Append("flag: --").Append(cmd_line.FlagName(i));
        if (cmd_line.FlagHasValue(i)) {
            line.Append('=').Append(cmd_line.FlagValue(i));
        }
        line.Append('\n');
        os::Write(1, line.CStr(), line.View().Length());
    }

    return 0;
}
