// Checks that capturing, resolving and formatting a trace inside a signal handler allocates nothing once
// InitDebugFacilities has run. Built into its own executable (debug_alloc_tests) whose malloc, calloc and realloc are
// wrapped by counters (see CMakeLists.txt); it also holds the death-test children.
#include <catch2/catch_test_macros.hpp>

#include "debug/compiler.h"
#include "debug/crash_context.h"
#include "debug/crash_report.h"
#include "debug/death_test.h"
#include "debug/stack_trace.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include "sync/atomic.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace {

Atomic<unsigned long> g_allocations{0};

} // namespace

extern "C" {
void* __real_malloc(size_t size);
void* __real_calloc(size_t count, size_t size);
void* __real_realloc(void* pointer, size_t size);
void* __wrap_malloc(size_t size) {
    g_allocations.FetchAdd(1, MemoryOrder::Relaxed);
    return __real_malloc(size);
}
void* __wrap_calloc(size_t count, size_t size) {
    g_allocations.FetchAdd(1, MemoryOrder::Relaxed);
    return __real_calloc(count, size);
}
void* __wrap_realloc(void* pointer, size_t size) {
    g_allocations.FetchAdd(1, MemoryOrder::Relaxed);
    return __real_realloc(pointer, size);
}
}

namespace {

char g_handler_text[8192];
FrameInfo g_handler_entries[kMaxStackFrames * 2];

void TraceInHandler(int) {
    const unsigned long before = g_allocations.Load(MemoryOrder::Relaxed);
    StackTrace trace;
    CaptureStackTrace(trace, 0);
    const usize entries = ResolveStackTrace(trace, g_handler_entries, sizeof(g_handler_entries) / sizeof(g_handler_entries[0]));
    const usize length = FormatStackTrace(trace, g_handler_text, sizeof(g_handler_text));
    const unsigned long after = g_allocations.Load(MemoryOrder::Relaxed);
    char line[96];
    const int n = std::snprintf(line, sizeof(line), "ALLOCS=%lu FRAMES=%u ENTRIES=%zu TEXT=%zu\n", after - before, trace.count,
                                static_cast<size_t>(entries), static_cast<size_t>(length));
    os::Write(1, line, static_cast<usize>(n));
    os::Write(1, g_handler_text, length);
}

ENGINE_NOINLINE void RaiseTheSignal() {
    std::raise(SIGUSR1);
    ENGINE_COMPILER_BARRIER();
}

} // namespace

ENGINE_DEATH_TEST_FN(dt_alloc_trace_in_signal_handler) {
    InitDebugFacilities(); // everything that may allocate happens here
    std::signal(SIGUSR1, &TraceInHandler);
    RaiseTheSignal();
}

TEST_CASE("Capturing, resolving and formatting a trace inside a signal handler allocates nothing", "[stacktrace][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_alloc_trace_in_signal_handler");
    REQUIRE(r.exited);
    CHECK(r.exit_code == 0);
    CHECK(r.StdoutContains("ALLOCS=0 "));
    CHECK_FALSE(r.StdoutContains("FRAMES=0 "));
    // The trace taken in the handler reaches the function that raised the signal.
    CHECK(r.StdoutContains("RaiseTheSignal"));
}

namespace {

void CountingWrite(void* context, const char* data, usize length) {
    *static_cast<usize*>(context) += length;
    (void)data;
}

void AllocProvider(void*, char* out, usize capacity) {
    std::snprintf(out, capacity, "alloc-test-state");
}

} // namespace

ENGINE_DEATH_TEST_FN(dt_alloc_crash_report) {
    InitDebugFacilities();
    InitCrashContext(0, nullptr);
    RegisterCrashStateProvider("alloc", &AllocProvider, nullptr);
    usize written = 0;
    CrashReportSink sink;
    sink.write = &CountingWrite;
    sink.context = &written;
    CrashReportInfo info;
    info.reason = CrashReason::AccessViolation;
    info.has_fault_address = true;
    info.fault_address = 8;
    StackTrace trace;
    CaptureStackTrace(trace, 0);
    info.trace = &trace;
    const unsigned long before = g_allocations.Load(MemoryOrder::Relaxed);
    WriteCrashReport(sink, info);
    const unsigned long after = g_allocations.Load(MemoryOrder::Relaxed);
    char line[64];
    const int n = std::snprintf(line, sizeof(line), "REPORT_ALLOCS=%lu BYTES=%zu\n", after - before, static_cast<size_t>(written));
    os::Write(1, line, static_cast<usize>(n));
}

TEST_CASE("Writing a crash report allocates nothing once the facilities are initialized", "[crashreport][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_alloc_crash_report");
    REQUIRE(r.exited);
    CHECK(r.exit_code == 0);
    CHECK(r.StdoutContains("REPORT_ALLOCS=0 "));
    CHECK_FALSE(r.StdoutContains("BYTES=0\n"));
}
