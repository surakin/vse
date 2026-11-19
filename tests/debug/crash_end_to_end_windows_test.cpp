// Crash handling end to end on Windows (Phase 13 Day 155): a child process installs the crash handler and faults in
// various ways; the parent checks the report on standard error, the report file, the envelope with its minidump, and
// the exception status the process ended with. The Linux counterpart is crash_end_to_end_linux_test.cpp; the cases that
// differ are noted where they are.
#include "containers/array.h"
#include "cstr/mem_ops.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "sync/atomic.h"
#include "sync/thread.h"

#include <catch2/catch_test_macros.hpp>

#include "debug/crash_context.h"
#include "debug/crash_handler.h"
#include "debug/crash_report.h"
#include "debug/death_test.h"
#include "debug/fault_injection.h"
#include "debug/stack_trace.h"
#include "debug/thread_info.h"
#include "envelope.h"
#include "hang_watchdog_windows.h"
#include "io/directory.h"
#include "io/file.h"
#include "io/file_system.h"
#include "io/path.h"
#include "log/log.h"
#include "os/os.h"
#include "os/signal.h"
#include "catch_engine_types.h"
#include "support/death_test_skip.h"
#include "test_text.h"

#include "os/windows/win32.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;
using engine_test::MakeText;
using engine_test::Text;
using engine_test::TextList;

namespace
{

// A number as 0x-prefixed hexadecimal text, for test messages.
Array<char, 24> HexText(i64 value)
{
    Array<char, 24> text = {};
    FormatTo(text.Data(), text.Size(), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

LogChannel LogEndToEndWindows("LogEndToEndWindows", LogVerbosity::Info);

constexpr i64 kAccessViolation = 0xC0000005;
constexpr i64 kFastFail = 0xC0000409;  // STATUS_STACK_BUFFER_OVERRUN: what __fastfail ends a process with

// A fresh directory under the temporary directory, removed with everything in it at the end of the scope.
class TempDir
{
public:
    TempDir()
    {
        io::Path base;
        if (io::TempDirectory(&base).Ok() && io::CreateTempDirectory(base, "engine-e2e-", &path_).Ok())
        {
            text_ = MakeText(path_.View());
            ok_ = true;
        }
    }
    ~TempDir()
    {
        if (ok_)
        {
            (void)io::RemoveAll(path_);
        }
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const char* Path() const { return text_.CStr(); }
    const io::Path& IoPath() const { return path_; }

private:
    io::Path path_;
    Text text_ = MakeText();
    bool ok_ = false;
};

io::WalkAction CollectFile(void* user, const io::WalkEntry& entry)
{
    if (entry.type == io::FileType::Regular)
    {
        static_cast<TextList*>(user)->Push(entry.path.View());
    }
    return io::WalkAction::Continue;
}

// The files below `directory` whose names match `pattern` (a wildcard such as "*.envelope").
TextList FindFiles(const io::Path& directory, const char* pattern)
{
    TextList found;
    io::WalkOptions options;
    options.name_pattern = StringView(pattern);
    (void)io::WalkDirectory(directory, options, &CollectFile, &found);
    return found;
}

Vector<u8> ReadWholeFile(StringView path)
{
    Vector<u8> bytes;
    io::Path parsed;
    if (io::Path::Parse(path, io::NativePathStyle(), &parsed))
    {
        (void)io::ReadFile(parsed, &bytes);
    }
    return bytes;
}

StringView AsText(const Vector<u8>& bytes)
{
    return StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
}

bool TextHas(StringView text, StringView needle)
{
    return text.Find(needle) != text.Length();
}

usize CountOf(StringView text, StringView needle)
{
    usize count = 0;
    for (usize from = 0; from < text.Length();)
    {
        const StringView rest = text.Substring(from);
        const usize at = rest.Find(needle);
        if (at == rest.Length())
        {
            break;
        }
        ++count;
        from += at + 1;
    }
    return count;
}

StringView Stderr(const ChildResult& r)
{
    return StringView(r.stderr_text, r.stderr_length);
}

void InstallInto(const char* directory)
{
    CrashHandlerOptions options;
    options.dump_directory = directory;
    InitDebugFacilities();
    InitCrashContext(0, nullptr);
    InstallCrashHandler(options);
    SetCrashThreadName("main-thread");
    Log(LogEndToEndWindows, LogVerbosity::Info, "logged-before-the-crash");
}

ChildResult RunCrashChild(const char* name, const char* directory)
{
    ChildOptions options;
    options.timeout_ms = 60000;
    return RunInChild(name, directory, options);
}

// The checks every crash shares: the report on standard error, one envelope with a minidump, the report file beside it.
void CheckReportAndDump(const ChildResult& r, const TempDir& dir, const char* reason_name)
{
    const StringView err = Stderr(r);
    const Text reason_line = engine_test::Concat("reason: ", StringView(reason_name));
    INFO("child stderr: " << Catch::Detail::stringify(err));
    INFO("child ended: exited " << r.exited << " code " << r.exit_code << ", terminated " << r.terminated << " status "
                                << HexText(r.native_code).Data() << ", timed out " << r.timed_out);
    CHECK(TextHas(err, "=== CRASH REPORT ==="));
    CHECK(TextHas(err, reason_line));
    CHECK(TextHas(err, "--- stack trace ---"));
    CHECK(TextHas(err, "build id: "));
    CHECK(TextHas(err, "logged-before-the-crash"));
    CHECK(TextHas(err, "=== END OF REPORT ==="));

    const TextList envelopes = FindFiles(dir.IoPath(), "*.envelope");
    REQUIRE(envelopes.Size() == 1);
    const Vector<u8> content = ReadWholeFile(envelopes[0]);
    crash_extract::Minidump dump;
    REQUIRE(crash_extract::FindMinidump(reinterpret_cast<const char*>(content.Data()), content.Size(), &dump) ==
            crash_extract::EnvelopeResult::Ok);
    CHECK(MemCompare(dump.data, "MDMP", 4) == 0);
    // The report travels in the envelope too (attached), and sits beside the dump.
    CHECK(TextHas(AsText(content), "=== CRASH REPORT ==="));
    const Vector<u8> file = ReadWholeFile((dir.IoPath() / "crash-report.txt").View());
    CHECK(TextHas(AsText(file), "=== CRASH REPORT ==="));
    CHECK(TextHas(AsText(file), reason_line));
}

}  // namespace

// ---- one child per kind of fault
// -----------------------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_e2e_win_null_write)
{
    InstallInto(argument);
    InjectFault(FaultKind::NullWrite);
}
ENGINE_DEATH_TEST_FN(dt_e2e_win_illegal_instruction)
{
    InstallInto(argument);
    InjectFault(FaultKind::IllegalInstruction);
}
ENGINE_DEATH_TEST_FN(dt_e2e_win_divide_by_zero)
{
    InstallInto(argument);
    InjectFault(FaultKind::DivideByZero);
}
ENGINE_DEATH_TEST_FN(dt_e2e_win_abort)
{
    InstallInto(argument);
    InjectFault(FaultKind::Abort);
}
// The child reports abort()'s flags through its exit code: 1 if installing the handler left the C runtime's message (a
// dialog in a Debug build) or its Windows Error Reporting call on, 2 if uninstalling did not bring them back, 0 if both
// held. The death-test child starts with the flags cleared (PlatformPrepareChild), so it sets them first, as a program
// starts.
ENGINE_DEATH_TEST_FN(dt_e2e_win_abort_flags)
{
    constexpr unsigned int kFlags = _WRITE_ABORT_MSG | _CALL_REPORTFAULT;
    _set_abort_behavior(kFlags, kFlags);
    InstallInto(argument);
    const unsigned int installed = _set_abort_behavior(0, 0) & kFlags;
    UninstallCrashHandler();
    const unsigned int after = _set_abort_behavior(0, 0) & kFlags;
    os::Exit(installed != 0 ? 1 : (after != kFlags ? 2 : 0));
}
ENGINE_DEATH_TEST_FN(dt_e2e_win_stack_overflow)
{
    InstallInto(argument);
    InjectFault(FaultKind::StackOverflow);
}

TEST_CASE("Windows: a null write prints the report, leaves a dump and the process ends with an access violation",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    REQUIRE(dir.Path()[0] != '\0');
    const ChildResult r = RunCrashChild("dt_e2e_win_null_write", dir.Path());
    CHECK(r.reason == CrashReason::AccessViolation);
    CHECK(r.native_code == kAccessViolation);
    CheckReportAndDump(r, dir, "AccessViolation at 0x");
    const StringView err = Stderr(r);
    CHECK(TextHas(err, "\"main-thread\""));
    // While a crash is handled the frames are module and offset only (no DbgHelp: it locks and allocates), "#0
    // 0x<address>  <module>+0x<offset>".
    const usize frame = err.Find("#0 ");
    REQUIRE(frame != err.Length());
    const Text first = MakeText(err.Substring(frame, err.Substring(frame).IndexOf('\n')));
    INFO(first.CStr());
    unsigned long long address = 0;
    unsigned long long offset = 0;
    const char* plus = CStrFind(first.CStr(), "+0x");
    REQUIRE(ScanFrom(first.CStr(), "#0  0x%llx", &address) == 1);
    REQUIRE(plus != nullptr);
    REQUIRE(ScanFrom(plus, "+0x%llx", &offset) == 1);
    CHECK(TextHas(first, "debug_tests"));
    CHECK(((address - offset) & 0xffffull) == 0);  // the offset is from the image base, which is 64 KiB aligned
    // The first frame is the function that faulted, not the hook that walked the stack: the same offset in this process
    // (the same executable) names it.
    StackTrace here;
    here.frames[0] = reinterpret_cast<u64>(GetModuleHandleW(nullptr)) + offset +
                     1;  // + 1: the resolver looks up the byte before a frame
    here.count = 1;
    char text[4096];
    FormatStackTrace(here, text, sizeof(text));
    INFO(text);
    CHECK(CStrFind(text, "InjectFault") != nullptr);
}

TEST_CASE("Windows: an illegal instruction, a divide by zero and an abort each report their reason",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    {
        TempDir dir;
        const ChildResult r = RunCrashChild("dt_e2e_win_illegal_instruction", dir.Path());
        CHECK(r.reason == CrashReason::IllegalInstruction);
        CheckReportAndDump(r, dir, "IllegalInstruction");
    }
    if (FaultSupported(FaultKind::DivideByZero))
    {
        TempDir dir;
        const ChildResult r = RunCrashChild("dt_e2e_win_divide_by_zero", dir.Path());
        CHECK(r.reason == CrashReason::ArithmeticFault);
        CheckReportAndDump(r, dir, "ArithmeticFault");
    }
    {
        TempDir dir;
        const ChildResult r = RunCrashChild("dt_e2e_win_abort", dir.Path());
        CHECK(r.reason == CrashReason::Abort);
        CheckReportAndDump(r, dir, "Abort");
        CHECK(TextHas(Stderr(r), "reason: Abort\n"));  // no address: an abort has no faulting instruction (as on Linux)
    }
}

// Day 161: a windowed program's abort() waited on the C runtime's dialog box instead of reaching the crash handler.
TEST_CASE("Windows: while the crash handler is installed, abort() shows no dialog and calls no error reporting",
          "[crash][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    REQUIRE(dir.Path()[0] != '\0');
    const ChildResult r = RunCrashChild("dt_e2e_win_abort_flags", dir.Path());
    INFO("child ended: exited " << r.exited << " code " << r.exit_code << ", terminated " << r.terminated << " status "
                                << HexText(r.native_code).Data() << ", timed out " << r.timed_out);
    INFO("child stderr: " << Catch::Detail::stringify(Stderr(r)));
    REQUIRE(r.exited);
    CHECK(r.exit_code == 0);
}

TEST_CASE("Windows: a stack overflow on the main thread prints a report and leaves a dump",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_stack_overflow", dir.Path());
    CHECK(r.terminated);
    CHECK(r.reason == CrashReason::StackOverflow);
    CheckReportAndDump(r, dir, "StackOverflow");
    CHECK(TextHas(Stderr(r), "\"main-thread\""));
}

// ---- other threads
// ------------------------------------------------------------------------------------------------------------

namespace
{

void WorkerFault(FaultKind kind, bool register_thread)
{
    Thread worker(
        [kind, register_thread]
        {
            if (register_thread)
            {
                RegisterThreadForCrashHandling();
            }
            SetCrashThreadName("worker-7");
            InjectFault(kind);
        });
    worker.Join();
}

}  // namespace

ENGINE_DEATH_TEST_FN(dt_e2e_win_worker_fault)
{
    InstallInto(argument);
    WorkerFault(FaultKind::NullWrite, false);
}
ENGINE_DEATH_TEST_FN(dt_e2e_win_worker_stack_overflow)
{
    InstallInto(argument);
    WorkerFault(FaultKind::StackOverflow, true);
}
ENGINE_DEATH_TEST_FN(dt_e2e_win_worker_abort)
{
    InstallInto(argument);
    WorkerFault(FaultKind::Abort, true);
}

TEST_CASE("Windows: a fault on another thread names that thread", "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_worker_fault", dir.Path());
    CHECK(r.reason == CrashReason::AccessViolation);
    CheckReportAndDump(r, dir, "AccessViolation at 0x");
    const StringView err = Stderr(r);
    CHECK(TextHas(err, "\"worker-7\""));
    CHECK_FALSE(TextHas(err, "\"main-thread\""));  // the main thread did not fault
}

TEST_CASE("Windows: a stack overflow on a registered worker thread still produces a report and a dump",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_worker_stack_overflow", dir.Path());
    CHECK(r.terminated);
    CheckReportAndDump(r, dir, "StackOverflow");
    CHECK(TextHas(Stderr(r), "\"worker-7\""));
}

// abort() raises no exception the observer sees; the report finds the thread by its registered stack instead.
TEST_CASE("Windows: an abort on a registered worker thread names that thread",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_worker_abort", dir.Path());
    CHECK(r.reason == CrashReason::Abort);
    CheckReportAndDump(r, dir, "Abort");
    const StringView err = Stderr(r);
    CHECK(TextHas(err, "\"worker-7\""));
    CHECK_FALSE(TextHas(err, "\"main-thread\""));
}

ENGINE_DEATH_TEST_FN(dt_e2e_win_unregistered_worker_abort)
{
    InstallInto(argument);
    WorkerFault(FaultKind::Abort, false);
}

// Nothing tells the report which thread called abort() when it was never registered: it says so, rather than naming the
// crash library's handler thread it runs on.
TEST_CASE("Windows: an abort on a thread never registered is reported for an unknown thread",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_unregistered_worker_abort", dir.Path());
    CHECK(r.reason == CrashReason::Abort);
    CheckReportAndDump(r, dir, "Abort");
    const StringView err = Stderr(r);
    INFO("child stderr: " << Catch::Detail::stringify(err));
    CHECK(TextHas(err, "thread: unknown\n"));
    CHECK_FALSE(TextHas(err, "\"main-thread\""));
}

ENGINE_DEATH_TEST_FN(dt_e2e_win_two_threads_fault)
{
    InstallInto(argument);
    Atomic<int> ready{0};
    auto body = [&ready]
    {
        ready.FetchAdd(1);
        while (ready.Load() < 2)
        {
            ThisThread::Yield();
        }
        InjectFault(FaultKind::NullWrite);
    };
    Thread a(body);
    Thread b(body);
    a.Join();
    b.Join();
}

TEST_CASE("Windows: two threads faulting at once produce one report and one dump",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_two_threads_fault", dir.Path());
    CHECK(r.terminated);
    CheckReportAndDump(r, dir, "AccessViolation");
    CHECK(CountOf(Stderr(r), "=== CRASH REPORT ===") == 1);
}

namespace
{
Atomic<bool> g_win_slow_provider_started{false};
void WinSlowProvider(void*, char* out, usize capacity)
{
    g_win_slow_provider_started = true;
    os::SleepNanos(400ull * 1000 * 1000);  // long enough for the other thread to fault in the middle of the report
    FormatTo(out, capacity, "slow but done");
}
}  // namespace

ENGINE_DEATH_TEST_FN(dt_e2e_win_second_fault_during_report)
{
    InstallInto(argument);
    RegisterCrashStateProvider("slow", &WinSlowProvider, nullptr);
    Thread second(
        []
        {
            while (!g_win_slow_provider_started.Load())
            {
                ThisThread::Yield();
            }
            os::SleepNanos(50ull * 1000 * 1000);
            InjectFault(FaultKind::NullWrite);
        });
    second.Detach();                    // InjectFault does not return: the process ends with the crash
    InjectFault(FaultKind::NullWrite);  // the main thread reports first, slowly
}

TEST_CASE("Windows: a second thread faulting while the first is being reported does not cut the report short",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_second_fault_during_report", dir.Path());
    CheckReportAndDump(r, dir, "AccessViolation");
    const StringView err = Stderr(r);
    CHECK(TextHas(err, "slow: slow but done"));
    CHECK(TextHas(err, "\"main-thread\""));
    CHECK(CountOf(err, "=== CRASH REPORT ===") == 1);
}

// ---- when things go wrong in the report
// ------------------------------------------------------------------------------------------

namespace
{
void E2eWinFaultingProvider(void*, char*, usize)
{
    InjectFault(FaultKind::NullWrite);
}
}  // namespace

ENGINE_DEATH_TEST_FN(dt_e2e_win_provider_faults)
{
    InstallInto(argument);
    RegisterCrashStateProvider("bad", &E2eWinFaultingProvider, nullptr);
    InjectFault(FaultKind::NullWrite);
}

// On Windows the dump is written before the report (the library calls on_crash after it), so a fault in the report
// loses only the report's rest; on Linux the report comes first and the dump follows it.
TEST_CASE("Windows: a state provider that faults prints double fault, and the dump is still there",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_provider_faults", dir.Path());
    const StringView err = Stderr(r);
    INFO("child stderr: " << Catch::Detail::stringify(err));
    CHECK(r.terminated);
    CHECK_FALSE(r.timed_out);
    CHECK(r.native_code == kAccessViolation);
    CHECK(CountOf(err, "=== CRASH REPORT ===") == 1);
    CHECK(TextHas(err, "double fault"));
    CHECK(FindFiles(dir.IoPath(), "*.envelope").Size() == 1);
}

ENGINE_DEATH_TEST_FN(dt_e2e_win_assert_fails)
{
    InstallInto(argument);
    ENGINE_ASSERT(false, "end to end assert");
}

TEST_CASE("Windows: a failed assert prints its report, aborts, and the crash report and a dump follow",
          "[crash][crashreport][assert][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_assert_fails", dir.Path());
    CHECK(r.reason == CrashReason::Abort);
    CHECK(r.StdoutContains("ASSERT FAILED: "));
    CHECK(r.StdoutContains("end to end assert"));
    CHECK(r.StdoutContains("--- build ---"));
    CHECK(r.StderrContains("=== CRASH REPORT ==="));
    CHECK(r.StderrContains("reason: Abort"));
    CHECK(FindFiles(dir.IoPath(), "*.envelope").Size() == 1);
    CHECK(TextHas(AsText(ReadWholeFile((dir.IoPath() / "crash-report.txt").View())), "=== CRASH REPORT ==="));
}

// ---- uninstalling, and what the handler cannot catch
// -------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_e2e_win_uninstall_then_fault)
{
    InstallInto(argument);
    UninstallCrashHandler();
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("Windows: after the crash handler is uninstalled a fault prints no report and leaves no dump",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_uninstall_then_fault", dir.Path());
    CHECK(r.native_code == kAccessViolation);
    CHECK_FALSE(r.StderrContains("=== CRASH REPORT ==="));
    CHECK(FindFiles(dir.IoPath(), "*.envelope").Empty());
}

// ---- a filter the program set before the crash handler (crash handler chaining, Day 2)
// ----------------------------------------------------------------------------

namespace
{

// The program's own filter: says on standard error which call this is, the exception it got and whether it runs on the
// thread that faulted (g_faulting_thread, set by that thread just before the fault). Test code: it formats.
Atomic<u32> g_host_filter_calls{0};
Atomic<u32> g_faulting_thread{0};
LONG g_host_filter_answer = EXCEPTION_CONTINUE_SEARCH;

void SayHostFilter(const EXCEPTION_POINTERS* info)
{
    const u32 call = g_host_filter_calls.FetchAdd(1) + 1;
    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    const unsigned long long address = record.NumberParameters >= 2
                                           ? static_cast<unsigned long long>(record.ExceptionInformation[1])
                                           : 0ull;
    char text[256];
    const int n = FormatTo(text, sizeof(text), "HOST-FILTER call %u code 0x%08lx address 0x%llx on %s thread\n", call,
                           static_cast<unsigned long>(record.ExceptionCode), address,
                           GetCurrentThreadId() == g_faulting_thread.Load() ? "the faulting" : "another");
    if (n > 0)
    {
        os::Write(2, text, static_cast<usize>(n));
    }
}

LONG WINAPI HostFilter(EXCEPTION_POINTERS* info)
{
    SayHostFilter(info);
    return g_host_filter_answer;
}

LONG WINAPI FaultingHostFilter(EXCEPTION_POINTERS* info)
{
    SayHostFilter(info);
    InjectFault(FaultKind::NullWrite);  // does not return
}

DWORD WINAPI FaultOnThisThread(LPVOID)
{
    InjectFault(FaultKind::NullWrite);  // does not return
}

// A filter during which another thread faults: that thread must wait at the gate (one report, one dump), not reach the
// library's filter, which is top-level again once the library has returned.
LONG WINAPI HostFilterWhileAnotherThreadFaults(EXCEPTION_POINTERS* info)
{
    SayHostFilter(info);
    const HANDLE other = CreateThread(nullptr, 0, &FaultOnThisThread, nullptr, 0, nullptr);
    if (other != nullptr)
    {
        Sleep(1000);  // the other thread's fault is reached and waits (or, without the gate on top, is dumped)
        CloseHandle(other);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void HostAbortHandler(int)
{
    static const char kText[] = "HOST-SIGABRT\n";
    os::Write(2, kText, sizeof(kText) - 1);
}

void FaultHere()
{
    g_faulting_thread.Store(GetCurrentThreadId());
    InjectFault(FaultKind::NullWrite);
}

ENGINE_DEATH_TEST_FN(dt_e2e_win_host_filter)
{
    SetUnhandledExceptionFilter(&HostFilter);
    InstallInto(argument);
    FaultHere();
}
// The filter answers "continue execution": not followed, the process still ends with the access violation.
ENGINE_DEATH_TEST_FN(dt_e2e_win_host_filter_continue)
{
    g_host_filter_answer = EXCEPTION_CONTINUE_EXECUTION;
    SetUnhandledExceptionFilter(&HostFilter);
    InstallInto(argument);
    FaultHere();
}
ENGINE_DEATH_TEST_FN(dt_e2e_win_host_filter_faults)
{
    SetUnhandledExceptionFilter(&FaultingHostFilter);
    InstallInto(argument);
    FaultHere();
}
ENGINE_DEATH_TEST_FN(dt_e2e_win_host_filter_second_fault)
{
    SetUnhandledExceptionFilter(&HostFilterWhileAnotherThreadFaults);
    InstallInto(argument);
    FaultHere();
}
ENGINE_DEATH_TEST_FN(dt_e2e_win_host_filter_on_worker)
{
    SetUnhandledExceptionFilter(&HostFilter);
    InstallInto(argument);
    Thread worker(
        []()
        {
            RegisterThreadForCrashHandling("worker");
            FaultHere();
        });
    worker.Join();
}
// Install, uninstall, install: the second install notes the host's filter again, and it runs once.
ENGINE_DEATH_TEST_FN(dt_e2e_win_host_filter_reinstall)
{
    hang_watchdog::Start(30);  // a hang under load was seen once: its stacks, not only a timeout
    SetUnhandledExceptionFilter(&HostFilter);
    InstallInto(argument);
    UninstallCrashHandler();
    CrashHandlerOptions options;
    options.dump_directory = argument;
    if (InstallCrashHandler(options) != CrashHandlerResult::Installed)
    {
        os::Exit(7);
    }
    FaultHere();
}
// A second install is refused and changes nothing: the host's filter, not the engine's gate, runs once.
ENGINE_DEATH_TEST_FN(dt_e2e_win_host_filter_install_twice)
{
    hang_watchdog::Start(30);  // a hang under load was seen once: its stacks, not only a timeout
    SetUnhandledExceptionFilter(&HostFilter);
    InstallInto(argument);
    CrashHandlerOptions options;
    options.dump_directory = argument;
    if (InstallCrashHandler(options) != CrashHandlerResult::AlreadyInstalled)
    {
        os::Exit(7);
    }
    FaultHere();
}
// abort() raises no exception, so no unhandled-exception filter sees it (as without the engine); the program's SIGABRT
// handler runs after the report and the dump (the library calls the one it replaced).
ENGINE_DEATH_TEST_FN(dt_e2e_win_host_abort_handler)
{
    os::SetSignalHandler(SIGABRT, &HostAbortHandler);
    SetUnhandledExceptionFilter(&HostFilter);
    InstallInto(argument);
    InjectFault(FaultKind::Abort);
}

// The host's line comes after the report's end, once, and the process ends with the access violation.
void CheckHostFilterRanOnceAfterReport(const ChildResult& r, const TempDir& dir)
{
    CheckReportAndDump(r, dir, "AccessViolation at 0x");
    const StringView err = Stderr(r);
    CHECK(r.native_code == kAccessViolation);
    CHECK(CountOf(err, "HOST-FILTER call") == 1);
    const usize end = err.Find("=== END OF REPORT ===");
    const usize host = err.Find("HOST-FILTER call 1 code 0xc0000005");
    REQUIRE(host != err.Length());
    CHECK(end < host);
}

}  // namespace

TEST_CASE("Windows: a filter set before the crash handler runs once after the report and the dump, on the faulting "
          "thread, with the original exception",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_host_filter", dir.Path());
    CheckHostFilterRanOnceAfterReport(r, dir);
    const StringView err = Stderr(r);
    CHECK(TextHas(err, "on the faulting thread"));
    // The address the filter got is the one the report names.
    const usize at = err.Find("AccessViolation at 0x");
    const usize host = err.Find("HOST-FILTER call 1");
    REQUIRE(at != err.Length());
    REQUIRE(host != err.Length());
    unsigned long long reported = 1;
    unsigned long long received = 2;
    const Text report_line = MakeText(err.Substring(at, err.Substring(at).IndexOf('\n')));
    const Text host_line = MakeText(err.Substring(host, err.Substring(host).IndexOf('\n')));
    INFO(report_line.CStr() << " / " << host_line.CStr());
    REQUIRE(ScanFrom(report_line.CStr(), "AccessViolation at 0x%llx", &reported) == 1);
    const char* address = CStrFind(host_line.CStr(), "address 0x");
    REQUIRE(address != nullptr);
    REQUIRE(ScanFrom(address, "address 0x%llx", &received) == 1);
    CHECK(reported == received);
}

TEST_CASE("Windows: the earlier filter's answer does not keep the process alive, and a fault inside it does not loop",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    {
        TempDir dir;
        const ChildResult r = RunCrashChild("dt_e2e_win_host_filter_continue", dir.Path());
        CheckHostFilterRanOnceAfterReport(r, dir);
    }
    {
        TempDir dir;
        const ChildResult r = RunCrashChild("dt_e2e_win_host_filter_faults", dir.Path());
        CHECK_FALSE(r.timed_out);
        CheckHostFilterRanOnceAfterReport(r, dir);
        CHECK(TextHas(Stderr(r), "the program's own exception filter faulted after the crash report"));
        CHECK(CountOf(Stderr(r), "=== CRASH REPORT ===") == 1);
    }
    {
        // Review finding: once the library has returned its filter is top-level again; the gate goes back on top
        // before the earlier filter runs, so a second thread faulting meanwhile is neither reported nor dumped.
        TempDir dir;
        const ChildResult r = RunCrashChild("dt_e2e_win_host_filter_second_fault", dir.Path());
        CHECK_FALSE(r.timed_out);
        CheckHostFilterRanOnceAfterReport(r, dir);  // one envelope
        CHECK(CountOf(Stderr(r), "=== CRASH REPORT ===") == 1);
    }
}

TEST_CASE("Windows: the earlier filter runs on a worker thread that faults",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_host_filter_on_worker", dir.Path());
    CheckHostFilterRanOnceAfterReport(r, dir);
    CHECK(TextHas(Stderr(r), "\"worker\""));
    CHECK(TextHas(Stderr(r), "on the faulting thread"));
}

TEST_CASE("Windows: install, uninstall, install and a refused second install each call the earlier filter once",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const Array<const char*, 2> children = {"dt_e2e_win_host_filter_reinstall", "dt_e2e_win_host_filter_install_twice"};
    for (const char* child : children)
    {
        INFO(child);
        TempDir dir;
        const ChildResult r = RunCrashChild(child, dir.Path());
        if (r.timed_out || r.native_code == 0xDEAD0002)
        {
            hang_watchdog::NameFrames(r.stderr_text, r.stderr_length);
        }
        CHECK_FALSE(r.exited);  // 7: the install's result was not the expected one
        CheckHostFilterRanOnceAfterReport(r, dir);
    }
}

TEST_CASE("Windows: an abort calls the earlier SIGABRT handler once after the report, and no exception filter",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_host_abort_handler", dir.Path());
    CheckReportAndDump(r, dir, "Abort");
    const StringView err = Stderr(r);
    CHECK(CountOf(err, "HOST-SIGABRT") == 1);
    CHECK_FALSE(TextHas(err, "HOST-FILTER"));
    const usize end = err.Find("=== END OF REPORT ===");
    const usize host = err.Find("HOST-SIGABRT");
    REQUIRE(host != err.Length());
    CHECK(end < host);
    // abort()'s exit code; the harness's own SIGABRT marker is not printed (the program's handler replaced it).
    CHECK(r.exited);
    CHECK(r.exit_code == 3);
}

ENGINE_DEATH_TEST_FN(dt_e2e_win_fast_fail)
{
    InstallInto(argument);
    __fastfail(7);  // FAST_FAIL_FATAL_APP_EXIT: the kernel ends the process without running any handler
}

// The known gap (BACKEND.md, foundation-debugging.md, KNOWN-ISSUES.md): __fastfail, which the C runtime and the
// compiler's security checks use, ends the process in the kernel; no exception filter, vectored handler or library sees
// it, so there is no report and no dump in the handler's directory (Windows Error Reporting may still keep one, Day
// 156).
TEST_CASE("Windows: a fast fail ends the process with 0xC0000409 and no engine report",
          "[crash][crashreport][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult r = RunCrashChild("dt_e2e_win_fast_fail", dir.Path());
    CHECK(r.terminated);
    CHECK(r.native_code == kFastFail);
    CHECK_FALSE(r.StderrContains("=== CRASH REPORT ==="));
    CHECK(FindFiles(dir.IoPath(), "*.envelope").Empty());
}

// ---- the dump directory, and dumps across a restart
// ---------------------------------------------------------------------------------

namespace
{

// %LOCALAPPDATA%\<app>, which the default crash dump directory of an application named <app> sits in; removed at the
// end of the scope.
class AppDataFolder
{
public:
    explicit AppDataFolder(const char* app)
    {
        char local[MAX_PATH] = {};
        const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", local, MAX_PATH);
        ok_ = n > 0 && n < MAX_PATH && io::Path::Parse(StringView(local), io::PathStyle::Windows, &path_);
        if (ok_)
        {
            path_ = path_.Join(StringView(app));
        }
    }
    ~AppDataFolder()
    {
        if (ok_)
        {
            (void)io::RemoveAll(path_);
        }
    }
    AppDataFolder(const AppDataFolder&) = delete;
    AppDataFolder& operator=(const AppDataFolder&) = delete;
    bool Ok() const { return ok_; }
    const io::Path& Path() const { return path_; }

private:
    io::Path path_;
    bool ok_ = false;
};

void PutResult(const char* key, int value)
{
    char line[64];
    const int n = FormatTo(line, sizeof(line), "%s=%d\n", key, value);
    os::Write(1, line, static_cast<usize>(n));
}

}  // namespace

ENGINE_DEATH_TEST_FN(dt_e2e_win_default_directory_then_fault)
{
    hang_watchdog::Start(20);
    InitDebugFacilities();
    CrashHandlerOptions options;  // no dump_directory: io::CrashDumpDirectory, %LOCALAPPDATA%\<ENGINE_APP_NAME>\crashes
    PutResult("INSTALLED", static_cast<int>(InstallCrashHandler(options)));
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("Windows: without a directory in the options the dump goes under the application's local folder",
          "[crash][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    // A name of this process's own: ctest runs the plain and the ASan copy of this case at the same time, and a shared
    // folder made each remove the other's dump (seen on Day 156).
    const Text name = engine_test::Format("engine-e2e-default-dir-%lu",
                                          static_cast<unsigned long>(GetCurrentProcessId()));
    AppDataFolder app(name.CStr());
    REQUIRE(app.Ok());
    const Text variable = engine_test::Concat("ENGINE_APP_NAME=", name.View());
    const char* const extra[] = {variable.CStr()};
    ChildOptions options;
    options.extra_env = extra;
    options.extra_env_count = 1;
    options.timeout_ms = 60000;
    const ChildResult r = RunInChild("dt_e2e_win_default_directory_then_fault", "", options);
    CHECK(r.StdoutContains("INSTALLED=0"));
    CHECK(r.reason == CrashReason::AccessViolation);
    CHECK(FindFiles(app.Path().Join("crashes"), "*.envelope").Size() == 1);
}

ENGINE_DEATH_TEST_FN(dt_e2e_win_bad_directory)
{
    CrashHandlerOptions options;
    options.dump_directory = argument;  // a path under a file: it cannot be created
    PutResult("RESULT", static_cast<int>(InstallCrashHandler(options)));
    PutResult("INSTALLED", CrashHandlerInstalled() ? 1 : 0);
    os::Write(1, "PROGRAM-RAN-ON\n", 15);
}

TEST_CASE("Windows: a dump directory that cannot be created fails the install, and the program runs on",
          "[crash][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    REQUIRE(io::WriteFile(dir.IoPath().Join("a-file"), "x", 1).Ok());
    const Text bad = MakeText(dir.IoPath().Join("a-file").Join("crashes").View());
    const ChildResult r = RunInChild("dt_e2e_win_bad_directory", bad.CStr());
    REQUIRE(r.exited);
    CHECK(r.StdoutContains(
        engine_test::Format("RESULT=%d", static_cast<int>(CrashHandlerResult::DirectoryFailed)).CStr()));
    CHECK(r.StdoutContains("INSTALLED=0"));
    CHECK(r.StdoutContains("PROGRAM-RAN-ON"));
}

ENGINE_DEATH_TEST_FN(dt_e2e_win_install_and_exit)
{
    hang_watchdog::Start(20);  // (KNOWN-ISSUES.md: this child has hung once in a while under ASan)
    CrashHandlerOptions options;
    options.dump_directory = argument;
    PutResult("INSTALLED", static_cast<int>(InstallCrashHandler(options)));
    UninstallCrashHandler();
    os::Write(1, "UNINSTALLED\n", 12);
}

// The library moves the last run's envelope into saved\ when the handler is installed again (PruneSavedDumps then keeps
// the newest).
TEST_CASE("Windows: a crash dump is still there after the program is started again", "[crash][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    TempDir dir;
    const ChildResult crashed = RunCrashChild("dt_e2e_win_null_write", dir.Path());
    REQUIRE(crashed.terminated);
    REQUIRE(FindFiles(dir.IoPath(), "*.envelope").Size() == 1);
    for (int launch = 0; launch < 2; ++launch)
    {
        const ChildResult restarted = RunCrashChild("dt_e2e_win_install_and_exit", dir.Path());
        INFO("launch " << launch << ": terminated " << restarted.terminated << " status "
                       << HexText(restarted.native_code).Data() << ", timed out " << restarted.timed_out << ", stdout "
                       << Catch::Detail::stringify(StringView(restarted.stdout_text, restarted.stdout_length))
                       << ", stderr " << Catch::Detail::stringify(Stderr(restarted)));
        if (!restarted.exited)
        {
            hang_watchdog::NameFrames(restarted.stderr_text, restarted.stderr_length);
        }
        REQUIRE(restarted.exited);
        CHECK(restarted.StdoutContains("INSTALLED=0"));
    }
    const TextList after = FindFiles(dir.IoPath(), "*.envelope");
    REQUIRE(after.Size() == 1);
    INFO(after[0].CStr());
    CHECK((TextHas(after[0], "\\saved\\") ||
           TextHas(after[0], "/saved/")));  // (an io::Path's own text may use either separator)
    const Vector<u8> kept = ReadWholeFile(after[0]);
    crash_extract::Minidump dump;
    REQUIRE(crash_extract::FindMinidump(reinterpret_cast<const char*>(kept.Data()), kept.Size(), &dump) ==
            crash_extract::EnvelopeResult::Ok);
    CHECK(dump.size > 1000);
}
