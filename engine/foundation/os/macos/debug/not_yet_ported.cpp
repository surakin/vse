// TEMPORARY (Phase 26 Day 306, removed on Days 307-308; KNOWN-ISSUES.md): what the macOS debugging backend does not have yet, answered as
// "not supported" so the layer links and its stack-trace and crash-context tests run. Day 307 replaces the crash handler (sentry with the
// Breakpad backend: crash_handler_sentry.cpp, whose macOS private headers are not vendored yet), the crash event hook, thread registration,
// the death tests, fault injection and the debugger check; Day 308 the post-mortem dumps. Nothing here pretends to work: each answer is the
// documented "unsupported" one, so a caller and a test take their unsupported path (death tests skip with DeathTestsSupported() false).
#include "debug/crash_event_platform.h"
#include "debug/crash_handler.h"
#include "debug/death_test.h"
#include "debug/death_test_platform.h"
#include "debug/debugger.h"
#include "debug/fault_injection.h"
#include "debug/post_mortem.h"
#include "debug/thread_info.h"
#include "os/console.h"
#include "os/os.h"

namespace engine_placeholder::debug {

// Day 307: crash_handler_sentry.cpp.
CrashHandlerResult InstallCrashHandler(const CrashHandlerOptions& options) {
    return options.enabled ? CrashHandlerResult::Unsupported : CrashHandlerResult::Disabled;
}
void UninstallCrashHandler() {}
bool CrashHandlerInstalled() { return false; }

namespace detail {

// Day 307: the crash event hook (Breakpad's on_crash on macOS).
bool InstallCrashEventHook(CrashEventFn) { return false; }
bool CrashReportInProgress() { return false; }
void RemoveCrashEventHook() {}

// Day 307: death tests through posix_spawn (os/posix/debug/).
bool PlatformCanSpawn() { return false; }
bool PlatformChildRequest(const char**, const char**) { return false; }
void PlatformPrepareChild() {}
ChildResult PlatformRunChild(const char*, const char*, const ChildOptions&) { return ChildResult{}; } // supported = false

} // namespace detail

// Day 307 registers with the crash handler. Until then there is none, so the contract's answer for "not installed yet": the name is set, and false.
bool RegisterThreadForCrashHandling(const char* name) {
    if (name != nullptr) SetCrashThreadName(name);
    return false;
}

// Day 307.
[[noreturn]] void ExitChildProcess(int code) { os::ExitNow(code); }

// Day 307: sysctl(KERN_PROC) and P_TRACED.
bool IsDebuggerAttached() { return false; }

// Day 307: as Linux on x86-64.
bool FaultSupported(FaultKind) { return false; }
[[noreturn]] void InjectFault(FaultKind) { os::Abort(); }

// Day 308: setrlimit(RLIMIT_CORE) and the /cores rules.
PostMortemReport EnablePostMortemDumps(const PostMortemOptions&) { return PostMortemReport{}; } // result Unsupported

} // namespace engine_placeholder::debug
