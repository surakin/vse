#pragma once

#include "types.h"

//! The current thread's id and the name it gave itself, for crash reports.
namespace engine_placeholder::debug {

/// Bytes kept of a thread name, including the null byte.
constexpr usize kMaxThreadNameBytes = 32;

/// Returns the operating system's id for the calling thread (the number a debugger and `top` show).
/// Thread-safety: thread-safe. Async-signal-safe.
u64 CurrentThreadId();

/// Gives the calling thread a name to appear in crash reports (for example `"worker 3"`), cut at [[debug::kMaxThreadNameBytes]] - 1 bytes.
///
/// Param name: a null-terminated string, or null to clear the name; borrowed for the call.
/// Thread-safety: thread-safe; it changes only the calling thread's name.
void SetCrashThreadName(const char* name);

/// Returns the name the calling thread gave itself, or an empty string.
/// Returns: a null-terminated string that stays valid for the life of the thread.
/// Ownership: the string belongs to the thread; do not free it or keep it past the thread's end.
/// Thread-safety: thread-safe. Async-signal-safe.
const char* CrashThreadName();

} // namespace engine_placeholder::debug
