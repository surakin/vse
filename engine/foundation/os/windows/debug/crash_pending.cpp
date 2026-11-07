// The parts of the Windows debugging backend that Days 155 and 156 build (plans/phase-13-windows-dx12/), here so foundation_debug links
// and every test that needs them skips with a reason until then (KNOWN-ISSUES.md lists them):
//   Day 155: the crash event hook (the report from sentry-native's on_crash), death tests (CreateProcessW and a job object), fault injection;
//   Day 156: post-mortem dumps (Windows Error Reporting LocalDumps).
// RegisterThreadForCrashHandling is real today (the name and the stack guarantee).
#include <cstdlib>

#include "debug/crash_event_platform.h"
#include "debug/crash_handler.h"
#include "debug/death_test.h"
#include "debug/death_test_platform.h"
#include "debug/fault_injection.h"
#include "debug/post_mortem.h"
#include "debug/thread_info.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

namespace engine_placeholder::debug {

// The stack a crashed thread keeps for the report after a stack overflow. 64 KiB is the starting value; Day 155 measures what the report
// written from on_crash needs and sets it (os/windows/debug/BACKEND.md, RegisterThreadForCrashHandling).
constexpr ULONG kStackGuaranteeBytes = 64 * 1024;

bool RegisterThreadForCrashHandling(const char* name) {
    if (name != nullptr) {
        SetCrashThreadName(name);
        wchar_t wide[64];
        os::windows::Utf8ToUtf16(StringView(name), wide, sizeof(wide) / sizeof(wide[0]));
        SetThreadDescription(GetCurrentThread(), wide);
    }
    if (!CrashHandlerInstalled()) return false; // nothing handles faults yet (only the name, above)
    ULONG guarantee = kStackGuaranteeBytes;
    return SetThreadStackGuarantee(&guarantee) != FALSE;
}

namespace detail {

bool InstallCrashEventHook(CrashEventFn) { return false; } // Day 155: the on_crash hook
bool CrashReportInProgress() { return false; }
void RemoveCrashEventHook() {}

bool PlatformCanSpawn() { return false; } // Day 155: death tests
ChildResult PlatformRunChild(const char*, const char*, const ChildOptions&) { return ChildResult(); }
bool PlatformChildRequest(const char**, const char**) { return false; }
void PlatformPrepareChild() {}

} // namespace detail

bool FaultSupported(FaultKind) { return false; } // Day 155

void InjectFault(FaultKind) { std::abort(); } // never called: FaultSupported is false (tests check it first)

PostMortemReport EnablePostMortemDumps(const PostMortemOptions&) { // Day 156
    PostMortemReport report;
    report.result = PostMortemResult::Unsupported;
    const char note[] = "Windows Error Reporting LocalDumps are configured from Phase 13 Day 156";
    for (usize i = 0; i < sizeof(note) && i < sizeof(report.note); ++i) report.note[i] = note[i];
    return report;
}

void ExitChildProcess(int code) { std::_Exit(code); } // ends at once, as _exit does on Linux (no atexit handlers, no flushing)

} // namespace engine_placeholder::debug
