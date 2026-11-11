#pragma once

// Starting slangc and spirv-val from shader_mint --permute (Phase 13 Day 163): one interface, two implementations,
// run_process_posix.cpp (posix_spawnp) and windows/run_process_windows.cpp (CreateProcessW). The child inherits
// standard output and standard error, so its messages reach the build log.

#include "text.h"

namespace engine_placeholder::shader_mint
{

// Appends `arg` to a Windows command line the way CommandLineToArgvW and the C runtime split it back into the same
// argument: unquoted when it has no space, tab, newline or quote and is not empty; otherwise in quotes, with each quote
// escaped and the backslashes before a quote (or before the closing quote) doubled. A space separates it from what is
// already in `line`. Portable, so its tests run on every machine.
void AppendWindowsArgument(Text* line, StringView arg);

// Runs `args[0]` with the arguments `args` (the first is the program) and waits for it. The program is looked up as the
// platform does when it has no directory (PATH on POSIX; CreateProcessW's search on Windows): pass an absolute path.
// Returns true if it exited with status 0; otherwise prints how it failed on stderr.
bool RunProcess(const Vector<Text>& args);

}  // namespace engine_placeholder::shader_mint
