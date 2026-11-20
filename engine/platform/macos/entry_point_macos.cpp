// The entry point of a windowed program on macOS (Phase 21 Day 295; platform/entry_point.h): `main`'s own arguments,
// already UTF-8, and the application object set up before the program runs (cocoa::EnsureApplication: a regular,
// dock-visible app with a Quit menu). Without a window server (an ssh login) the program still runs; only opening a
// window fails.
#include "platform/entry_point.h"

#include "os/command_line.h"
#include "platform/macos/cocoa_window.h"

namespace vse::os::detail
{

int RunWindowedProgram(int argc, char** argv, WindowedProgramEntry entry)
{
    StashNativeCommandLine(argc, argv);
    cocoa::EnsureApplication();  // logs why when it cannot; a window opened later says so again
    return entry(argc, argv);
}

}  // namespace vse::os::detail
