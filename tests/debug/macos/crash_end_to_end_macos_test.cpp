// Crash handling end to end on macOS (Phase 21 Day 291): a child installs the crash handler (sentry's Breakpad backend:
// Mach exception ports) and faults; the parent checks the report on standard error and in the report file, the envelope
// with its minidump, and the thread the report names. The report is written from Breakpad's on_crash hook
// (os/macos/debug/crash_hook.cpp). Also: a death-test child does not outlive a parent killed with SIGKILL (the watch
// pipe), and how long a crashing child takes to end with and without the handler.
#include <catch2/catch_test_macros.hpp>

#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include "build_defs.h"
#include "log/output.h"
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

using namespace vse;
using namespace vse::debug;
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
    // (Day 291 review). The dump is written before the report, so it is there; the report stops where the provider
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

// ---- a host's handler installed before the crash handler (crash handler chaining, Day 3)
// ----------------------------------------------------------------------------------------------

namespace
{

// A host's own crash handler, installed before the crash handler, as a host with its own crash log installs it: a
// marker, the default action back, the signal raised again.
void MacHostSegvHandler(int signal_number)
{
    static const char kText[] = "HOST-SEGV-HANDLER-RAN\n";
    os::Write(2, kText, sizeof(kText) - 1);
    os::SetSignalHandler(signal_number, SIG_DFL);
    ::raise(signal_number);
}

void MacHostAbrtHandler(int signal_number)
{
    static const char kText[] = "HOST-ABRT-HANDLER-RAN\n";
    os::Write(2, kText, sizeof(kText) - 1);
    os::SetSignalHandler(signal_number, SIG_DFL);
    ::raise(signal_number);
}

void MacInstallHostHandler(int signal_number, void (*fn)(int))
{
    struct sigaction action;
    MemSet(&action, 0, sizeof(action));
    action.sa_handler = fn;
    sigemptyset(&action.sa_mask);
    ::sigaction(signal_number, &action, nullptr);
}

// The address a test faults at: not null, and inside __PAGEZERO, so the kernel delivers SIGSEGV with it as si_addr.
constexpr u64 kMacFaultAddress = 0x1238;

void MacHostInfoHandler(int, siginfo_t* info, void*)
{
    if (info != nullptr && reinterpret_cast<u64>(info->si_addr) == kMacFaultAddress)
    {
        static const char kText[] = "HOST-SIGINFO-ADDRESS-OK\n";
        os::Write(2, kText, sizeof(kText) - 1);
    }
    else
    {
        static const char kText[] = "HOST-SIGINFO-ADDRESS-WRONG\n";
        os::Write(2, kText, sizeof(kText) - 1);
    }
    // Returns: the fault runs again under the default action and ends the process.
}

void MacHostFaultingHandler(int)
{
    static const char kText[] = "HOST-HANDLER-ENTERED\n";
    os::Write(2, kText, sizeof(kText) - 1);
    *reinterpret_cast<volatile int*>(kMacFaultAddress) = 1;  // the host's handler itself faults
}

// The report, then the host's handler once, the envelope written before it, and the original signal ends the child.
void CheckHostRanAfterDump(const ChildResult& r, const char* directory, const char* marker, int signal_number)
{
    const StringView err = debug_test::StderrView(r);
    INFO("child stderr: " << Cat(err).CStr());
    CHECK(err.Contains("=== CRASH REPORT ==="));
    CHECK(err.Find("=== END OF REPORT ===") < err.Find(StringView(marker)));
    CHECK(debug_test::CountOf(err, marker) == 1);
    CHECK(r.terminated);
    CHECK(r.native_code == signal_number);
    engine_test::TextList envelopes;
    debug_test::FindFiles(directory, ".envelope", &envelopes);
    REQUIRE(envelopes.Size() == 1);
    const Text content = debug_test::ReadWholeFile(envelopes[0]);
    crash_extract::Minidump dump;
    CHECK(crash_extract::FindMinidump(content.CStr(), content.Length(), &dump) == crash_extract::EnvelopeResult::Ok);
}

}  // namespace

ENGINE_DEATH_TEST_FN(dt_mac_host_before)
{
    MacInstallHostHandler(SIGSEGV, &MacHostSegvHandler);  // installed before the crash handler
    InstallInto(argument);
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("macOS: a handler installed before the crash handler runs once, after the report and the dump",
          "[crash][crashreport][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_mac_host_before", dir.Path());
    CheckHostRanAfterDump(r, dir.Path(), "HOST-SEGV-HANDLER-RAN", SIGSEGV);
}

ENGINE_DEATH_TEST_FN(dt_mac_host_two_signals_segv)
{
    MacInstallHostHandler(SIGSEGV, &MacHostSegvHandler);
    MacInstallHostHandler(SIGABRT, &MacHostAbrtHandler);
    InstallInto(argument);
    InjectFault(FaultKind::NullWrite);
}

ENGINE_DEATH_TEST_FN(dt_mac_host_two_signals_abrt)
{
    MacInstallHostHandler(SIGSEGV, &MacHostSegvHandler);
    MacInstallHostHandler(SIGABRT, &MacHostAbrtHandler);
    InstallInto(argument);
    InjectFault(FaultKind::Abort);
}

TEST_CASE("macOS: earlier handlers on two signals: each runs for its own signal only",
          "[crash][crashreport][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    {
        TempDir dir;
        const ChildResult r = RunCrashChild("dt_mac_host_two_signals_segv", dir.Path());
        CheckHostRanAfterDump(r, dir.Path(), "HOST-SEGV-HANDLER-RAN", SIGSEGV);
        CHECK_FALSE(debug_test::StderrView(r).Contains("HOST-ABRT-HANDLER-RAN"));
    }
    {
        TempDir dir;
        const ChildResult r = RunCrashChild("dt_mac_host_two_signals_abrt", dir.Path());
        CheckHostRanAfterDump(r, dir.Path(), "HOST-ABRT-HANDLER-RAN", SIGABRT);
        CHECK_FALSE(debug_test::StderrView(r).Contains("HOST-SEGV-HANDLER-RAN"));
    }
}

ENGINE_DEATH_TEST_FN(dt_mac_host_siginfo)
{
    struct sigaction action;
    MemSet(&action, 0, sizeof(action));
    action.sa_sigaction = &MacHostInfoHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    ::sigaction(SIGSEGV, &action, nullptr);
    InstallInto(argument);
    *reinterpret_cast<volatile int*>(kMacFaultAddress) = 1;
}

TEST_CASE("macOS: an earlier SA_SIGINFO handler gets the fault's siginfo, with its address, on the faulting thread",
          "[crash][crashreport][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_mac_host_siginfo", dir.Path());
    CheckHostRanAfterDump(r, dir.Path(), "HOST-SIGINFO-ADDRESS-OK", SIGSEGV);
    CHECK_FALSE(debug_test::StderrView(r).Contains("HOST-SIGINFO-ADDRESS-WRONG"));
}

ENGINE_DEATH_TEST_FN(dt_mac_host_handler_faults)
{
    MacInstallHostHandler(SIGSEGV, &MacHostFaultingHandler);
    InstallInto(argument);
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("macOS: an earlier handler that faults itself ends the process with the original signal, once",
          "[crash][crashreport][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_mac_host_handler_faults", dir.Path());
    CheckHostRanAfterDump(r, dir.Path(), "HOST-HANDLER-ENTERED", SIGSEGV);
}

namespace
{

void MacHostAbrtFaultingHandler(int)
{
    static const char kText[] = "HOST-ABRT-HANDLER-ENTERED\n";
    os::Write(2, kText, sizeof(kText) - 1);
    *reinterpret_cast<volatile int*>(kMacFaultAddress) = 1;  // the host's SIGABRT handler itself faults
}

}  // namespace

ENGINE_DEATH_TEST_FN(dt_mac_host_abrt_handler_faults)
{
    os::SetSignalHandler(SIGSEGV, SIG_DFL);  // no Catch2 handler on the second fault's signal
    os::SetSignalHandler(SIGBUS, SIG_DFL);
    MacInstallHostHandler(SIGABRT, &MacHostAbrtFaultingHandler);
    InstallInto(argument);
    InjectFault(FaultKind::Abort);
}

TEST_CASE("macOS: an earlier SIGABRT handler that faults ends the process without a second dump",
          "[crash][crashreport][death][debug][macos]")
{
    // On the abort path the crash library is still installed when the host's handler runs; a fault there must not
    // reach it again (a second report and envelope), it ends the process (Day 3 review).
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_mac_host_abrt_handler_faults", dir.Path());
    const StringView err = debug_test::StderrView(r);
    INFO("child stderr: " << Cat(err).CStr());
    CHECK(err.Find("=== END OF REPORT ===") < err.Find(StringView("HOST-ABRT-HANDLER-ENTERED")));
    CHECK(debug_test::CountOf(err, "HOST-ABRT-HANDLER-ENTERED") == 1);
    CHECK(debug_test::CountOf(err, "=== CRASH REPORT ===") == 1);
    CHECK(r.terminated);
    engine_test::TextList envelopes;
    debug_test::FindFiles(dir.Path(), ".envelope", &envelopes);
    CHECK(envelopes.Size() == 1);
}

ENGINE_DEATH_TEST_FN(dt_mac_host_reinstall)
{
    MacInstallHostHandler(SIGSEGV, &MacHostSegvHandler);
    InstallInto(argument);
    UninstallCrashHandler();
    InstallInto(argument);
    CrashHandlerOptions again;
    again.dump_directory = argument;
    if (InstallCrashHandler(again) != CrashHandlerResult::AlreadyInstalled)
    {
        static const char kText[] = "SECOND-INSTALL-NOT-REFUSED\n";
        os::Write(2, kText, sizeof(kText) - 1);
    }
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("macOS: install, uninstall, install: the earlier handler runs once, and a second install does not chain to "
          "itself",
          "[crash][crashreport][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_mac_host_reinstall", dir.Path());
    CheckHostRanAfterDump(r, dir.Path(), "HOST-SEGV-HANDLER-RAN", SIGSEGV);
    CHECK(debug_test::CountOf(debug_test::StderrView(r), "=== CRASH REPORT ===") == 1);
    CHECK_FALSE(debug_test::StderrView(r).Contains("SECOND-INSTALL-NOT-REFUSED"));
}

ENGINE_DEATH_TEST_FN(dt_mac_host_abrt_only_then_fault)
{
    // The child runs inside a Catch2 test case, whose fatal-condition handlers are on SIGSEGV and the others: those are
    // earlier handlers too (and are chained to in the other crash tests). Here the fault's signals have none.
    os::SetSignalHandler(SIGSEGV, SIG_DFL);
    os::SetSignalHandler(SIGBUS, SIG_DFL);
    MacInstallHostHandler(SIGABRT, &MacHostAbrtHandler);
    InstallInto(argument);
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("macOS: with no earlier handler for the crash's signal, the process ends as before (no chaining)",
          "[crash][crashreport][death][debug][macos]")
{
    // Only SIGABRT has a host handler; a null write is a SIGSEGV, so Breakpad exits after the dump as it always has.
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_mac_host_abrt_only_then_fault", dir.Path());
    CheckReportAndDump(r, dir.Path(), "AccessViolation at 0x");
    INFO("child stderr: " << Cat(debug_test::StderrView(r)).CStr());
    CHECK_FALSE(debug_test::StderrView(r).Contains("HOST-ABRT-HANDLER-RAN"));
    CHECK(r.exited);          // Breakpad's _exit after the dump, not a signal
    CHECK(r.exit_code == 1);  // EXC_BAD_ACCESS
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
        out::Print("%s: ended in %.0f ms (%s %lld)\n", child, static_cast<double>(took) / 1e6,
                   r.terminated ? "signal" : "exit code",
                   static_cast<long long>(r.terminated ? r.native_code : r.exit_code));
        CHECK((r.terminated || r.exited));
        CHECK(took < 30ull * 1000 * 1000 * 1000);
    }
}
