#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"

#include "log/assert.h"
#include "log/diagnostics_hooks.h"
#include "log/log.h"
#include "os/os.h"
#include "string/string.h"
#include "types.h"
#include "cstr/mem_ops.h"

// Phase 12 Day 142: the assert report and the thread registration reach the debugging module through hooks, and are complete
// without them.

using engine_placeholder::AssertAction;
using engine_placeholder::AssertInfo;
using engine_placeholder::AssertKind;
using engine_placeholder::DefaultAssertHandler;
using engine_placeholder::FailureReportHooks;
using engine_placeholder::GetFailureReportHooks;
using engine_placeholder::NotifyThreadStart;
using engine_placeholder::SetFailureReportHooks;
using engine_placeholder::SetThreadStartHook;
using engine_placeholder::SetWriteSinkForTesting;
using engine_placeholder::StringView;
using engine_placeholder::u32;
using engine_placeholder::usize;

namespace {

char g_captured[8192];
usize g_captured_length = 0;

long CaptureSink(int, const void* buffer, usize length) {
    const usize room = sizeof(g_captured) - g_captured_length;
    const usize take = length < room ? length : room;
    engine_placeholder::MemCopy(g_captured + g_captured_length, buffer, take);
    g_captured_length += take;
    return static_cast<long>(length);
}

StringView Captured() {
    return StringView(g_captured, g_captured_length);
}

int g_trace_calls = 0;
u32 g_trace_skip = 0;

usize FakeTrace(char* buffer, usize capacity, u32 skip) {
    ++g_trace_calls;
    g_trace_skip = skip;
    static const char kText[] = "  #0 fake_frame\n";
    const usize length = sizeof(kText) - 1;
    if (length > capacity) return 0;
    engine_placeholder::MemCopy(buffer, kText, length);
    return length;
}

void FakeContext(void (*write)(void*, const char*, usize), void* context) {
    static const char kText[] = "--- fake context ---\n";
    write(context, kText, sizeof(kText) - 1);
}

bool g_debugger = false;
bool FakeDebugger() {
    return g_debugger;
}

const FailureReportHooks kFakeHooks = {&FakeTrace, &FakeContext, &FakeDebugger};

AssertInfo MakeInfo(AssertKind kind) {
    return AssertInfo{kind, "hooks_test.cpp", 7, "TestFunction", "x == 1", "the message"};
}

// Puts the hooks back as they were when the test began.
struct HookGuard {
    const FailureReportHooks* previous = GetFailureReportHooks();
    ~HookGuard() {
        SetFailureReportHooks(previous);
        SetWriteSinkForTesting(&engine_placeholder::os::Write);
    }
};

} // namespace

TEST_CASE("An assert with no report hooks prints its message line and aborts", "[log][assert][hooks]") {
    HookGuard guard;
    SetFailureReportHooks(nullptr);
    g_captured_length = 0;
    SetWriteSinkForTesting(&CaptureSink);

    const AssertAction action = DefaultAssertHandler(MakeInfo(AssertKind::Assert));

    CHECK(action == AssertAction::Abort);
    CHECK(Captured().Contains(StringView("ASSERT FAILED")));
    CHECK(Captured().Contains(StringView("the message")));
    CHECK_FALSE(Captured().Contains(StringView("fake")));
}

TEST_CASE("An ensure with no report hooks prints and continues", "[log][assert][hooks]") {
    HookGuard guard;
    SetFailureReportHooks(nullptr);
    g_captured_length = 0;
    SetWriteSinkForTesting(&CaptureSink);

    CHECK(DefaultAssertHandler(MakeInfo(AssertKind::Ensure)) == AssertAction::Continue);
    CHECK(Captured().Contains(StringView("the message")));
}

TEST_CASE("Installed report hooks add the trace and the context, and the debugger decides Break", "[log][assert][hooks]") {
    HookGuard guard;
    g_captured_length = 0;
    g_trace_calls = 0;
    g_debugger = false;
    SetWriteSinkForTesting(&CaptureSink);
    CHECK(SetFailureReportHooks(&kFakeHooks) == guard.previous);

    CHECK(DefaultAssertHandler(MakeInfo(AssertKind::Assert)) == AssertAction::Abort);
    CHECK(g_trace_calls == 1);
    CHECK(g_trace_skip == 2);
    CHECK(Captured().Contains(StringView("fake_frame")));
    CHECK(Captured().Contains(StringView("fake context")));

    g_debugger = true;
    CHECK(DefaultAssertHandler(MakeInfo(AssertKind::Assert)) == AssertAction::Break);
}

TEST_CASE("An ensure gets the trace but not the crash context", "[log][assert][hooks]") {
    HookGuard guard;
    g_captured_length = 0;
    g_debugger = false;
    SetWriteSinkForTesting(&CaptureSink);
    SetFailureReportHooks(&kFakeHooks);

    CHECK(DefaultAssertHandler(MakeInfo(AssertKind::Ensure)) == AssertAction::Continue);
    CHECK(g_trace_skip == 4);
    CHECK(Captured().Contains(StringView("fake_frame")));
    CHECK_FALSE(Captured().Contains(StringView("fake context")));
}

namespace {
int g_started = 0;
char g_started_name[16];
void RecordStart(const char* name) {
    ++g_started;
    engine_placeholder::CStrCopyN(g_started_name, name != nullptr ? name : "", sizeof(g_started_name) - 1);
}
} // namespace

TEST_CASE("NotifyThreadStart does nothing without a hook and calls the installed hook", "[log][hooks]") {
    const auto previous = SetThreadStartHook(nullptr);
    g_started = 0;
    NotifyThreadStart("nobody");
    CHECK(g_started == 0);

    SetThreadStartHook(&RecordStart);
    NotifyThreadStart("io");
    CHECK(g_started == 1);
    CHECK(StringView(g_started_name) == StringView("io"));

    SetThreadStartHook(previous);
}
