#pragma once

#include "build_defs.h"

//! The entry point of a windowed program (Phase 13 Day 158), written once in source so no build file chooses it per platform.
//!
//! A windowed program (a game, a demo) defines `int EngineMain(int argc, char** argv)` and, in one source file at global scope, writes
//! `ENGINE_WINDOWED_PROGRAM(EngineMain)` instead of `main`. The macro expands to the platform's own entry point: `main` on Linux, `wWinMain`
//! on Windows, where it makes the program a Windows (GUI) program: no console window opens when it is started from Explorer. Either way
//! `EngineMain` gets UTF-8 arguments, already stashed for [[os::CommandLine::FromNative]], and its result is the exit code.
//!
//! The Windows subsystem comes with the entry point, from source: the macro's expansion carries a linker directive for `/SUBSYSTEM:WINDOWS`,
//! which wins over the `/SUBSYSTEM:CONSOLE` the build gives every program, so tests and command-line tools, which keep a plain `main`, stay
//! console programs, and no build file chooses a subsystem per target.
//! A Windows GUI program writes its output to the handles it inherited (a pipe, a redirected file, ctest's capture) as they are. With none,
//! started from a terminal, it attaches to that terminal's console, so its output appears there (after the shell's next prompt: a shell does
//! not wait for a GUI program); started from Explorer, there is no console and the writes fail (the log file still has everything).

namespace engine_placeholder::os {

/// The function a windowed program defines in place of `main`: UTF-8 arguments, the exit code as the result.
using WindowedProgramEntry = int (*)(int argc, char** argv);

namespace detail {

/// Runs a windowed program: gives standard output and error somewhere to go, stashes the UTF-8 arguments
/// ([[os::StashNativeCommandLine]]) and returns `entry`'s result. Called by [[ENGINE_WINDOWED_PROGRAM]]'s expansion only.
///
/// - Param argc: `main`'s count on Linux; ignored on Windows, which reads the command line itself (`GetCommandLineW`).
/// - Param argv: `main`'s arguments on Linux; ignored on Windows, which converts its wide arguments to UTF-8.
/// - Param entry: the program's own entry point.
/// - Returns: `entry`'s result.
/// - Fails: on Windows, returns 1 without calling `entry` if the command line cannot be read or the arguments cannot be stored.
/// - Backend: `platform/<os>/entry_point_<os>.cpp`.
int RunWindowedProgram(int argc, char** argv, WindowedProgramEntry entry);

} // namespace detail
} // namespace engine_placeholder::os

#if ENGINE_PLATFORM_WINDOWS
/// Defines the program's entry point as a call of `entry` (see the file comment). Write it once, at global scope.
// `wWinMain` with the plain types the C runtime expects (`HINSTANCE` is `HINSTANCE__*`), so the file need not include <windows.h>, and the
// directive that makes the program a GUI program (cmake/EngineCompilerOptions.cmake says why it is not left to the linker's inference).
#define ENGINE_WINDOWED_PROGRAM(entry)                                                                                                   \
    __pragma(comment(linker, "/SUBSYSTEM:WINDOWS"))                                                                                      \
    extern "C" int __stdcall wWinMain(struct HINSTANCE__*, struct HINSTANCE__*, wchar_t*, int) {                                         \
        return ::engine_placeholder::os::detail::RunWindowedProgram(0, nullptr, &(entry));                                               \
    }
#else
// The same, as `main` (the documentation is on the Windows definition above).
#define ENGINE_WINDOWED_PROGRAM(entry)                                                                                                   \
    int main(int argc, char** argv) { return ::engine_placeholder::os::detail::RunWindowedProgram(argc, argv, &(entry)); }
#endif
