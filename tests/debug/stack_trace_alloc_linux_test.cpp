// Checks that capturing, resolving and formatting a trace inside a signal handler allocates nothing once
// InitDebugFacilities has run. Built into its own executable (debug_alloc_tests) whose malloc, calloc and realloc are
// wrapped by counters (see CMakeLists.txt); it also holds the death-test children.
#include <catch2/catch_test_macros.hpp>

#include "debug/compiler.h"
#include "debug/death_test.h"
#include "debug/stack_trace.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace {

std::atomic<unsigned long> g_allocations{0};

} // namespace

extern "C" {
void* __real_malloc(size_t size);
void* __real_calloc(size_t count, size_t size);
void* __real_realloc(void* pointer, size_t size);
void* __wrap_malloc(size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    return __real_malloc(size);
}
void* __wrap_calloc(size_t count, size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    return __real_calloc(count, size);
}
void* __wrap_realloc(void* pointer, size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    return __real_realloc(pointer, size);
}
}

namespace {

char g_handler_text[8192];
FrameInfo g_handler_entries[kMaxStackFrames * 2];

void TraceInHandler(int) {
    const unsigned long before = g_allocations.load(std::memory_order_relaxed);
    StackTrace trace;
    CaptureStackTrace(trace, 0);
    const usize entries = ResolveStackTrace(trace, g_handler_entries, sizeof(g_handler_entries) / sizeof(g_handler_entries[0]));
    const usize length = FormatStackTrace(trace, g_handler_text, sizeof(g_handler_text));
    const unsigned long after = g_allocations.load(std::memory_order_relaxed);
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
