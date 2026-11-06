#pragma once

#include "debug/crash_reason.h"
#include "types.h"

//! Running code that is supposed to crash, in a child process, and seeing what happened.
//!
//! The helper starts a fresh copy of the running program; it does not `fork`, because `fork` does not exist on every
//! platform and in a multithreaded process it leaves the child holding locks it can never release. The child runs one
//! function registered with [[ENGINE_DEATH_TEST_FN]]; the parent gets a [[debug::ChildResult]]. A program that wants to be
//! the child calls [[debug::RunDeathTestChildIfRequested]] early (the test support in tests/debug does it for Catch2).
namespace engine_placeholder::debug {

/// Bytes kept of a child's standard output and of its standard error; the rest is dropped and flagged.
constexpr usize kChildCaptureBytes = 32 * 1024;

/// Exit code of a child that was asked to run a death test nobody registered.
constexpr int kUnknownDeathTestExitCode = 125;

/// The function type of a death test: it receives the argument string the parent passed.
using DeathTestFn = void (*)(const char* argument);

/// One registered death test; created by [[ENGINE_DEATH_TEST_FN]], not by hand.
///
/// Thread-safety: not-thread-safe. The registry is built during static initialization and only read afterwards.
struct DeathTestEntry {
    /// The function's name, as passed to [[debug::RunInChild]].
    const char* name;
    /// The function.
    DeathTestFn fn;
    /// Next entry in the registry; set when the entry is registered.
    DeathTestEntry* next = nullptr;
};

/// Adds a [[debug::DeathTestEntry]] to the registry when constructed; used by [[ENGINE_DEATH_TEST_FN]].
///
/// Thread-safety: not-thread-safe. Registration happens during static initialization.
/// Ownership: the registry stores a pointer to the entry; the entry must outlive the program (static storage).
class DeathTestRegistrar {
public:
    /// Registers `entry`.
    /// Param entry: an entry with static storage duration; borrowed for the life of the program.
    explicit DeathTestRegistrar(DeathTestEntry& entry);
};

/// Looks up a registered death test by name.
/// Param name: the function name.
/// Returns: the entry, or `nullptr` if none has that name.
/// Fails: returns `nullptr` for an unknown name.
/// Ownership: the entry belongs to the registry.
/// Thread-safety: thread-safe once static initialization is over.
const DeathTestEntry* FindDeathTest(const char* name);

/// How a child is started and watched.
///
/// Thread-safety: thread-compatible. A plain value; the pointers it holds are borrowed for the [[debug::RunInChild]] call.
struct ChildOptions {
    /// Milliseconds before the child (and its process group) is killed; 0 means the default of 10000.
    u32 timeout_ms = 0;
    /// Keep the child's standard output; if false it is discarded.
    bool capture_stdout = true;
    /// Turn core dumps off in the child so a deliberate crash does not fill the disk.
    bool disable_core_dumps = true;
    /// Make the child's fatal faults reach its exit status by telling the sanitizers not to handle them.
    bool sanitizers_pass_faults = true;
    /// Extra environment entries of the form `NAME=value`, or null; borrowed for the call.
    const char* const* extra_env = nullptr;
    /// Number of entries in `extra_env`.
    usize extra_env_count = 0;
    /// Command-line arguments for the child program, or null for the default that selects the Catch2 test
    /// `[.death-child]`; borrowed for the call.
    const char* const* arguments = nullptr;
    /// Number of entries in `arguments`.
    usize argument_count = 0;
};

/// What happened to a child process.
///
/// Thread-safety: thread-compatible. A plain value, about 64 KB, normally returned by value into a local.
struct ChildResult {
    /// False if the platform cannot start a process; nothing else is meaningful then.
    bool supported = false;
    /// True if the child process was started.
    bool started = false;
    /// True if the child ended by returning an exit code.
    bool exited = false;
    /// The exit code, when `exited`.
    int exit_code = 0;
    /// True if the platform ended the child with a fault or a kill instead of an exit code.
    bool terminated = false;
    /// The platform's own code for that end (a signal number on POSIX, an exception status on Windows), when `terminated`.
    i64 native_code = 0;
    /// The code translated to a [[debug::CrashReason]]; [[debug::CrashReason::None]] for a normal exit or an outside kill.
    CrashReason reason = CrashReason::None;
    /// True if the child did not finish within the timeout and was killed.
    bool timed_out = false;
    /// The child's standard output, up to [[debug::kChildCaptureBytes]] bytes, not null-terminated.
    char stdout_text[kChildCaptureBytes] = {};
    /// Bytes in `stdout_text`.
    usize stdout_length = 0;
    /// True if the child wrote more standard output than fits.
    bool stdout_truncated = false;
    /// The child's standard error, up to [[debug::kChildCaptureBytes]] bytes, not null-terminated.
    char stderr_text[kChildCaptureBytes] = {};
    /// Bytes in `stderr_text`.
    usize stderr_length = 0;
    /// True if the child wrote more standard error than fits.
    bool stderr_truncated = false;

    /// Reports whether the captured standard error contains `text`.
    /// Param text: a null-terminated string; borrowed for the call.
    /// Returns: true if it occurs in the captured bytes.
    bool StderrContains(const char* text) const;
    /// Reports whether the captured standard output contains `text`.
    /// Param text: a null-terminated string; borrowed for the call.
    /// Returns: true if it occurs in the captured bytes.
    bool StdoutContains(const char* text) const;
};

/// Reports whether this platform can start child processes, so death tests can skip where it cannot.
/// Returns: true where [[debug::RunInChild]] works.
/// Thread-safety: thread-safe.
/// Backend: Windows: true (CreateProcess); macOS: true (posix_spawn, shared with Linux).
bool DeathTestsSupported();

/// Starts the running program as a child, runs the registered death test `name` in it, and reports what happened.
///
/// The child inherits the environment (plus `options.extra_env`) and nothing else: no threads, locks or memory of the
/// caller. Every path reaps the child and closes every descriptor it opened.
///
/// Param name: the registered function to run in the child.
/// Param argument: a short string the function receives; larger inputs go in a file whose name is the argument.
/// Param options: how to start and watch the child.
/// Returns: the result; `supported` is false where the platform cannot start a process.
/// Fails: `started` is false if the process could not be started; `timed_out` is set if it had to be killed. If the
///   platform never reported how the child ended (its status was reaped by someone else), `started` is true and both
///   `exited` and `terminated` are false.
/// Thread-safety: thread-safe. Each call owns its child and its pipes. The child's environment is built from the
///   process environment, so do not change it (`setenv`) from another thread while a call is running.
/// Backend: Windows: CreateProcess with a job object, exit codes decoded from NTSTATUS; macOS: posix_spawn, signals decoded as on Linux.
ChildResult RunInChild(const char* name, const char* argument = "", const ChildOptions& options = ChildOptions());

/// Makes the program act as a death-test child if the parent asked for it, otherwise returns.
///
/// When the environment says this process is a child, it runs the requested function and ends the process without
/// returning (exit code 0, or [[debug::kUnknownDeathTestExitCode]] for an unknown name); otherwise it returns false.
///
/// Returns: false when this process is not a death-test child.
/// Thread-safety: owner-thread-only. Call it from the main thread, before other threads start.
bool RunDeathTestChildIfRequested();

/// Ends the process now, without running exit handlers or static destructors.
/// Param code: the exit code.
/// Thread-safety: thread-safe. It ends the whole process.
[[noreturn]] void ExitChildProcess(int code);

/// Makes [[debug::RunInChild]] report an unsupported platform, for testing the skip path.
/// Param supported: false to simulate a platform that cannot start processes, true to restore.
/// Thread-safety: not-thread-safe. Call while no other thread uses the helper.
void SetDeathTestSupportForTesting(bool supported);

} // namespace engine_placeholder::debug

/// Defines a function that can run in a child process, and registers it under its own name.
///
/// Use it like a function definition: `ENGINE_DEATH_TEST_FN(crash_on_null) { ... }`. The function receives the parameter
/// `argument` (a `const char*`) from [[debug::RunInChild]]. The name must be unique in the program.
///
/// Param fn_name: the function's name, also the registered name.
#define ENGINE_DEATH_TEST_FN(fn_name)                                                                              \
    static void fn_name(const char* argument);                                                                     \
    static ::engine_placeholder::debug::DeathTestEntry fn_name##_death_entry{#fn_name, &fn_name};                  \
    static ::engine_placeholder::debug::DeathTestRegistrar fn_name##_death_registrar{fn_name##_death_entry};       \
    static void fn_name([[maybe_unused]] const char* argument)
