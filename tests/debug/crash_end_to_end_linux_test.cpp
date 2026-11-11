// Crash handling end to end on Linux: a child process installs the crash handler and faults in various ways; the parent checks
// the report on standard error, the report file, the envelope with its minidump, and the way the process died.
#include <catch2/catch_test_macros.hpp>

#include "debug/crash_context.h"
#include "debug/crash_handler.h"
#include "debug/crash_report.h"
#include "debug/death_test.h"
#include "debug/fault_injection.h"
#include "debug/thread_info.h"
#include "envelope.h"
#include "log/log.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include "sync/atomic.h"
#include "os/console.h"
#include "os/signal.h"
#include <fcntl.h>
#include <sys/stat.h>
#include "sync/thread.h"
#include <unistd.h>
#include "build_defs.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"
#include "support/test_support_posix.h"

#if ENGINE_SANITIZER_THREAD
#define E2E_UNDER_TSAN 1
#endif

using namespace engine_placeholder;
using namespace engine_placeholder::debug;
using debug_test::Cat;
using debug_test::Text;

namespace {

LogChannel LogEndToEnd("LogEndToEnd", LogVerbosity::Info);

class TempDir {
public:
    TempDir() {
        const char* tmp = os::GetEnvironment("TMPDIR");
        FormatTo(path_, sizeof(path_), "%s/engine-e2e-XXXXXX", tmp != nullptr ? tmp : "/tmp");
        if (mkdtemp(path_) == nullptr) {
            path_[0] = '\0';
        }
    }
    ~TempDir() {
        if (path_[0] != '\0') {
            debug_test::RemoveTree(path_);
        }
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const char* Path() const { return path_; }

private:
    char path_[256];
};

void InstallInto(const char* directory) {
    CrashHandlerOptions options;
    options.dump_directory = directory;
    InitDebugFacilities();
    InitCrashContext(0, nullptr);
    InstallCrashHandler(options);
    SetCrashThreadName("main-thread");
    Log(LogEndToEnd, LogVerbosity::Info, "logged-before-the-crash");
}

ChildResult RunCrashChild(const char* name, const char* directory) {
    ChildOptions options;
    options.timeout_ms = 60000;
    return RunInChild(name, directory, options);
}

// The checks every crash shares: the report on standard error, one envelope with a minidump, the report file beside it.
void CheckReportAndDump(const ChildResult& r, const char* directory, const char* reason_name) {
    const StringView err = debug_test::StderrView(r);
    INFO("child stderr: " << Cat(err).CStr());
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
    // The report travels in the envelope too (attached), and sits beside the dump.
    CHECK(content.View().Contains("=== CRASH REPORT ==="));
    const Text file = debug_test::ReadWholeFile(Cat(directory, "/crash-report.txt").View());
    CHECK(file.View().Contains("=== CRASH REPORT ==="));
    CHECK(file.View().Contains(Cat("reason: ", reason_name).View()));
}

} // namespace

// ---- one child per kind of fault -----------------------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_e2e_null_write) {
    InstallInto(argument);
    InjectFault(FaultKind::NullWrite);
}
ENGINE_DEATH_TEST_FN(dt_e2e_illegal_instruction) {
    InstallInto(argument);
    InjectFault(FaultKind::IllegalInstruction);
}
ENGINE_DEATH_TEST_FN(dt_e2e_divide_by_zero) {
    InstallInto(argument);
    InjectFault(FaultKind::DivideByZero);
}
ENGINE_DEATH_TEST_FN(dt_e2e_abort) {
    InstallInto(argument);
    InjectFault(FaultKind::Abort);
}
ENGINE_DEATH_TEST_FN(dt_e2e_stack_overflow) {
    InstallInto(argument);
    InjectFault(FaultKind::StackOverflow);
}

TEST_CASE("A null write prints the report, leaves a dump and the process dies from SIGSEGV", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_null_write", dir.Path());
    CHECK(r.reason == CrashReason::AccessViolation);
    CHECK(r.native_code == SIGSEGV);
    CheckReportAndDump(r, dir.Path(), "AccessViolation at 0x");
    const StringView err = debug_test::StderrView(r);
    CHECK(err.Contains("\"main-thread\""));
    // The first frame is the function that faulted, not the recorder.
    REQUIRE(err.Contains("#0 "));
    CHECK_FALSE(debug_test::ValueAfter(err, "#0 ").Contains("Recorder"));
}

TEST_CASE("An illegal instruction, a divide by zero and an abort each report their reason", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    {
        TempDir dir;
        const ChildResult r = RunCrashChild("dt_e2e_illegal_instruction", dir.Path());
        CHECK(r.reason == CrashReason::IllegalInstruction);
        CheckReportAndDump(r, dir.Path(), "IllegalInstruction");
    }
    if (FaultSupported(FaultKind::DivideByZero)) {
        TempDir dir;
        const ChildResult r = RunCrashChild("dt_e2e_divide_by_zero", dir.Path());
        CHECK(r.reason == CrashReason::ArithmeticFault);
        CheckReportAndDump(r, dir.Path(), "ArithmeticFault");
    }
    {
        TempDir dir;
        const ChildResult r = RunCrashChild("dt_e2e_abort", dir.Path());
        CHECK(r.reason == CrashReason::Abort);
        CHECK(r.native_code == SIGABRT);
        CheckReportAndDump(r, dir.Path(), "Abort");
    }
}

TEST_CASE("A stack overflow on the main thread prints a report and leaves a dump", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_stack_overflow", dir.Path());
    CHECK(r.terminated);
    CheckReportAndDump(r, dir.Path(), "StackOverflow");
}

// ---- other threads ------------------------------------------------------------------------------------------------------------

namespace {

void WorkerFault(FaultKind kind, bool register_thread) {
    Thread worker([kind, register_thread] {
        if (register_thread) {
            RegisterThreadForCrashHandling();
        }
        SetCrashThreadName("worker-7");
        InjectFault(kind);
    });
    worker.Join();
}

} // namespace

ENGINE_DEATH_TEST_FN(dt_e2e_worker_fault) {
    InstallInto(argument);
    WorkerFault(FaultKind::NullWrite, false);
}
ENGINE_DEATH_TEST_FN(dt_e2e_worker_stack_overflow) {
    InstallInto(argument);
    WorkerFault(FaultKind::StackOverflow, true);
}

TEST_CASE("A fault on another thread names that thread and the dump holds every thread", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_worker_fault", dir.Path());
    CHECK(r.reason == CrashReason::AccessViolation);
    CheckReportAndDump(r, dir.Path(), "AccessViolation at 0x");
    const StringView err = debug_test::StderrView(r);
    CHECK(err.Contains("\"worker-7\""));
    CHECK_FALSE(err.Contains("\"main-thread\"")); // the main thread did not fault
}

TEST_CASE("A stack overflow on a registered worker thread still produces a report and a dump", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_worker_stack_overflow", dir.Path());
    CHECK(r.terminated);
    CheckReportAndDump(r, dir.Path(), "StackOverflow");
    CHECK(debug_test::StderrView(r).Contains("\"worker-7\""));
}

ENGINE_DEATH_TEST_FN(dt_e2e_two_threads_fault) {
    InstallInto(argument);
    Atomic<int> ready{0};
    auto body = [&ready] {
        ready.FetchAdd(1);
        while (ready.Load() < 2) {
            ThisThread::Yield();
        }
        InjectFault(FaultKind::NullWrite);
    };
    Thread a(body);
    Thread b(body);
    a.Join();
    b.Join();
}

TEST_CASE("Two threads faulting at once produce one report and one dump", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_two_threads_fault", dir.Path());
    CHECK(r.terminated);
    CheckReportAndDump(r, dir.Path(), "AccessViolation");
    CHECK(debug_test::CountOf(debug_test::StderrView(r), "=== CRASH REPORT ===") == 1);
}

namespace {
Atomic<bool> g_slow_provider_started{false};
void SlowProvider(void*, char* out, usize capacity) {
    g_slow_provider_started = true;
    os::SleepNanos(400ull * 1000 * 1000); // long enough for the other thread to fault in the middle of the report
    FormatTo(out, capacity, "slow but done");
}
} // namespace

ENGINE_DEATH_TEST_FN(dt_e2e_second_fault_during_report) {
    InstallInto(argument);
    RegisterCrashStateProvider("slow", &SlowProvider, nullptr);
    Thread second([] {
        while (!g_slow_provider_started.Load()) {
            ThisThread::Yield();
        }
        os::SleepNanos(50ull * 1000 * 1000);
        InjectFault(FaultKind::NullWrite);
    });
    InjectFault(FaultKind::NullWrite); // the main thread reports first, slowly
    second.Join();
}

TEST_CASE("A second thread faulting while the first is writing the report does not cut the report short", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_second_fault_during_report", dir.Path());
    CheckReportAndDump(r, dir.Path(), "AccessViolation");
    const StringView err = debug_test::StderrView(r);
    CHECK(err.Contains("slow: slow but done"));
    CHECK(debug_test::CountOf(err, "=== CRASH REPORT ===") == 1);
}

// ---- when things go wrong in the report ------------------------------------------------------------------------------------------

namespace {
void E2eFaultingProvider(void*, char*, usize) {
    InjectFault(FaultKind::NullWrite);
}
} // namespace

ENGINE_DEATH_TEST_FN(dt_e2e_provider_faults) {
    InstallInto(argument);
    RegisterCrashStateProvider("bad", &E2eFaultingProvider, nullptr);
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("A state provider that faults prints double fault, and the dump is still written", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_provider_faults", dir.Path());
    const StringView err = debug_test::StderrView(r);
    INFO("child stderr: " << Cat(err).CStr());
    CHECK(r.terminated);
    CHECK(r.native_code == SIGSEGV);
    CHECK(debug_test::CountOf(err, "=== CRASH REPORT ===") == 1);
#if !defined(E2E_UNDER_TSAN)
    // (Under ThreadSanitizer a fault inside a signal handler kills the process before our recorder sees it again; see KNOWN-ISSUES.)
    CHECK(err.Contains("double fault"));
    engine_test::TextList envelopes;
    debug_test::FindFiles(dir.Path(), ".envelope", &envelopes);
    CHECK(envelopes.Size() == 1);
#endif
}

ENGINE_DEATH_TEST_FN(dt_e2e_assert_fails) {
    InstallInto(argument);
    ENGINE_ASSERT(false, "end to end assert");
}

TEST_CASE("A failed assert prints its report, aborts, and the crash report and a dump follow", "[crash][crashreport][assert][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_assert_fails", dir.Path());
    CHECK(r.reason == CrashReason::Abort);
    CHECK(r.native_code == SIGABRT);
    CHECK(r.StdoutContains("ASSERT FAILED: "));
    CHECK(r.StdoutContains("end to end assert"));
    CHECK(r.StdoutContains("--- build ---"));
    CHECK(r.StderrContains("=== CRASH REPORT ==="));
    CHECK(r.StderrContains("reason: Abort"));
    engine_test::TextList envelopes;
    debug_test::FindFiles(dir.Path(), ".envelope", &envelopes);
    CHECK(envelopes.Size() == 1);
    CHECK(debug_test::ReadWholeFile(Cat(dir.Path(), "/crash-report.txt").View()).View().Contains("=== CRASH REPORT ==="));
}

// ---- handlers of the application and uninstalling ---------------------------------------------------------------------------------

namespace {

void E2eAppHandler(int, siginfo_t*, void*) {
    static const char kText[] = "APP-HANDLER-RAN\n";
    os::Write(2, kText, sizeof(kText) - 1);
    _exit(42);
}

struct sigaction g_after_previous;

void AppHandlerThatChains(int signal_number, siginfo_t* info, void* context) {
    static const char kText[] = "APP-HANDLER-RAN\n";
    os::Write(2, kText, sizeof(kText) - 1);
    if (g_after_previous.sa_sigaction != nullptr) {
        g_after_previous.sa_sigaction(signal_number, info, context);
    }
}

void InstallAppHandler(void (*fn)(int, siginfo_t*, void*), struct sigaction* previous) {
    struct sigaction action;
    MemSet(&action, 0, sizeof(action));
    action.sa_sigaction = fn;
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, previous);
}

} // namespace

ENGINE_DEATH_TEST_FN(dt_e2e_app_handler_after) {
    InstallInto(argument);
    InstallAppHandler(&AppHandlerThatChains, &g_after_previous); // installed after: runs first, and calls ours
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("A handler the application installs after the crash handler runs first and, if it chains, the report and dump follow", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_app_handler_after", dir.Path());
    const StringView err = debug_test::StderrView(r);
    INFO("child stderr: " << Cat(err).CStr());
    CHECK(err.Contains("APP-HANDLER-RAN"));
    CHECK(err.Find("APP-HANDLER-RAN") < err.Find("=== CRASH REPORT ===")); // (both are Length() when missing)
    CHECK(r.native_code == SIGSEGV);
    engine_test::TextList envelopes;
    debug_test::FindFiles(dir.Path(), ".envelope", &envelopes);
    CHECK(envelopes.Size() == 1);
}

ENGINE_DEATH_TEST_FN(dt_e2e_app_handler_before) {
    InstallAppHandler(&E2eAppHandler, nullptr); // installed before the crash handler
    InstallInto(argument);
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("A handler installed before the crash handler is replaced by the library's and does not run", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_app_handler_before", dir.Path());
    const StringView err = debug_test::StderrView(r);
    INFO("child stderr: " << Cat(err).CStr());
    // The documented rule: install your own handler after InstallCrashHandler and chain to the previous one.
    CHECK(err.Contains("=== CRASH REPORT ==="));
    CHECK_FALSE(err.Contains("APP-HANDLER-RAN"));
    CHECK(r.native_code == SIGSEGV);
}

ENGINE_DEATH_TEST_FN(dt_e2e_uninstall_then_fault) {
    InstallInto(argument);
    UninstallCrashHandler();
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("After the crash handler is uninstalled a fault prints no report and leaves no dump", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_uninstall_then_fault", dir.Path());
    CHECK(r.native_code == SIGSEGV);
    CHECK_FALSE(r.StderrContains("=== CRASH REPORT ==="));
    engine_test::TextList envelopes;
    debug_test::FindFiles(dir.Path(), ".envelope", &envelopes);
    CHECK(envelopes.Empty());
}

// ---- the dump opens in a debugger ----------------------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_e2e_for_debugger) {
    InstallInto(argument);
    InitCrashContext(0, nullptr);
    char line[128];
    const int n = FormatTo(line, sizeof(line), "BUILD_ID=%s\n", GetCrashContext().build_id);
    os::Write(1, line, static_cast<usize>(n));
    WorkerFault(FaultKind::NullWrite, false);
}

namespace {

// Runs a shell command and returns everything it printed (standard error included).
Text RunCommand(StringView command) {
    debug_test::ShellRun run = debug_test::RunShell(Cat(command, " 2>&1").View());
    return static_cast<Text&&>(run.output);
}

} // namespace

TEST_CASE("The extracted dump opens in lldb: the faulting function, the other threads and the module with its build id", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    if (!RunCommand("command -v lldb").View().Contains("lldb")) {
        SKIP("lldb is not installed; this check opens the dump with it");
    }
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_for_debugger", dir.Path());
    const StringView out = debug_test::StdoutView(r);
    REQUIRE(out.Contains("BUILD_ID="));
    const StringView build_id = debug_test::ValueAfter(out, "BUILD_ID=");
    engine_test::TextList envelopes;
    debug_test::FindFiles(dir.Path(), ".envelope", &envelopes);
    REQUIRE(envelopes.Size() == 1);
    const Text content = debug_test::ReadWholeFile(envelopes[0]);
    crash_extract::Minidump dump;
    REQUIRE(crash_extract::FindMinidump(content.CStr(), content.Length(), &dump) == crash_extract::EnvelopeResult::Ok);
    const Text dump_path = Cat(dir.Path(), "/crash.dmp");
    REQUIRE(debug_test::WriteWholeFile(dump_path.View(), dump.data, dump.size));

    // DEBUGINFOD_URLS empty: the debugger must not go to the network to look for symbols.
    const Text output = RunCommand(Cat("DEBUGINFOD_URLS= lldb --no-lldbinit -b -c '", dump_path, "' -o 'thread list' -o 'bt all' -o 'image list'").View());
    INFO(output.CStr());
    CHECK(output.View().Contains("stop reason = signal SIGSEGV"));
    CHECK(output.View().Contains("InjectFault"));
    CHECK(output.View().Contains("thread #1"));
    CHECK(output.View().Contains("thread #2"));
    // The module list names the program with the id of its build (lldb prints the first 16 bytes as a GUID).
    Text guid = engine_test::MakeText();
    const StringView id_prefix = build_id.Substring(0, build_id.Length() < 32 ? build_id.Length() : 32);
    for (usize i = 0; i < id_prefix.Length(); ++i) {
        const char c = id_prefix[i];
        guid.Append(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
    }
    Text flat = engine_test::MakeText();
    for (usize i = 0; i < output.Length(); ++i) {
        if (output.CStr()[i] != '-') {
            flat.AppendRawBytes(output.CStr() + i, 1);
        }
    }
    CHECK(flat.View().Contains(guid.View()));
}

// ---- review fixes ---------------------------------------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_e2e_install_without_init) {
    // No InitDebugFacilities, no InitCrashContext: the installer must prepare them, or the first fault would start the
    // stack-trace setup (locks, allocation) inside the signal handler.
    CrashHandlerOptions options;
    options.dump_directory = argument;
    InstallCrashHandler(options);
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("Installing the crash handler prepares the stack-trace facilities, so the first fault reports with frames", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_install_without_init", dir.Path());
    const StringView err = debug_test::StderrView(r);
    INFO(Cat(err).CStr());
    CHECK(err.Contains("InjectFault"));
    CHECK(err.Contains("build id: "));
    CHECK(err.Contains("=== END OF REPORT ==="));
}

ENGINE_DEATH_TEST_FN(dt_e2e_install_and_exit_cleanly) {
    CrashHandlerOptions options;
    options.dump_directory = argument;
    InstallCrashHandler(options);
}

TEST_CASE("A crash report left by an earlier run is emptied when the handler is installed again", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult first = RunCrashChild("dt_e2e_null_write", dir.Path());
    CHECK(first.terminated);
    const Text report_path = Cat(dir.Path(), "/crash-report.txt");
    REQUIRE(debug_test::ReadWholeFile(report_path.View()).View().Contains("=== CRASH REPORT ==="));
    const ChildResult second = RunCrashChild("dt_e2e_install_and_exit_cleanly", dir.Path());
    CHECK(second.exited);
    CHECK(debug_test::ReadWholeFile(report_path.View()).Length() == 0);
}

ENGINE_DEATH_TEST_FN(dt_e2e_post_mortem_option) {
    CrashHandlerOptions options;
    options.dump_directory = argument;
    options.post_mortem_dumps = true; // the child's hard core limit is 0, so this reports Limited and no core is written
    InstallCrashHandler(options);
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("With post-mortem dumps requested the crash report says where the core goes", "[crash][crashreport][postmortem][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_post_mortem_option", dir.Path());
    const StringView err = debug_test::StderrView(r);
    INFO(Cat(err).CStr());
    CHECK(err.Contains("core dumps: none will be written: "));
    CHECK(err.Contains("hard core size limit is 0")); // the warning at install time
    CHECK(err.Contains("=== END OF REPORT ==="));
}

ENGINE_DEATH_TEST_FN(dt_e2e_alternate_stack_size) {
    CrashHandlerOptions options;
    options.dump_directory = argument;
    InstallCrashHandler(options);
    stack_t current;
    sigaltstack(nullptr, &current);
    char line[64];
    const int n = FormatTo(line, sizeof(line), "ALTSTACK=%zu\n", static_cast<usize>(current.ss_size));
    os::Write(1, line, static_cast<usize>(n));
}

TEST_CASE("Installing the crash handler leaves the installing thread a signal stack big enough for the report", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_alternate_stack_size", dir.Path());
    const StringView out = debug_test::StdoutView(r);
    REQUIRE(out.Contains("ALTSTACK="));
    const Text size = engine_test::MakeText(debug_test::ValueAfter(out, "ALTSTACK="));
    // The library's own stack is 16 KiB, which the report (an 8 KiB trace buffer and more) does not fit on.
    CHECK(CStrToULongLong(size.CStr(), nullptr, 10) >= 64 * 1024);
}

TEST_CASE("Writing to a pipe whose reader has stopped gives up after the timeout instead of hanging", "[crash][crashreport][debug][linux]") {
    int fds[2];
    REQUIRE(pipe(fds) == 0);
    // Fill the pipe without blocking.
    const int flags = fcntl(fds[1], F_GETFL);
    fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
    char block[4096];
    MemSet(block, 'x', sizeof(block));
    usize filled = 0;
    for (;;) {
        const ssize_t n = write(fds[1], block, sizeof(block));
        if (n <= 0) {
            break;
        }
        filled += static_cast<usize>(n);
    }
    fcntl(fds[1], F_SETFL, flags); // blocking again, as the report's descriptors are
    CHECK(filled > 0);
    const u64 before = os::MonotonicTimeNanos();
    const long written = os::WriteAllWithTimeout(fds[1], block, 1000, 100ull * 1000 * 1000);
    const u64 elapsed = os::MonotonicTimeNanos() - before;
    CHECK(written == 0);
    CHECK(elapsed < 2ull * 1000 * 1000 * 1000);
    // Once the reader catches up, everything goes through.
    char sink[4096];
    while (filled > 0) {
        const ssize_t n = read(fds[0], sink, sizeof(sink));
        REQUIRE(n > 0);
        filled -= static_cast<usize>(n);
    }
    CHECK(os::WriteAllWithTimeout(fds[1], block, 1000, 100ull * 1000 * 1000) == 1000);
    close(fds[0]);
    close(fds[1]);
}
