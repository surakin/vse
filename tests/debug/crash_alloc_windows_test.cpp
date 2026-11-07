// The crash report allocates nothing on Windows (Phase 13 Day 155). Linux proves it by wrapping malloc, calloc and realloc at link time
// (stack_trace_alloc_linux_test.cpp, GNU ld --wrap), which link.exe and lld-link cannot do. The Windows form is the debug C runtime's
// allocation hook (_CrtSetAllocHook): it sees every malloc, calloc, realloc and operator new of the C runtime, so it exists only in a Debug
// build and not under AddressSanitizer (which replaces the C runtime's allocator). It does not see HeapAlloc called directly by Windows
// itself; os/windows/debug/BACKEND.md lists what the report calls into Windows for, and why none of it is DbgHelp.
//
// The child builds the EXCEPTION_POINTERS a crash would bring and runs the same report the crash library's hook runs, counting the
// allocations made on its own thread meanwhile. A second child plants an allocation in a state provider, to show the count sees one.
#include <catch2/catch_test_macros.hpp>

#include "debug/crash_context.h"
#include "debug/crash_event_platform.h"
#include "debug/crash_handler.h"
#include "debug/crash_report.h"
#include "debug/death_test.h"
#include "debug/stack_trace.h"
#include "io/file_system.h"
#include "io/path.h"
#include "catch_engine_types.h"
#include "support/death_test_skip.h"
#include "test_text.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "os/windows/win32.h"

#if defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
#define CRASH_ALLOC_HOOK 1
#include <crtdbg.h>
#endif

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace {

#if defined(CRASH_ALLOC_HOOK)
std::atomic<u32> g_counted_thread{0};
std::atomic<long> g_allocations{0};

StackTrace g_first_allocation; // where the first counted allocation came from, printed when there is one

int CountAllocation(int type, void*, size_t, int, long, const byte*, int) { // (the hook type spells the file name as unsigned char)
    if (type != _HOOK_FREE && GetCurrentThreadId() == g_counted_thread.load(std::memory_order_relaxed)) {
        if (g_allocations.fetch_add(1, std::memory_order_relaxed) == 0) CaptureStackTrace(g_first_allocation, 0);
    }
    return TRUE; // let the allocation go on
}
#endif

void PlantedAllocationProvider(void*, char* out, usize capacity) {
    void* block = std::malloc(64); // what the report must never do: the check has to see this one
    std::snprintf(out, capacity, "planted %s", block != nullptr ? "yes" : "no");
    std::free(block);
}

// Installs the crash handler into `directory`, then reports a made-up access violation from this thread the way the library's hook does,
// and prints how many C runtime allocations the report made.
void ReportAndCount(const char* directory, bool plant) {
    CrashHandlerOptions options;
    options.dump_directory = directory;
    InitDebugFacilities();
    InitCrashContext(0, nullptr);
    if (InstallCrashHandler(options) != CrashHandlerResult::Installed) {
        std::printf("INSTALL-FAILED\n");
        return;
    }
    if (plant) RegisterCrashStateProvider("planted", &PlantedAllocationProvider, nullptr);
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    record.NumberParameters = 2;
    record.ExceptionInformation[0] = 1; // a write
    record.ExceptionInformation[1] = 0x10;
    EXCEPTION_POINTERS pointers{};
    pointers.ExceptionRecord = &record;
    pointers.ContextRecord = &context;
#if defined(CRASH_ALLOC_HOOK)
    g_counted_thread.store(GetCurrentThreadId());
    _CrtSetAllocHook(&CountAllocation);
    debug::detail::ReportCrashFromExceptionPointers(&pointers);
    _CrtSetAllocHook(nullptr);
    g_counted_thread.store(0);
    std::printf("ALLOCS=%ld;\n", g_allocations.load()); // the ";" ends the number (standard output turns a newline into two bytes)
    if (g_allocations.load() != 0) {
        static char text[16384];
        FormatStackTrace(g_first_allocation, text, sizeof(text)); // after the count: resolving allocates
        std::printf("first allocation:\n%s", text);
    }
#else
    debug::detail::ReportCrashFromExceptionPointers(&pointers);
#endif
    std::fflush(stdout);
    UninstallCrashHandler();
}

// A fresh directory for the child's dump directory, removed at the end of the scope.
class AllocTestDir {
public:
    AllocTestDir() {
        io::Path base;
        if (io::TempDirectory(&base).Ok() && io::CreateTempDirectory(base, "engine-alloc-", &path_).Ok()) {
            text_ = engine_test::MakeText(path_.View());
            ok_ = true;
        }
    }
    ~AllocTestDir() {
        if (ok_) (void)io::RemoveAll(path_);
    }
    AllocTestDir(const AllocTestDir&) = delete;
    AllocTestDir& operator=(const AllocTestDir&) = delete;
    const char* Path() const { return text_.CStr(); }

private:
    io::Path path_;
    engine_test::Text text_ = engine_test::MakeText();
    bool ok_ = false;
};

} // namespace

ENGINE_DEATH_TEST_FN(dt_alloc_win_report) {
    ReportAndCount(argument, false);
}

ENGINE_DEATH_TEST_FN(dt_alloc_win_report_planted) {
    ReportAndCount(argument, true);
}

// A call through a null function pointer, as the hook receives it: the instruction pointer is 0 and the return address is on top of the
// stack (a made-up one here, holding this function's own address, so the walk has somewhere to go and then reads zeros and stops).
ENGINE_DEATH_TEST_FN(dt_report_win_null_call) {
    CrashHandlerOptions options;
    options.dump_directory = argument;
    InitDebugFacilities();
    InitCrashContext(0, nullptr);
    if (InstallCrashHandler(options) != CrashHandlerResult::Installed) return;
    static u64 fake_stack[64] = {};
    CONTEXT context{};
    RtlCaptureContext(&context);
    fake_stack[0] = context.Rip;
    context.Rsp = reinterpret_cast<u64>(&fake_stack[0]);
    context.Rip = 0;
    EXCEPTION_RECORD record{};
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    record.NumberParameters = 2;
    record.ExceptionInformation[0] = 8; // an execute
    record.ExceptionInformation[1] = 0;
    EXCEPTION_POINTERS pointers{};
    pointers.ExceptionRecord = &record;
    pointers.ContextRecord = &context;
    debug::detail::ReportCrashFromExceptionPointers(&pointers);
    UninstallCrashHandler();
}

TEST_CASE("Windows: a crash in a call through a null pointer still reports the caller", "[crash][crashreport][death][debug][windows]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    AllocTestDir dir;
    const ChildResult r = RunInChild("dt_report_win_null_call", dir.Path());
    INFO(Catch::Detail::stringify(StringView(r.stderr_text, r.stderr_length)));
    REQUIRE(r.exited);
    CHECK(r.StderrContains("#0  0x0000000000000000"));
    CHECK(r.StderrContains("#1  0x"));
}

TEST_CASE("Windows: the crash report makes no C runtime allocation, and the check sees a planted one", "[crash][crashreport][death][debug][windows]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
#if !defined(CRASH_ALLOC_HOOK)
    SKIP("the allocation hook needs the debug C runtime without AddressSanitizer (a Debug build, plain flavor)");
#else
    {
        AllocTestDir dir;
        REQUIRE(dir.Path()[0] != '\0');
        const ChildResult r = RunInChild("dt_alloc_win_report", dir.Path());
        INFO(Catch::Detail::stringify(StringView(r.stdout_text, r.stdout_length)));
        REQUIRE(r.exited);
        CHECK(r.StderrContains("=== END OF REPORT ==="));
        CHECK_FALSE(r.StderrContains("ReportAndCount")); // frames are module and offset only: no DbgHelp while a crash is reported
        CHECK(r.StdoutContains("ALLOCS=0;"));
    }
    {
        AllocTestDir dir;
        const ChildResult r = RunInChild("dt_alloc_win_report_planted", dir.Path());
        REQUIRE(r.exited);
        CHECK(r.StderrContains("planted: planted yes"));
        CHECK_FALSE(r.StdoutContains("ALLOCS=0;"));
        CHECK(r.StdoutContains("ALLOCS="));
    }
#endif
}
