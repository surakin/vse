// The macOS debugger check (Phase 21 Day 291): sysctl(KERN_PROC_PID) and P_TRACED. A normal run is not traced; a child
// that asks to be traced (PT_TRACE_ME sets the same P_TRACED flag lldb's attach does) sees a debugger attached. Checked
// once under lldb itself by hand (the day file).
#include <catch2/catch_test_macros.hpp>

#include <sys/ptrace.h>
#include <sys/types.h>

#include "debug/death_test.h"
#include "debug/debugger.h"
#include "os/os.h"
#include "support/death_test_skip.h"

using namespace vse;
using namespace vse::debug;

ENGINE_DEATH_TEST_FN(dt_mac_traced)
{
    if (::ptrace(PT_TRACE_ME, 0, nullptr, 0) != 0)
    {
        os::Write(1, "NOPTRACE\n", 9);
        return;
    }
    os::Write(1, IsDebuggerAttached() ? "ATTACHED\n" : "DETACHED\n", 9);
}

TEST_CASE("macOS: a normal run has no debugger attached", "[debug][macos]")
{
    if (IsDebuggerAttached())
    {
        SKIP("this test run is itself under a debugger");
    }
    CHECK_FALSE(IsDebuggerAttached());
}

// The traced child runs one level down: PT_TRACE_ME makes its parent the tracer, and a tracer keeps state about the
// child that the test process must not carry into later tests (seen: "Repeated children leave no process or descriptor
// behind" failed after this case in one process).
ENGINE_DEATH_TEST_FN(dt_mac_tracer)
{
    const ChildResult inner = RunInChild("dt_mac_traced");
    os::Write(1, inner.stdout_text, inner.stdout_length);
}

TEST_CASE("macOS: a traced child sees a debugger attached", "[debug][death][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildOptions options;
    options.capture_stdout = true;
    const ChildResult r = RunInChild("dt_mac_tracer", "", options);
    REQUIRE(r.exited);
    if (r.StdoutContains("NOPTRACE"))
    {
        SKIP("ptrace(PT_TRACE_ME) is not allowed here");
    }
    CHECK(r.StdoutContains("ATTACHED"));
}
