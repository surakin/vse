#pragma once

#include "types.h"

//! Making sure a crash leaves a post-mortem artifact a developer can open later, and saying where it will be.
//!
//! This is the platform-neutral idea; each platform implements it its own way:
//!   - Linux: a core file, or a handoff to `systemd-coredump` (`coredumpctl`); the process raises its core size limit.
//!   - Windows (Phase 13): Windows Error Reporting `LocalDumps`.
//!   - macOS (Phase 26): the system's core files and crash reports.
//!   - Android: tombstones, which the system always writes ([[debug::PostMortemResult::Enabled]] with nothing to do).
//!   - iOS and consoles with their own crash system: [[debug::PostMortemResult::Unsupported]].
//! The minidump the crash handler writes ([[debug::InstallCrashHandler]]) is separate and always on; a core dump is bigger and
//! holds the whole process memory, so it is a developer's tool, off by default.
namespace engine_placeholder::debug {

/// Largest text in a [[debug::PostMortemReport]].
inline constexpr usize kPostMortemTextBytes = 256;

/// What [[debug::EnablePostMortemDumps]] achieved.
enum class PostMortemResult : u8 {
    Enabled,    ///< The platform will write a post-mortem artifact for a fatal fault.
    Limited,    ///< It may, but something limits it; `note` in the report says what and how to lift it.
    Unsupported ///< This platform has no such artifact the program can enable.
};

/// How to enable post-mortem dumps.
///
/// - Thread-safety: thread-compatible. A plain value.
struct PostMortemOptions {
    /// Largest artifact to allow, in bytes; 0 means as large as the system permits.
    u64 size_limit_bytes = 0;
};

/// What [[debug::EnablePostMortemDumps]] did and where the artifact will go.
///
/// - Thread-safety: thread-compatible. A plain value.
struct PostMortemReport {
    /// The outcome.
    PostMortemResult result = PostMortemResult::Unsupported;
    /// The size limit before the call, in bytes; the maximum `u64` value means unlimited.
    u64 previous_limit_bytes = 0;
    /// The size limit after the call, in bytes; the maximum `u64` value means unlimited.
    u64 current_limit_bytes = 0;
    /// Where the artifact goes, as a sentence for a person (`stored by systemd-coredump; list them with coredumpctl`).
    char destination[kPostMortemTextBytes] = {};
    /// Why the result is [[debug::PostMortemResult::Limited]], and what to do about it; empty otherwise.
    char note[kPostMortemTextBytes] = {};
};

/// Allows the platform to write a post-mortem artifact if the process dies from a fatal fault, and reports where it will go.
///
/// On Linux this raises the soft core size limit to the hard limit (or to `options.size_limit_bytes` if that is lower), makes
/// the process dumpable (only when the limit was raised), and reads `core_pattern` to say where the core goes. It changes only this process.
///
/// - Param options: how to enable.
/// - Returns: what was done; never fails hard.
/// - Fails: reports [[debug::PostMortemResult::Limited]] when the system's hard limit is zero (nothing the process can do) and
///   [[debug::PostMortemResult::Unsupported]] where there is no such artifact.
/// - Thread-safety: thread-safe. Not async-signal-safe.
/// - Backend: Windows: reads WER's LocalDumps values under HKLM and writes nothing (Limited, with the values to set, when they are absent); macOS: setrlimit(RLIMIT_CORE) and /cores; Android: Enabled with nothing to do.
PostMortemReport EnablePostMortemDumps(const PostMortemOptions& options);

} // namespace engine_placeholder::debug
