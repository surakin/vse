#pragma once

#include "types.h"

//! Two registrations through which the debugging module (layer 7) reaches code below it without being included by it.
//!
//! - [[FailureReportHooks]]: what the default assert handler and the error-log trace add to a bare message (a stack trace, the crash
//!   context, a look at the debugger).
//! - [[SetThreadStartHook]]: what a thread does first so a crash on it is reported by name (registration with the crash handler).
//!
//! `debug::InstallDiagnosticsHooks` (called by `debug::InitDebugFacilities`) installs both. Until then the base behavior is complete on its
//! own: a failed assert prints its message line and aborts, an ensure prints its line and continues, and a thread starts unregistered.

namespace engine_placeholder {

/// Callbacks that complete an assert or error report. Any member may be null.
///
/// Every callback may run while the process is in a bad state (a failed assert can come from inside the allocator), so each must not
/// allocate or take a lock that ordinary code can hold.
///
/// Thread-safety: thread-compatible. A table installed with [[SetFailureReportHooks]] must not change afterwards.
/// Ownership: the table is borrowed and must have static storage duration.
struct FailureReportHooks {
    /// Appends a stack trace of the calling thread to `buffer` (room for `capacity` bytes), leaving out `skip_frames` frames counted from the
    /// caller of the hook (the hook's own frame is not counted). Returns the number of bytes appended; 0 if no trace could be taken.
    usize (*append_stack_trace)(char* buffer, usize capacity, u32 skip_frames);
    /// Writes the crash context (program, machine, subsystem state, recent log) through `write`, in pieces, each passed with `context`.
    void (*write_crash_context)(void (*write)(void* context, const char* data, usize length), void* context);
    /// Returns true if a debugger is attached to the process.
    bool (*debugger_attached)();
};

/// Installs the table [[DefaultAssertHandler]] and the error trace of [[Log]] use to complete their reports.
///
/// Param hooks: the table, or null to go back to the bare report; borrowed, static storage duration.
/// Returns: the table installed before, or null.
/// Ownership: the table stays owned by the caller.
/// Thread-safety: thread-safe. One atomic store; a failure on another thread meanwhile sees the old or the new table, both complete.
const FailureReportHooks* SetFailureReportHooks(const FailureReportHooks* hooks);

/// Returns the installed [[FailureReportHooks]], or null.
/// Thread-safety: thread-safe.
const FailureReportHooks* GetFailureReportHooks();

/// The function a new thread's entry calls through [[NotifyThreadStart]]; it receives a short thread name (for crash reports), borrowed for the call, which may be null.
using ThreadStartHook = void (*)(const char* name);

/// Installs the function [[NotifyThreadStart]] calls.
///
/// A thread that started before the hook was installed is not registered later: registration with the crash handler only succeeds
/// once the handler is installed, so such a thread was never covered either.
///
/// Param hook: the function, or null to remove it.
/// Returns: the hook installed before, or null.
/// Thread-safety: thread-safe. One atomic store.
ThreadStartHook SetThreadStartHook(ThreadStartHook hook);

/// Called first thing on a new thread (the I/O thread, the job workers) with the thread's name. Does nothing until a hook is installed.
///
/// Param name: a short name for the thread; borrowed for the call.
/// Thread-safety: thread-safe.
void NotifyThreadStart(const char* name);

} // namespace engine_placeholder
