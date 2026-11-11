// Linux-only checks of the assert family: the debugger break, debugger detection, and the file that parses it. The
// neutral tests are in assert_family_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include "debug/compiler.h"
#include "debug/death_test.h"
#include "debug/debugger.h"
#include "debug/module_list.h"
#include "log/assert.h"
#include "os/linux/debug/debugger_linux.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include "os/signal.h"
#include <link.h>
#include "cstr/cstr.h"
#include <sys/ptrace.h>
#include <unistd.h>

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace
{

AssertAction LinBreakHandler(const AssertInfo&)
{
    return AssertAction::Break;
}

void LinTrapHandler(int)
{
    os::Write(1, "TRAP\n", 5);
}

}  // namespace

// Stands in for a debugger: a SIGTRAP handler that records the break and lets the program go on.
ENGINE_DEATH_TEST_FN(dt_linux_debug_break)
{
    os::SetSignalHandler(SIGTRAP, &LinTrapHandler);
    os::Write(1, "BEFORE\n", 7);
    ENGINE_DEBUG_BREAK();
    os::Write(1, "AFTER\n", 6);
}

ENGINE_DEATH_TEST_FN(dt_linux_assert_breaks_then_resumes)
{
    os::SetSignalHandler(SIGTRAP, &LinTrapHandler);
    SetAssertHandler(&LinBreakHandler);
    ENGINE_ASSERT(false, "break please");
    os::Write(1, "RESUMED-AFTER-ASSERT\n", 21);
    ENGINE_ENSURE(false, "break again");
    os::Write(1, "RESUMED-AFTER-ENSURE\n", 21);
}

// A debugger that resumes after the break must not get to run on past a place declared unreachable.
ENGINE_DEATH_TEST_FN(dt_linux_unreachable_breaks_then_aborts)
{
    os::SetSignalHandler(SIGTRAP, &LinTrapHandler);
    SetAssertHandler(&LinBreakHandler);
    ENGINE_UNREACHABLE("cannot get here");
    os::Write(1, "RAN-PAST-UNREACHABLE\n", 21);
}

ENGINE_DEATH_TEST_FN(dt_linux_traced)
{
    if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) != 0)
    {
        os::Write(1, "NOPTRACE\n", 9);
        return;
    }
    os::Write(1, IsDebuggerAttached() ? "ATTACHED\n" : "DETACHED\n", 9);
}

TEST_CASE("ENGINE_DEBUG_BREAK raises a trap at its own line and execution continues after it", "[debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_linux_debug_break");
    REQUIRE(r.exited);
    CHECK(r.exit_code == 0);
    const char* before = CStrFind(r.stdout_text, "BEFORE");
    const char* trap = CStrFind(r.stdout_text, "TRAP");
    const char* after = CStrFind(r.stdout_text, "AFTER");
    REQUIRE(before != nullptr);
    REQUIRE(trap != nullptr);
    REQUIRE(after != nullptr);
    CHECK(before < trap);
    CHECK(trap < after);
}

TEST_CASE("A handler that answers Break stops at the failing line of an assert and of an ensure, then both resume",
          "[assert][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_linux_assert_breaks_then_resumes");
    REQUIRE(r.exited);
    CHECK(r.exit_code == 0);
    const char* first_trap = CStrFind(r.stdout_text, "TRAP");
    REQUIRE(first_trap != nullptr);
    const char* resumed_assert = CStrFind(r.stdout_text, "RESUMED-AFTER-ASSERT");
    REQUIRE(resumed_assert != nullptr);
    CHECK(first_trap < resumed_assert);
    const char* second_trap = CStrFind(first_trap + 1, "TRAP");
    REQUIRE(second_trap != nullptr);
    CHECK(second_trap > resumed_assert);
    CHECK(CStrFind(r.stdout_text, "RESUMED-AFTER-ENSURE") != nullptr);
}

TEST_CASE("A debugger that resumes after ENGINE_UNREACHABLE breaks is followed by the abort",
          "[assert][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_linux_unreachable_breaks_then_aborts");
    CHECK(r.StdoutContains("TRAP"));
    CHECK_FALSE(r.StdoutContains("RAN-PAST-UNREACHABLE"));
    CHECK(r.terminated);
    CHECK(r.reason == CrashReason::Abort);
}

TEST_CASE("The tracer line of /proc/self/status decides whether a debugger is attached", "[debug][linux]")
{
    using engine_placeholder::debug::detail::StatusShowsTracer;
    auto shows = [](const char* text) { return StatusShowsTracer(text, CStrLength(text)); };
    CHECK_FALSE(shows("Name:\tx\nTracerPid:\t0\nUid:\t1000\n"));
    CHECK(shows("Name:\tx\nTracerPid:\t4242\nUid:\t1000\n"));
    CHECK(shows("TracerPid:   7\n"));
    CHECK_FALSE(shows("TracerPid:\t000\n"));
    CHECK_FALSE(shows("Name:\tTracerPid:\t99\nOther:\t1\n"));  // the key must start a line
    CHECK_FALSE(shows(""));
    CHECK_FALSE(shows("TracerPid:"));  // no digits
    CHECK_FALSE(shows("NoTracerPid:\t5\n"));
}

TEST_CASE("A normal run has no debugger attached", "[debug][linux]")
{
    if (IsDebuggerAttached())
    {
        SKIP("this test run is itself under a debugger");
    }
    CHECK_FALSE(IsDebuggerAttached());
}

TEST_CASE("A traced child sees a debugger attached", "[debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_linux_traced");
    REQUIRE(r.exited);
    if (r.StdoutContains("NOPTRACE"))
    {
        SKIP("ptrace is not allowed here");
    }
    CHECK(r.StdoutContains("ATTACHED"));
}

namespace
{

struct LinModuleCheck
{
    int objects = 0;
    int missing = 0;
};

int LinCheckObject(struct dl_phdr_info* info, usize, void* data)
{
    LinModuleCheck* check = static_cast<LinModuleCheck*>(data);
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        if (info->dlpi_phdr[i].p_type == PT_LOAD)
        {
            ++check->objects;
            LoadedModule found;
            if (!FindLoadedModule(static_cast<u64>(info->dlpi_addr) + info->dlpi_phdr[i].p_vaddr, &found))
            {
                ++check->missing;
            }
            break;
        }
    }
    return 0;
}

}  // namespace

TEST_CASE("Every object the loader reports is in the module list, however many there are", "[stacktrace][debug][linux]")
{
    RefreshLoadedModules();
    LinModuleCheck check;
    dl_iterate_phdr(&LinCheckObject, &check);
    CHECK(check.objects > 3);
    CHECK(check.missing == 0);
}
