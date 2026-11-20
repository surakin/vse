// debug::InstallDiagnosticsHooks (debug/failure_hooks.h): after it runs, an assert report carries a real stack trace
// and the crash context, and new threads reach the crash handler's registration. The state before installation is
// tested in tests/foundation.
#include <catch2/catch_test_macros.hpp>

#include "debug/failure_hooks.h"
#include "log/assert.h"
#include "log/diagnostics_hooks.h"
#include "os/os.h"

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"

using namespace vse;

namespace
{

char g_output[16384];
usize g_output_length = 0;

long Capture(int, const void* data, usize length)
{
    const usize room = sizeof(g_output) - 1 - g_output_length;
    const usize take = length < room ? length : room;
    MemCopy(g_output + g_output_length, data, take);
    g_output_length += take;
    g_output[g_output_length] = '\0';
    return static_cast<long>(length);
}

bool Has(const char* text)
{
    return CStrFind(g_output, text) != nullptr;
}

}  // namespace

TEST_CASE("InstallDiagnosticsHooks gives the default assert report a stack trace and the crash context",
          "[debug][hooks]")
{
    debug::InstallDiagnosticsHooks();
    debug::InstallDiagnosticsHooks();  // harmless to repeat
    REQUIRE(GetFailureReportHooks() != nullptr);

    g_output_length = 0;
    g_output[0] = '\0';
    SetWriteSinkForTesting(&Capture);
    const AssertInfo info{AssertKind::Assert, "failure_hooks_test.cpp", 1, "TestBody", "false", "installed report"};
    const AssertAction action = DefaultAssertHandler(info);
    SetWriteSinkForTesting(&os::Write);

    CHECK(action != AssertAction::Continue);
    CHECK(Has("installed report"));
    CHECK(Has("#0"));             // a trace frame
    CHECK(Has("--- build ---"));  // the crash context
}

TEST_CASE("InstallDiagnosticsHooks installs the thread-start hook", "[debug][hooks]")
{
    debug::InstallDiagnosticsHooks();
    const ThreadStartHook hook = SetThreadStartHook(nullptr);
    CHECK(hook != nullptr);
    SetThreadStartHook(hook);
}
