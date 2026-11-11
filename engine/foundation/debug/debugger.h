#pragma once

#include "types.h"

//! Asking whether a debugger is attached to the running process.
namespace engine_placeholder::debug
{

/// Reports whether a debugger is attached to this process.
///
/// The answer is cached for up to one second, so asking on a failure path costs a clock read, not a system call. On a
/// platform with no way to tell, the answer is false.
///
/// Any tracer counts, not only an interactive debugger: on Linux a process run under `strace` or `rr` reports true too,
/// and an assert failure then breaks (SIGTRAP) instead of aborting.
///
/// - Returns: true if a debugger (or another tracer) is attached; false if none is or the platform cannot tell.
/// - Thread-safety: thread-safe.
/// - Backend: Windows: IsDebuggerPresent; macOS: sysctl kern.proc.pid with the P_TRACED flag.
bool IsDebuggerAttached();

}  // namespace engine_placeholder::debug
