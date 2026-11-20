// What the Windows death tests do differently (Phase 13 Day 155; os/windows/debug/BACKEND.md, RunInChild): abort() is
// told from exit(3) by a marker line, a child never shows an error dialog or files a Windows Error Reporting report,
// and a child that hangs is killed at the deadline together with any process it started (the job object).
#include <catch2/catch_test_macros.hpp>

#include "debug/death_test.h"
#include "log/output.h"
#include "resource_guard.h"
#include "support/death_test_skip.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "os/console.h"

#include "os/windows/win32.h"

using namespace vse;
using namespace vse::debug;

// ---- the abort marker
// -----------------------------------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_win_exit_three)
{
    out::Flush();
    os::ExitNow(3);  // the same exit code abort() gives, without the marker
}

ENGINE_DEATH_TEST_FN(dt_win_abort)
{
    os::Abort();
}

TEST_CASE("Windows: abort and exit(3) end with the same code, and the marker line tells them apart",
          "[death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult exited = RunInChild("dt_win_exit_three");
    CHECK(exited.exited);
    CHECK(exited.exit_code == 3);
    CHECK(exited.reason == CrashReason::None);
    CHECK_FALSE(exited.StderrContains("ENGINE-ABORT"));

    const ChildResult aborted = RunInChild("dt_win_abort");
    CHECK(aborted.terminated);
    CHECK(aborted.reason == CrashReason::Abort);
    CHECK(aborted.native_code == 3);
    CHECK(aborted.StderrContains("ENGINE-ABORT"));
}

// ---- no dialogs, no Windows Error Reporting
// -----------------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_win_report_error_mode)
{
    const UINT mode = GetErrorMode();
    const unsigned int abort_flags = _set_abort_behavior(0, 0);  // reads the flags without changing them
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    const BOOL in_job = QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits),
                                                  nullptr);
    out::Print(
        "gpfault-box-off=%d critical-box-off=%d abort-msg=%d abort-report=%d job=%d kill-on-close=%d die-on-unhandled=%d\n",
        (mode & SEM_NOGPFAULTERRORBOX) != 0 ? 1 : 0, (mode & SEM_FAILCRITICALERRORS) != 0 ? 1 : 0,
        (abort_flags & _WRITE_ABORT_MSG) != 0 ? 1 : 0, (abort_flags & _CALL_REPORTFAULT) != 0 ? 1 : 0, in_job ? 1 : 0,
        (limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0 ? 1 : 0,
        (limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION) != 0 ? 1 : 0);
    out::Flush();
}

TEST_CASE("Windows: a death-test child shows no error dialog, writes no abort message and runs in a job that ends it",
          "[death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_win_report_error_mode");
    REQUIRE(r.exited);
    REQUIRE(r.stdout_length < sizeof(r.stdout_text));
    INFO(r.stdout_text);
    CHECK(r.StdoutContains(
        "gpfault-box-off=1 critical-box-off=1 abort-msg=0 abort-report=0 job=1 kill-on-close=1 die-on-unhandled=1"));
}

// ---- a child that hangs, and the process it started
// -----------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_win_sleep_forever)
{
    Sleep(INFINITE);
}

// Starts a second copy of the test program that sleeps forever (no job of its own, no handles shared), prints its
// process id, then hangs.
ENGINE_DEATH_TEST_FN(dt_win_hang_with_grandchild)
{
    SetEnvironmentVariableW(L"ENGINE_DEATH_TEST_CHILD", L"dt_win_sleep_forever");
    wchar_t exe[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(sizeof(exe) / sizeof(exe[0])));
    if (length == 0 || length >= sizeof(exe) / sizeof(exe[0]))
    {
        os::ExitNow(10);
    }
    wchar_t command[MAX_PATH * 4 + 64];
    WFormatTo(command, sizeof(command) / sizeof(command[0]), L"\"%ls\" [.death-child] --colour-mode none", exe);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process))
    {
        os::ExitNow(11);
    }
    out::Print("grandchild=%lu\n", static_cast<unsigned long>(process.dwProcessId));
    out::Flush();
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Sleep(INFINITE);
}

TEST_CASE("Windows: a child that hangs is killed at the deadline, and so is every process it started",
          "[death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildOptions options;
    options.timeout_ms = 3000;
    const ChildResult r = RunInChild("dt_win_hang_with_grandchild", "", options);
    CHECK(r.timed_out);
    CHECK(r.terminated);
    CHECK(r.reason == CrashReason::None);
    REQUIRE(r.stdout_length < sizeof(r.stdout_text));  // so the zeroed rest of the buffer ends the text
    const char* line = CStrFind(r.stdout_text, "grandchild=");
    REQUIRE(line != nullptr);
    const DWORD pid = static_cast<DWORD>(CStrToULong(line + CStrLength("grandchild="), nullptr, 10));
    REQUIRE(pid != 0);
    // The job ended the grandchild with the child: it is gone, or exits within moments (process ids can be reused, so
    // only a process that still runs after the wait is a failure, and that one would be ours to clean up).
    const HANDLE grandchild = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                          pid);
    if (grandchild != nullptr)
    {
        const DWORD waited = WaitForSingleObject(grandchild, 5000);
        CHECK(waited == WAIT_OBJECT_0);
        if (waited != WAIT_OBJECT_0)
        {
            TerminateProcess(grandchild, 1);
        }
        CloseHandle(grandchild);
    }
}

// ---- the resource guard
// -----------------------------------------------------------------------------------------------------------

// The guard tells a child from an unrelated process that names this one as its parent (a reused process id, Day 157).
// This checks the other side: a child this process started, still running, with no handle of ours left open, is
// reported until it ends.
TEST_CASE("Windows: the resource guard reports a running child until it ends", "[death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    wchar_t exe[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(sizeof(exe) / sizeof(exe[0])));
    REQUIRE(length != 0);
    REQUIRE(length < sizeof(exe) / sizeof(exe[0]));
    wchar_t command[MAX_PATH * 4 + 64];
    WFormatTo(command, sizeof(command) / sizeof(command[0]), L"\"%ls\" --list-tests", exe);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // Suspended, so it is running (not exited) for as long as the test needs and runs none of its code.
    REQUIRE(CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                           nullptr, &startup, &process));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CHECK_FALSE(guard.Clean());

    const HANDLE child = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, process.dwProcessId);
    REQUIRE(child != nullptr);
    TerminateProcess(child, 1);
    CHECK(WaitForSingleObject(child, 5000) == WAIT_OBJECT_0);
    CloseHandle(child);
    CHECK(guard.Clean());
}
