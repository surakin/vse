#pragma once

#include <cstdio>
#include <cstdlib>

#include "cstr/varargs.h"
#include "debug/compiler.h"
#include "types.h"

//! Console output and process control (Phase 16): the engine's spelling of `printf`, `fprintf(stderr, ...)`, `puts`, `fflush`,
//! `abort`, `exit`, `_Exit` and `getenv`. Output goes through the C library's buffered streams, so it interleaves with other users of
//! `stdout` and `stderr` as before. Writing to a descriptor without buffering is [[os::Write]]; files are the `io` module's.
namespace engine_placeholder::os {

/// Formats and writes to standard output (`printf`).
///
/// Returns: the number of characters written, or negative on an output error.
/// Thread-safety: thread-safe. One call's text is not interleaved with another's.
inline int Print(const char* format, ...) ENGINE_PRINTF_FORMAT(1, 2);
inline int Print(const char* format, ...) {
    VarArgs args;
    ENGINE_VA_START(args, format);
    const int written = std::vfprintf(stdout, format, args);
    ENGINE_VA_END(args);
    return written;
}

/// Formats and writes to standard error (`fprintf(stderr, ...)`).
///
/// Returns: the number of characters written, or negative on an output error.
/// Thread-safety: thread-safe.
inline int PrintError(const char* format, ...) ENGINE_PRINTF_FORMAT(1, 2);
inline int PrintError(const char* format, ...) {
    VarArgs args;
    ENGINE_VA_START(args, format);
    const int written = std::vfprintf(stderr, format, args);
    ENGINE_VA_END(args);
    return written;
}

/// Writes already formatted text to standard output without a newline (`fputs(text, stdout)`).
///
/// Thread-safety: thread-safe.
inline void PrintText(const char* text) { std::fputs(text, stdout); }
/// Writes already formatted text to standard error without a newline (`fputs(text, stderr)`).
///
/// Thread-safety: thread-safe.
inline void PrintErrorText(const char* text) { std::fputs(text, stderr); }
/// Writes `text` and a newline to standard output (`puts`).
///
/// Thread-safety: thread-safe.
inline void PrintLine(const char* text) { std::puts(text); }
/// Writes `size` bytes to standard output (`fwrite(data, 1, size, stdout)`); returns the number written.
///
/// Thread-safety: thread-safe.
inline usize PrintBytes(const void* data, usize size) { return std::fwrite(data, 1, size, stdout); }
/// Writes `size` bytes to standard error (`fwrite(data, 1, size, stderr)`); returns the number written.
///
/// Thread-safety: thread-safe.
inline usize PrintErrorBytes(const void* data, usize size) { return std::fwrite(data, 1, size, stderr); }
/// Flushes standard output (`fflush(stdout)`).
///
/// Thread-safety: thread-safe.
inline void Flush() { std::fflush(stdout); }
/// Flushes standard error (`fflush(stderr)`).
///
/// Thread-safety: thread-safe.
inline void FlushError() { std::fflush(stderr); }

/// Ends the process abnormally at once, without running exit handlers (`abort`).
///
/// Thread-safety: thread-safe. Async-signal-safe.
[[noreturn]] inline void Abort() noexcept { std::abort(); }
/// Ends the process after running exit handlers and flushing streams (`exit`).
///
/// Thread-safety: thread-safe.
[[noreturn]] inline void Exit(int code) { std::exit(code); }
/// Ends the process at once with `code`, without running exit handlers or flushing streams (`_Exit`).
///
/// Thread-safety: thread-safe. Async-signal-safe.
[[noreturn]] inline void ExitNow(int code) noexcept { std::_Exit(code); }

/// The value of the environment variable `name`, or null when it is not set (`getenv`). The text is borrowed and valid until the
/// environment changes.
///
/// Settings read the environment through the settings system; this is for the bootstrap variables listed in
/// `tools/config_access_check/allowlist.txt`.
/// Thread-safety: thread-compatible. Another thread changing the environment meanwhile is a race, as for `getenv`.
inline const char* GetEnvironmentVariable(const char* name) noexcept { return std::getenv(name); }

} // namespace engine_placeholder::os
