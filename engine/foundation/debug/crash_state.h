#pragma once

#include "types.h"

#include "sync/atomic.h"

//! Two ready-made kinds of engine state for the crash report, so a subsystem does not have to write a provider function:
//! a line of text set once ("gpu: NVIDIA ..., driver 1.2.3") and a counter that is kept up to date ("frame: 41231").
//! Both appear in the `--- state ---` section of every report. See [[debug::RegisterCrashStateProvider]] for the general form.
namespace engine_placeholder::debug {

/// Longest text a [[debug::SetCrashStateText]] entry keeps, including the terminating null byte.
inline constexpr usize kCrashStateEntryTextBytes = 128;

/// Sets the text shown for `name` in the crash report, creating the entry on first use.
///
/// Call during startup (when the adapter is chosen, when the window opens). Setting the text again replaces it; a report
/// written at that very moment may show a mix of old and new text, which is acceptable for a crash report.
///
/// - Param name: a short label (at most 31 characters, longer is cut); copied.
/// - Param text: the text; copied and cut to [[debug::kCrashStateEntryTextBytes]]; null clears it.
/// - Returns: true if the entry exists or was created.
/// - Fails: returns false if the table of entries is full or `name` is null.
/// - Ownership: the text and the name are copied.
/// - Thread-safety: thread-safe against other callers; not async-signal-safe.
bool SetCrashStateText(const char* name, const char* text);

/// Returns the counter shown for `name` in the crash report, creating the entry on first use. Store to it as often as needed.
///
/// - Param name: a short label (at most 31 characters, longer is cut); copied.
/// - Returns: the counter, valid for the life of the program; null if the table of entries is full or `name` is null.
/// - Fails: returns null as described.
/// - Ownership: the counter belongs to the crash state table; the caller keeps the pointer and never frees it.
/// - Thread-safety: thread-safe. Storing to the returned counter is lock-free.
Atomic<u64>* CrashStateCounter(const char* name);

/// Removes every entry made by [[debug::SetCrashStateText]] and [[debug::CrashStateCounter]] and every crash state provider.
/// Counters handed out before are no longer shown (they stay valid memory but belong to no entry). For tests.
/// - Thread-safety: not-thread-safe. Call only while no report is being written and no thread uses a counter.
void ResetCrashStateForTesting();

} // namespace engine_placeholder::debug
