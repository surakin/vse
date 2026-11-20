#pragma once

#include "debug/crash_reason.h"
#include "debug/stack_trace.h"
#include "types.h"
#include "build_defs.h"

// The per-platform half of crash reporting (crash_handler_sentry.cpp is the neutral half). Not part of the public API.
//
// The crash library cannot tell us what happened: on Linux its Breakpad backend passes no signal information to the
// `on_crash` hook, and runs the hook in a helper thread rather than the thread that faulted. So the platform installs a
// small recorder in front of the library's own handler. It runs on the faulting thread, builds a CrashEvent, hands it
// to the neutral half to write the report, and then calls the library's handler, which writes the dump. The recorder
// never replaces the library.
namespace vse::debug::detail
{

// What the platform learned about a fault, on the faulting thread.
struct CrashEvent
{
    CrashReason reason = CrashReason::None;
    bool has_fault_address = false;
    u64 fault_address = 0;
    u64 thread_id = 0;
    bool thread_known = true;           // false: the platform could not tell which thread faulted (the report says so)
    const char* thread_name = nullptr;  // the faulting thread's name when the event is built on another thread
                                        // (Windows); null: the caller's
    StackTrace trace;                   // from the fault, not from the recorder
};

// Called on the faulting thread, inside the recorder, at most once per crash; must be signal-safe.
using CrashEventFn = void (*)(const CrashEvent& event);

// Installs the recorder for the fatal signals, in front of the handlers already installed, which it calls afterwards.
// Call after the library has installed its own. Returns false if it could not.
bool InstallCrashEventHook(CrashEventFn fn);

// True while a fault is being reported (or the library's handler is running after the report). Uninstalling waits for
// it to end.
bool CrashReportInProgress();

// Gives back the calling thread's alternate signal stack, if this backend set one up (Linux; the other backends keep
// nothing per thread, so the call there does nothing).
void ReleaseThreadAlternateStack();

// Removes the recorder if it is still the current handler; if something was installed over it, it stays but passes
// through.
void RemoveCrashEventHook();

#if ENGINE_PLATFORM_LINUX || ENGINE_PLATFORM_MACOS
// Notes, per fatal signal, the handler the host had installed before the crash library's (call it before
// `sentry_init`): after the report and the dump it is called once, in signal context on the faulting thread (a host's
// crash log keeps working beside the engine's dump). Linux: the recorder calls it. macOS: a relay put on the crash's
// signal after the report, with the vendored Breakpad told not to exit (os/macos/debug/crash_hook.cpp). `SIG_DFL`,
// `SIG_IGN` and the engine's own handler are noted as none. `RemoveCrashEventHook` forgets them.
void NoteHostSignalHandlers();
#endif

#if ENGINE_PLATFORM_MACOS
// macOS (Phase 21 Day 291): the crash library calls its `on_crash` hook with the crashed thread's register context (a
// ucontext_t, ucontext64_t on arm64), on Breakpad's exception thread for a hardware fault or on the crashing thread for
// an abort; crash_handler_sentry.cpp passes it here, which builds the CrashEvent from it
// (os/macos/debug/crash_hook.cpp) and calls the hook InstallCrashEventHook installed. Signal-safe.
void ReportCrashFromMachContext(const void* user_context);
#endif

#if ENGINE_PLATFORM_WINDOWS
// Windows: called before the library is initialized, so the platform's crash gate (crash_hook.cpp) is the filter the
// library replaces.
void PrepareCrashEventHook();

// Windows: called after the library has closed (which puts the gate back on top): when the gate is still the top-level
// filter, the filter that was there before PrepareCrashEventHook is made top-level again, so a host that loaded the
// engine gets its own handling back, and no filter points into an engine library that is unloaded later (Phase 23 Day
// 354).
void FinishCrashEventHookRemoval();

// Windows: the crash library calls its `on_crash` hook with the fault's EXCEPTION_POINTERS (on its own handler thread,
// after the dump); crash_handler_sentry.cpp passes them here, which builds the CrashEvent and calls the hook
// InstallCrashEventHook installed. Signal-safe.
void ReportCrashFromExceptionPointers(const void* exception_pointers);
#endif

}  // namespace vse::debug::detail
