#pragma once

#include "debug/crash_reason.h"
#include "debug/stack_trace.h"
#include "types.h"

//! The text report that goes with a crash or a failed assertion: why, on which thread, in which build on which machine,
//! the call stack, what the engine's subsystems say about their state, and the last lines the program logged.
//!
//! Everything it prints is prepared ahead of time (see crash_context.h, the log ring in log.h, and the state providers
//! below), so writing it allocates nothing and takes no lock and can run inside a crashing process. It writes in small
//! pieces, one section line at a time, so if something faults in the middle the part already written is intact.
namespace vse::debug
{

/// Most state providers that can be registered.
constexpr usize kMaxCrashStateProviders = 16;
/// Bytes a state provider may write, including the null byte; the report cuts anything longer.
constexpr usize kCrashStateTextBytes = 256;

/// Writes the text of one state provider into `out`.
///
/// **The rules, because this runs in a crashing process:** read only plain memory that is always valid (a few atomics
/// or copies the subsystem keeps up to date), call nothing that allocates or locks or reads a file, write at most
/// `capacity` bytes including the terminating null byte, and finish quickly. A provider that breaks these rules can
/// lose the rest of the report.
///
/// `context` is the pointer given at registration; `out` is where to write the null-terminated text, `capacity` bytes
/// ([[debug::kCrashStateTextBytes]]).
using CrashStateFn = void (*)(void* context, char* out, usize capacity);

/// Adds a named piece of engine state to every crash report.
///
/// Register during startup, once per subsystem (the RHI adds the adapter, the renderer the frame number, the window
/// layer the backend). The table is append-only and holds [[debug::kMaxCrashStateProviders]] entries.
///
/// - Param name: a short label such as `"gpu"`; must outlive the program (a literal).
/// - Param fn: the provider; see [[debug::CrashStateFn]] for what it may do.
/// - Param context: passed to `fn`; must stay valid for the life of the program.
/// - Returns: true if the provider was added.
/// - Fails: returns false if the table is full or `name` or `fn` is null.
/// - Ownership: the table stores the pointers; it owns none of what they point at.
/// - Thread-safety: thread-safe.
bool RegisterCrashStateProvider(const char* name, CrashStateFn fn, void* context);

/// Removes every registered provider. For tests.
/// - Thread-safety: not-thread-safe. Call only while no report is being written.
void ResetCrashStateProvidersForTesting();

/// Receives a piece of report text.
///
/// `context` is the pointer given in the sink; `data` points at `length` bytes, borrowed for the call and not
/// null-terminated.
using CrashWriteFn = void (*)(void* context, const char* data, usize length);

/// Where a report goes: a function and the pointer it gets.
///
/// - Thread-safety: thread-compatible. A plain value.
struct CrashReportSink
{
    /// Called with each piece of the report, in order.
    CrashWriteFn write = nullptr;
    /// Passed to `write`.
    void* context = nullptr;
};

/// What a crash report is about.
///
/// - Thread-safety: thread-compatible. A plain value; the pointers are borrowed for the [[debug::WriteCrashReport]]
/// call.
struct CrashReportInfo
{
    /// Why the process is being reported.
    CrashReason reason = CrashReason::None;
    /// True if `fault_address` is meaningful (an access violation, a bus error).
    bool has_fault_address = false;
    /// The address that was accessed.
    u64 fault_address = 0;
    /// A line of explanation (an assertion's message); null for none.
    const char* message = nullptr;
    /// The call stack to print; null for none.
    const StackTrace* trace = nullptr;
    /// Id of the thread that faulted; 0 means the calling thread.
    u64 thread_id = 0;
    /// False when the thread that faulted could not be found (Windows writes the report on the crash library's own
    /// thread, and an `abort()` on a thread never registered with [[debug::RegisterThreadForCrashHandling]] leaves no
    /// trace of it); the report then says `thread: unknown` instead of naming the calling thread.
    bool thread_known = true;
    /// Name of the thread that faulted, for a report written on another thread (Windows writes it on the crash
    /// library's own thread); null means the calling thread's [[debug::CrashThreadName]].
    const char* thread_name = nullptr;
};

/// Writes the whole report: a header with the reason and the thread, the stack trace, then
/// [[debug::WriteCrashContext]].
///
/// - Param sink: where the text goes.
/// - Param info: what to report.
/// - Thread-safety: thread-safe. Async-signal-safe once [[debug::InitDebugFacilities]] and [[debug::InitCrashContext]]
/// have run
///   (it never calls `malloc` and takes no lock of its own).
void WriteCrashReport(const CrashReportSink& sink, const CrashReportInfo& info);

/// Writes the sections that describe the program and its surroundings: the build, the platform, the state providers and
/// the most recent log lines. [[debug::WriteCrashReport]] ends with this; an assertion failure prints it after its own
/// message.
///
/// - Param sink: where the text goes.
/// - Thread-safety: thread-safe. Async-signal-safe once [[debug::InitCrashContext]] has run.
void WriteCrashContext(const CrashReportSink& sink);

/// How many of the most recent log lines a report includes.
constexpr usize kCrashReportLogLines = 32;

}  // namespace vse::debug
