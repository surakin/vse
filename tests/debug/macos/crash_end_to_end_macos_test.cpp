// Crash handling end to end on macOS (Phase 26 Day 307): a child installs the crash handler (sentry's Breakpad backend:
// Mach exception ports) and faults; the parent checks the report on standard error and in the report file, the envelope
// with its minidump, and the thread the report names. The report is written from Breakpad's on_crash hook
// (os/macos/debug/crash_hook.cpp). Also: a death-test child does not outlive a parent killed with SIGKILL (the watch
// pipe), and how long a crashing child takes to end with and without the handler.
#include <catch2/catch_test_macros.hpp>

#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include "build_defs.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"
#include "debug/crash_context.h"
#include "debug/crash_handler.h"
#include "debug/crash_report.h"
#include "debug/death_test.h"
#include "debug/fault_injection.h"
#include "debug/stack_trace.h"
#include "debug/thread_info.h"
#include "envelope.h"
#include "log/assert.h"
#include "log/log.h"
#include "os/console.h"
#include "os/last_error.h"
#include "os/os.h"
#include "os/signal.h"
#include "support/death_test_skip.h"
#include "support/test_support_posix.h"
#include "sync/thread.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;
using debug_test::Cat;
using debug_test::Text;

namespace
{

LogChannel LogEndToEndMac("LogEndToEndMac", LogVerbosity::Info);

class TempDir
{
public:
    TempDir()
    {
        const char* tmp = os::GetEnvironment("TMPDIR");
        FormatTo(path_, sizeof(path_), "%s/engine-e2e-mac-XXXXXX", tmp != nullptr ? tmp : "/tmp");
        if (::mkdtemp(path_) == nullptr)
        {
            path_[0] = '\0';
        }
    }
    ~TempDir()
    {
        if (path_[0] != '\0')
        {
            debug_test::RemoveTree(path_);
        }
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const char* Path() const { return path_; }

private:
    char path_[256];
};

void InstallInto(const char* directory)
{
    CrashHandlerOptions options;
    options.dump_directory = directory;
    InitDebugFacilities();
    InitCrashContext(0, nullptr);
    REQUIRE(InstallCrashHandler(options) == CrashHandlerResult::Installed);
    RegisterThreadForCrashHandling("main-thread");
    Log(LogEndToEndMac, LogVerbosity::Info, "logged-before-the-crash");
}

ChildResult RunCrashChild(const char* name, const char* directory)
{
    ChildOptions options;
    options.timeout_ms = 60000;
    return RunInChild(name, directory, options);
}

void CheckReportAndDump(const ChildResult& r, const char* directory, const char* reason_name)
{
    const StringView err = debug_test::StderrView(r);
    INFO("child stderr: " << Cat(err).CStr());
    CHECK((r.terminated || r.exited));
    CHECK(err.Contains("=== CRASH REPORT ==="));
    CHECK(err.Contains(Cat("reason: ", reason_name).View()));
    CHECK(err.Contains("--- stack trace ---"));
    CHECK(err.Contains("build id: "));
    CHECK(err.Contains("logged-before-the-crash"));
    CHECK(err.Contains("=== END OF REPORT ==="));
    engine_test::TextList envelopes;
    debug_test::FindFiles(directory, ".envelope", &envelopes);
    REQUIRE(envelopes.Size() == 1);
    const Text content = debug_test::ReadWholeFile(envelopes[0]);
    crash_extract::Minidump dump;
    REQUIRE(crash_extract::FindMinidump(content.CStr(), content.Length(), &dump) == crash_extract::EnvelopeResult::Ok);
    CHECK(MemCompare(dump.data, "MDMP", 4) == 0);
    const Text file = debug_test::ReadWholeFile(Cat(directory, "/crash-report.txt").View());
    CHECK(file.View().Contains(Cat("reason: ", reason_name).View()));
}

void WorkerFault(FaultKind kind, bool register_thread)
{
    Thread worker(
        [kind, register_thread]
        {
            if (register_thread)
            {
                RegisterThreadForCrashHandling("worker-7");
            }
            else
            {
                SetCrashThreadName("worker-7");
                ::pthread_setname_np("worker-7");
            }
            InjectFault(kind);
        });
    worker.Join();
}

}  // namespace

ENGINE_DEATH_TEST_FN(dt_mac_null_write)
{
    InstallInto(argument);
    InjectFault(FaultKind::NullWrite);
}
ENGINE_DEATH_TEST_FN(dt_mac_illegal_instruction)
{
    InstallInto(argument);
    InjectFault(FaultKind::IllegalInstruction);
}
ENGINE_DEATH_TEST_FN(dt_mac_divide_by_zero)
{
    InstallInto(argument);
    InjectFault(FaultKind::DivideByZero);
}
ENGINE_DEATH_TEST_FN(dt_mac_abort)
{
    InstallInto(argument);
    InjectFault(FaultKind::Abort);
}
ENGINE_DEATH_TEST_FN(dt_mac_bus_error)
{
    InstallInto(argument);
    InjectFault(FaultKind::BusError);
}
ENGINE_DEATH_TEST_FN(dt_mac_assert)
{
    InstallInto(argument);
    volatile bool never = false;
    ENGINE_ASSERT(never, "end to end assert on macOS");
}
ENGINE_DEATH_TEST_FN(dt_mac_worker_fault)
{
    InstallInto(argument);
    WorkerFault(FaultKind::NullWrite, false);
}
ENGINE_DEATH_TEST_FN(dt_mac_worker_stack_overflow)
{
    InstallInto(argument);
    WorkerFault(FaultKind::StackOverflow, true);
}

TEST_CASE("macOS: a null write is reported from Breakpad's hook, with the faulting thread, and leaves a dump",
          "[crash][crashreport][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_mac_null_write", dir.Path());
    CheckReportAndDump(r, dir.Path(), "AccessViolation at 0x");
    const StringView err = debug_test::StderrView(r);
    CHECK(err.Contains("\"main-thread\""));
    // The first frame is the function that faulted (from the crashed thread's registers), not the handler.
    REQUIRE(err.Contains("#0 "));
    CHECK(debug_test::ValueAfter(err, "#0 ").Contains("InjectFault"));
}

TEST_CASE("macOS: an illegal instruction, a divide by zero, a bus error and an abort each report their reason",
          "[crash][crashreport][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    struct Case
    {
        const char* child;
        const char* reason;
    };
    const Case cases[] = {{"dt_mac_illegal_instruction", "IllegalInstruction"},
                          {"dt_mac_divide_by_zero", "ArithmeticFault"},
                          {"dt_mac_bus_error", "BusError at 0x"},
                          {"dt_mac_abort", "Abort"}};
    for (const Case& c : cases)
    {
        INFO(c.child);
        TempDir dir;
        const ChildResult r = RunCrashChild(c.child, dir.Path());
        CheckReportAndDump(r, dir.Path(), c.reason);
    }
}

TEST_CASE("macOS: a failed assert prints its report and aborts, and the crash report and a dump follow",
          "[crash][crashreport][assert][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_mac_assert", dir.Path());
    CHECK(r.StdoutContains("ASSERT FAILED: "));
    CHECK(r.StdoutContains("end to end assert on macOS"));
    CheckReportAndDump(r, dir.Path(), "Abort");
}

TEST_CASE("macOS: a fault on another thread names that thread", "[crash][crashreport][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_mac_worker_fault", dir.Path());
    CheckReportAndDump(r, dir.Path(), "AccessViolation at 0x");
    const StringView err = debug_test::StderrView(r);
    CHECK(err.Contains("\"worker-7\""));
    CHECK_FALSE(err.Contains("\"main-thread\""));
}

TEST_CASE("macOS: a stack overflow on a registered worker is reported (Mach exceptions need no alternate stack)",
          "[crash][crashreport][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_mac_worker_stack_overflow", dir.Path());
    CheckReportAndDump(r, dir.Path(), "StackOverflow");
    CHECK(debug_test::StderrView(r).Contains("\"worker-7\""));
}

// ---- a fault inside the report
// --------------------------------------------------------------------------------------------------------

namespace
{

void MacReportFaultingProvider(void*, char* out, usize)
{
    volatile int* volatile null_pointer = nullptr;
    *null_pointer = out != nullptr ? 1 : 2;
}

}  // namespace

ENGINE_DEATH_TEST_FN(dt_mac_provider_faults)
{
    InstallInto(argument);
    RegisterCrashStateProvider("faulty", &MacReportFaultingProvider, nullptr);
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("macOS: a state provider that faults inside the report ends the process instead of hanging it",
          "[crash][crashreport][death][debug][macos]")
{
    // The report runs on Breakpad's exception thread; a second fault there would go to the port only that thread serves
    // (Day 307 review). The dump is written before the report, so it is there; the report stops where the provider
    // faulted.
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    ChildOptions options;
    options.timeout_ms = 20000;
    const ChildResult r = RunInChild("dt_mac_provider_faults", dir.Path(), options);
    INFO("child stderr: " << Cat(debug_test::StderrView(r)).CStr());
    CHECK_FALSE(r.timed_out);
    CHECK((r.terminated || r.exited));
    CHECK(debug_test::CountOf(debug_test::StderrView(r), "=== CRASH REPORT ===") == 1);
    engine_test::TextList envelopes;
    debug_test::FindFiles(dir.Path(), ".dmp", &envelopes);
    debug_test::FindFiles(dir.Path(), ".envelope", &envelopes);
    CHECK(envelopes.Size() >= 1);
}

// ---- a child never outlives its parent
// ----------------------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_mac_sleeper)
{
    // Writes its pid where the test finds it, then waits far longer than the test.
    char path[300];
    FormatTo(path, sizeof(path), "%s/pid", argument);
    char text[32];
    const int n = FormatTo(text, sizeof(text), "%ld", static_cast<long>(::getpid()));
    debug_test::WriteWholeFile(StringView(path), text, static_cast<usize>(n));
    os::SleepNanos(30ull * 1000 * 1000 * 1000);
}

ENGINE_DEATH_TEST_FN(dt_mac_parent_of_sleeper)
{
    // Starts the sleeper as its own death-test child, waits until it has written its pid, then dies at once (SIGKILL:
    // no handler, no atexit).
    char path[300];
    FormatTo(path, sizeof(path), "%s/pid", argument);
    Thread runner(
        [argument]
        {
            ChildOptions options;
            options.timeout_ms = 60000;
            RunInChild("dt_mac_sleeper", argument, options);
        });
    runner.Detach();
    for (int i = 0; i < 1000 && debug_test::ReadWholeFile(StringView(path)).Length() == 0; ++i)
    {
        os::SleepNanos(10 * 1000 * 1000);
    }
    ::kill(::getpid(), SIGKILL);
}

TEST_CASE("macOS: a death-test child does not outlive a parent killed with SIGKILL", "[death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    ChildOptions options;
    options.timeout_ms = 30000;
    const ChildResult r = RunInChild("dt_mac_parent_of_sleeper", dir.Path(), options);
    CHECK(r.terminated);
    CHECK(r.native_code == SIGKILL);
    const Text pid_text = debug_test::ReadWholeFile(Cat(dir.Path(), "/pid").View());
    REQUIRE(pid_text.Length() > 0);
    const pid_t sleeper = static_cast<pid_t>(CStrToLong(pid_text.CStr()));
    REQUIRE(sleeper > 0);
    // The sleeper's watch pipe reached end of file when its parent died: it kills itself (well within five seconds).
    bool gone = false;
    for (int i = 0; i < 500 && !gone; ++i)
    {
        gone = ::kill(sleeper, 0) != 0 && os::LastError() == ESRCH;
        if (!gone)
        {
            os::SleepNanos(10 * 1000 * 1000);
        }
    }
    CHECK(gone);
}

// ---- how long a crash takes to end the process
// -------------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_mac_plain_null_write)
{
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("macOS: the time a crashing child takes to end, with and without the crash handler",
          "[crash][death][debug][macos]")
{
    // Recorded, not bounded: the system's crash reporter (ReportCrash) also writes ~/Library/Logs/DiagnosticReports for
    // a process that dies from a signal; the death tests must not wait on it.
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    for (const char* child : {"dt_mac_plain_null_write", "dt_mac_null_write"})
    {
        TempDir dir;
        const u64 start = os::MonotonicTimeNanos();
        const ChildResult r = RunCrashChild(child, dir.Path());
        const u64 took = os::MonotonicTimeNanos() - start;
        os::Print("%s: ended in %.0f ms (%s %lld)\n", child, static_cast<double>(took) / 1e6,
                  r.terminated ? "signal" : "exit code",
                  static_cast<long long>(r.terminated ? r.native_code : r.exit_code));
        CHECK((r.terminated || r.exited));
        CHECK(took < 30ull * 1000 * 1000 * 1000);
    }
}
