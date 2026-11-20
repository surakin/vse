#pragma once

#include "os/command_line.h"
#include "types.h"

//! What every program (the demos, the benches, a game's `main`) does at startup for debugging, in one call: prepare the
//! stack-trace and crash-context facilities, stamp the build id in the log, and install the crash handler when it is
//! wanted.
//!
//! Whether the crash handler is installed: the argument `--crash-handler=on|off` wins, then the environment variable
//! `ENGINE_CRASH_HANDLER` (`1`/`0`, `on`/`off`), then the build's default: on in Debug and RelWithDebInfo builds, off
//! in Release. A program without a command line (mobile, console) passes none and uses the environment and the default.
namespace vse::debug
{

/// The deliberate fault a program was asked to cause with `--crash-test=...`, for checking the whole crash path by
/// hand.
enum class CrashTestKind : u8
{
    None,    ///< No test requested.
    Segv,    ///< Write through a null pointer.
    Abort,   ///< Call `abort`.
    Assert,  ///< Fail an `ENGINE_ASSERT`.
    Ensure   ///< Fail an `ENGINE_ENSURE`; the program carries on.
};

/// The `--crash-handler=on|off` option [[debug::StartProgramDebugging]] reads, for a program that declares its own
/// options to [[os::CommandLine]] (list it among them, or the command line is refused).
inline constexpr os::CommandLineOption kCrashHandlerOption = {"crash-handler", true};
/// The `--crash-test=KIND` option, as above.
inline constexpr os::CommandLineOption kCrashTestOption = {"crash-test", true};

/// What [[debug::StartProgramDebugging]] decided and did.
///
/// - Thread-safety: thread-compatible. A plain value.
struct ProgramDebugSetup
{
    /// True if the crash handler was installed.
    bool crash_handler_installed = false;
    /// The crash test requested on the command line, if any.
    CrashTestKind crash_test = CrashTestKind::None;
};

/// What the command line and environment asked for, before anything is installed. Separate so it can be tested.
///
/// - Thread-safety: thread-compatible. A plain value.
struct ProgramDebugRequest
{
    /// Whether to install the crash handler.
    bool crash_handler = false;
    /// The requested crash test.
    CrashTestKind crash_test = CrashTestKind::None;
    /// The last argument whose value was not understood (and so ignored), or null; [[debug::StartProgramDebugging]]
    /// warns about it.
    const char* ignored_argument = nullptr;
};

/// Works out what a program should do from its arguments, the value of `ENGINE_CRASH_HANDLER`, and the build's default.
///
/// Unknown arguments are ignored (the program parses its own). An unknown value for a known flag is ignored too,
/// leaving the setting from the lower-priority source.
///
/// - Param argc: number of arguments; 0 for a program with no command line.
/// - Param argv: the arguments; borrowed for the call; may be null.
/// - Param environment_value: the text of `ENGINE_CRASH_HANDLER`, or null if unset; borrowed for the call.
/// - Param build_default: what to do when neither is given.
/// - Returns: the decisions.
/// - Fails: never.
/// - Thread-safety: thread-safe.
ProgramDebugRequest ParseProgramDebugRequest(int argc, const char* const* argv, const char* environment_value,
                                             bool build_default);

/// Reports whether `argument` is one of the arguments [[debug::StartProgramDebugging]] handles, so a program's own
/// argument parser can skip it instead of rejecting it as unknown.
/// - Param argument: one command-line argument; borrowed for the call.
/// - Returns: true for `--crash-handler=...` and `--crash-test=...`.
/// - Fails: never.
/// - Thread-safety: thread-safe.
bool IsProgramDebugArgument(const char* argument);

/// Prepares the debugging facilities for this program and installs the crash handler if wanted. Call first thing in
/// `main`.
///
/// Runs [[debug::InitDebugFacilities]] and [[debug::InitCrashContext]], logs one `build ...` line (build id, platform,
/// configuration) on its own `LogBuild` channel (always at Info), and installs the crash handler as described at the
/// top of this header.
///
/// - Param argc: number of arguments; 0 for a program with no command line.
/// - Param argv: the arguments; borrowed for the call; may be null.
/// - Returns: what was decided; a failed install is logged and reported as `crash_handler_installed == false`.
/// - Fails: never aborts; see [[debug::InstallCrashHandler]] for the failures it logs.
/// - Thread-safety: thread-safe; call once, before other threads start.
ProgramDebugSetup StartProgramDebugging(int argc, const char* const* argv);

/// Causes the fault asked for with `--crash-test=...`; does nothing for [[debug::CrashTestKind::None]].
///
/// Call it right after [[debug::StartProgramDebugging]], before the program creates a window or a device, so the crash
/// path can be checked without a display or a GPU. Does not return for every kind except `Ensure`.
///
/// - Param kind: the fault to cause.
/// - Fails: by design, the program dies from the fault (`Ensure` returns after reporting).
/// - Thread-safety: thread-safe.
void RunCrashTest(CrashTestKind kind);

}  // namespace vse::debug
