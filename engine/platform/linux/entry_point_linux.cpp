// The entry point of a windowed program on Linux (Phase 13 Day 158): `main`'s own arguments, already UTF-8.
#include "platform/entry_point.h"

#include "os/command_line.h"

namespace vse::os::detail
{

int RunWindowedProgram(int argc, char** argv, WindowedProgramEntry entry)
{
    StashNativeCommandLine(argc, argv);
    return entry(argc, argv);
}

}  // namespace vse::os::detail
