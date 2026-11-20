#pragma once

#include <cstdio>
#include <cstdlib>

#include "cstr/varargs.h"
#include "debug/compiler.h"
#include "types.h"

//! Process control (Phase 16): the engine's spelling of `fflush`, `abort`, `_Exit` and `getenv` (`os::Exit`, which runs
//! the exit handlers, is in `os/os.h`). A program's output is not here: it goes through the log system (`out::Print`,
//! `log/output.h`), which sends it to the console and every other log device; what is left here is flushing the C
//! streams that libraries write to. Writing to a descriptor without buffering is [[os::Write]]; files are the `io`
//! module's.
namespace vse::os
{

/// Flushes standard output (`fflush(stdout)`).
///
/// - Thread-safety: thread-safe.
inline void Flush()
{
    std::fflush(stdout);
}
/// Flushes standard error (`fflush(stderr)`).
///
/// - Thread-safety: thread-safe.
inline void FlushError()
{
    std::fflush(stderr);
}
/// Flushes every open C stream, standard output and standard error included (`fflush(nullptr)`): what a process that
/// ends without running exit handlers does first, so nothing a library wrote through the C streams is lost.
///
/// - Thread-safety: thread-safe.
inline void FlushAll()
{
    std::fflush(nullptr);
}

/// Ends the process abnormally at once, without running exit handlers (`abort`).
///
/// - Thread-safety: thread-safe. Async-signal-safe.
[[noreturn]] inline void Abort() noexcept
{
    std::abort();
}
/// Ends the process at once with `code`, without running exit handlers or flushing streams (`_Exit`).
///
/// - Thread-safety: thread-safe. Async-signal-safe.
[[noreturn]] inline void ExitNow(int code) noexcept
{
    std::_Exit(code);
}

/// The value of the environment variable `name`, or null when it is not set (`getenv`). The text is borrowed and valid
/// until the environment changes.
///
/// Settings read the environment through the settings system; this is for the bootstrap variables listed in
/// `tools/config_access_check/allowlist.txt`.
/// - Thread-safety: thread-compatible. Another thread changing the environment meanwhile is a race, as for `getenv`.
inline const char* GetEnvironment(const char* name) noexcept
{
    return std::getenv(name);
}

}  // namespace vse::os
