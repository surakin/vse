#pragma once

#include "types.h"

//! Assertion macro and failure hook, split out of log.h so low-level headers
//! can assert without depending on the string module.
namespace engine_placeholder {

/// Signature of the function that receives log and assert output: file
/// descriptor, buffer, byte count; returns bytes written or a negative value.
using WriteSinkFn = long (*)(int fd, const void* buf, usize len);
/// Signature of the function that terminates the process after a failed assert.
using ExitSinkFn = void (*)(int code);
/// Replaces the output sink shared by [[AssertFail]] and [[Log]], so tests can
/// capture output without redirecting file descriptors.
///
/// Param sink: replacement function; pass the default OS write function to
///   restore normal behavior. Not for production use.
/// Thread-safety: not-thread-safe. Call only while no thread logs or asserts.
void SetWriteSinkForTesting(WriteSinkFn sink);
/// Replaces the function [[AssertFail]] calls to terminate the process, so
/// tests can intercept the exit.
///
/// Param sink: replacement function; it must not return (for example it may
///   `longjmp`). Not for production use.
/// Thread-safety: not-thread-safe. Call only while no thread asserts.
void SetExitSinkForTesting(ExitSinkFn sink);

/// Prints `ASSERT FAILED: file:line message` to the write sink (file
/// descriptor 1) and terminates the process with exit code 1.
///
/// Param file: source file name, usually `__FILE__`.
/// Param line: source line number.
/// Param message: explanation text; the line is truncated at 512 bytes.
/// Aborts: always; this function never returns.
/// Thread-safety: thread-safe. Formats into a stack buffer; output goes
///   through the shared sinks.
[[noreturn]] void AssertFail(const char* file, int line, const char* message);

} // namespace engine_placeholder

/// Checks `cond` in every build configuration and calls [[AssertFail]] with the
/// current file and line when it is false.
///
/// Param cond: expression that must be true; evaluated exactly once.
/// Param msg: `const char*` message describing the violated condition.
/// Aborts: `cond` evaluates to false.
#define ENGINE_ASSERT(cond, msg)                                            \
    do {                                                                     \
        if (!(cond)) {                                                        \
            ::engine_placeholder::AssertFail(__FILE__, __LINE__, msg);         \
        }                                                                       \
    } while (false)
