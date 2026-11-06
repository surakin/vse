#pragma once

#include "debug/crash_reason.h"
#include "debug/stack_trace.h"
#include "types.h"

// The per-platform half of crash reporting (crash_handler_sentry.cpp is the neutral half). Not part of the public API.
//
// The crash library cannot tell us what happened: on Linux its Breakpad backend passes no signal information to the `on_crash`
// hook, and runs the hook in a helper thread rather than the thread that faulted. So the platform installs a small recorder
// in front of the library's own handler. It runs on the faulting thread, builds a CrashEvent, hands it to the neutral half to
// write the report, and then calls the library's handler, which writes the dump. The recorder never replaces the library.
namespace engine_placeholder::debug::detail {

// What the platform learned about a fault, on the faulting thread.
struct CrashEvent {
    CrashReason reason = CrashReason::None;
    bool has_fault_address = false;
    u64 fault_address = 0;
    u64 thread_id = 0;
    StackTrace trace; // from the fault, not from the recorder
};

// Called on the faulting thread, inside the recorder, at most once per crash; must be signal-safe.
using CrashEventFn = void (*)(const CrashEvent& event);

// Installs the recorder for the fatal signals, in front of the handlers already installed, which it calls afterwards.
// Call after the library has installed its own. Returns false if it could not.
bool InstallCrashEventHook(CrashEventFn fn);

// True while a fault is being reported (or the library's handler is running after the report). Uninstalling waits for it to end.
bool CrashReportInProgress();

// Removes the recorder if it is still the current handler; if something was installed over it, it stays but passes through.
void RemoveCrashEventHook();

} // namespace engine_placeholder::debug::detail
