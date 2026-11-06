#pragma once

#include "types.h"

//! What a crash report says about the program and the machine, collected once at startup.
//!
//! Filled before anything can crash, so the crash path only reads fixed buffers: no allocation, no file access.
namespace engine_placeholder::debug {

/// Bytes kept of the build id as hex text (a 20-byte id is 40 digits), including the null byte.
constexpr usize kMaxBuildIdText = 65;
/// Bytes kept of the executable path, including the null byte.
constexpr usize kMaxContextPath = 256;
/// Bytes kept of the command line, including the null byte.
constexpr usize kMaxContextCommandLine = 1024;
/// Bytes kept of each short text field of the context, including the null byte.
constexpr usize kMaxContextText = 96;

/// Facts about the running program and machine, for crash reports.
///
/// Every text field is null-terminated and may be empty when the platform has nothing to say.
///
/// Thread-safety: immutable. Written once by [[debug::InitCrashContext]]; read from any thread and from signal handlers.
struct CrashContext {
    /// The executable's build id as lowercase hex (the GNU build-id note on Linux); empty if the binary has none. It ties a
    /// dump to the exact binary and its symbol file.
    char build_id[kMaxBuildIdText] = {};
    /// The engine's version text.
    char engine_version[kMaxContextText] = {};
    /// Path of the running executable.
    char executable[kMaxContextPath] = {};
    /// The command line, arguments joined by single spaces and cut at [[debug::kMaxContextCommandLine]] - 1 bytes; empty on
    /// a platform or program without one.
    char command_line[kMaxContextCommandLine] = {};
    /// Operating system name, for example `Linux`.
    char os_name[kMaxContextText] = {};
    /// Operating system version, for example the kernel release.
    char os_version[kMaxContextText] = {};
    /// CPU architecture, for example `x86_64`.
    char architecture[kMaxContextText] = {};
    /// Device model on phones, tablets and consoles; empty on a desktop.
    char device_model[kMaxContextText] = {};
    /// Where a core dump goes, if [[debug::SetCoreDumpDescription]] was called; empty otherwise.
    char core_dumps[256] = {};
    /// The CPU's marketing name; empty if unknown.
    char cpu_brand[kMaxContextText] = {};
    /// Number of logical processors that are online; 0 if unknown.
    u32 cpu_count = 0;
    /// Installed physical memory in bytes; 0 if unknown.
    u64 total_memory_bytes = 0;
    /// When the program started, in seconds since 1970-01-01 UTC; 0 if unknown.
    u64 start_time_unix = 0;
};

/// Collects the crash context.
///
/// Call it once, early in `main`, before the crash handler is installed. Later calls do nothing.
///
/// Param argc: number of arguments, or 0 if the program has no command line.
/// Param argv: the arguments; borrowed for the call; null if the program has none.
/// The first call fills the context. A later call changes nothing, except that it supplies the command line if the first call
/// had none (the crash handler and a failed assert initialize without arguments when the program has not).
/// Thread-safety: thread-safe. Not async-signal-safe: it reads files.
void InitCrashContext(int argc, const char* const* argv);

/// Records where a core dump will go (from [[debug::EnablePostMortemDumps]]), to be printed in every crash report.
/// Param text: a sentence for a person; copied (and cut to fit); null or empty clears it.
/// Thread-safety: thread-safe against other callers and [[debug::InitCrashContext]]; call during startup, because a crash report
///   reads it without a lock.
void SetCoreDumpDescription(const char* text);

/// Reports whether [[debug::InitCrashContext]] has run.
/// Thread-safety: thread-safe. Async-signal-safe.
/// Fails: never; returns false until initialized.
bool CrashContextInitialized();

/// Returns the crash context; all fields are empty or zero until [[debug::InitCrashContext]] has run.
/// Returns: the process-wide context.
/// Ownership: the context is static; do not free it.
/// Thread-safety: thread-safe. Async-signal-safe.
const CrashContext& GetCrashContext();

} // namespace engine_placeholder::debug
