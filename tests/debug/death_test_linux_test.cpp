// Linux-only death-test checks: signal numbers, a grandchild that keeps the pipes open, process groups and the working
// directory. The neutral tests are in death_test_run_test.cpp.
#include <catch2/catch_test_macros.hpp>

#include "debug/death_test.h"
#include "debug/fault_injection.h"
#include "os/os.h"
#include "resource_guard.h"
#include "support/death_test_skip.h"

#include <fcntl.h>
#include "os/console.h"
#include "os/last_error.h"
#include "os/signal.h"
#include "support/test_support_posix.h"
#include "cstr/cstr.h"
#include <sys/types.h>
#include <unistd.h>
#include "cstr/format.h"
#include "cstr/number.h"

using namespace vse;
using namespace vse::debug;

// Starts a grandchild that keeps the inherited pipes open for a second, then returns at once: the helper must come back
// when the child is gone, not when the pipes close.
ENGINE_DEATH_TEST_FN(dt_linux_orphan_holds_pipes)
{
    if (fork() == 0)
    {
        usleep(1000 * 1000);
        _exit(0);
    }
}

// Starts a grandchild that sleeps for a long time, prints its pid, and hangs.
ENGINE_DEATH_TEST_FN(dt_linux_hang_with_grandchild)
{
    const pid_t grandchild = fork();
    if (grandchild == 0)
    {
        for (;;)
        {
            usleep(1000 * 1000);
        }
    }
    char line[48];
    const int n = FormatTo(line, sizeof(line), "GRANDCHILD=%d\n", static_cast<int>(grandchild));
    os::Write(1, line, static_cast<usize>(n));
    for (;;)
    {
        usleep(10 * 1000);
    }
}

// Writes its own pid to the file named by the argument, then hangs.
ENGINE_DEATH_TEST_FN(dt_linux_hang_report_pid)
{
    char line[32];
    const int n = FormatTo(line, sizeof(line), "%d\n", static_cast<int>(getpid()));
    const int fd = os::Open(argument, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    os::Write(fd, line, static_cast<usize>(n));
    os::Close(fd);
    for (;;)
    {
        usleep(10 * 1000);
    }
}

// Runs a hanging child of its own with a long timeout; the test kills this process while that child runs.
ENGINE_DEATH_TEST_FN(dt_linux_nested_parent)
{
    ChildOptions options;
    options.timeout_ms = 60000;
    RunInChild("dt_linux_hang_report_pid", argument, options);
}

ENGINE_DEATH_TEST_FN(dt_linux_null_write)
{
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("A grandchild that keeps the pipes open does not keep the helper waiting", "[death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    const u64 start = os::MonotonicTimeNanos();
    const ChildResult r = RunInChild("dt_linux_orphan_holds_pipes");
    const u64 elapsed_ms = (os::MonotonicTimeNanos() - start) / 1000000ull;
    CHECK(r.exited);
    CHECK(r.exit_code == 0);
    CHECK(elapsed_ms < 700);  // the grandchild holds the pipes for a full second
    CHECK(guard.Clean());
}

TEST_CASE("A timeout kills the child's whole process group", "[death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    ChildOptions options;
    options.timeout_ms = 400;
    const ChildResult r = RunInChild("dt_linux_hang_with_grandchild", "", options);
    REQUIRE(r.timed_out);
    // The child's standard output starts with the test framework's own text; the pid follows the marker.
    const char* marker = CStrFind(r.stdout_text, "GRANDCHILD=");
    REQUIRE(marker != nullptr);
    const pid_t grandchild = static_cast<pid_t>(CStrToInt(marker + CStrLength("GRANDCHILD=")));
    REQUIRE(grandchild > 1);
    // The grandchild was killed with the group; give the kernel a moment to finish tearing it down.
    bool gone = false;
    for (int i = 0; i < 100 && !gone; ++i)
    {
        gone = (kill(grandchild, 0) != 0 && os::LastError() == ESRCH);
        if (!gone)
        {
            usleep(10 * 1000);
        }
    }
    CHECK(gone);
    CHECK(guard.Clean());
}

TEST_CASE("The signal behind a crash is kept next to the reason", "[death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_linux_null_write");
    CHECK(r.terminated);
    CHECK(r.native_code == SIGSEGV);
    CHECK(r.reason == CrashReason::AccessViolation);
}

TEST_CASE("The helper works from another working directory", "[death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    char previous[4096];
    REQUIRE(getcwd(previous, sizeof(previous)) != nullptr);
    REQUIRE(chdir("/") == 0);
    const ChildResult r = RunInChild("dt_linux_null_write");
    REQUIRE(chdir(previous) == 0);
    CHECK(r.started);
    CHECK(r.terminated);
    CHECK(r.native_code == SIGSEGV);
}

TEST_CASE("A child's own child dies with it when its parent is killed", "[death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    // The grandchild is in its own process group, so killing the child's group does not reach it: only the
    // parent-death signal the helper arms in every child does.
    const char* tmp = os::GetEnvironment("TMPDIR");
    char path[256];
    FormatTo(path, sizeof(path), "%s/engine-death-test-%d.pid", tmp != nullptr ? tmp : "/tmp",
             static_cast<int>(getpid()));
    unlink(path);
    ChildOptions options;
    options.timeout_ms = 800;
    const ChildResult r = RunInChild("dt_linux_nested_parent", path, options);
    REQUIRE(r.timed_out);

    pid_t grandchild = 0;
    for (int i = 0; i < 100 && grandchild <= 1; ++i)
    {
        const debug_test::Text recorded = debug_test::ReadWholeFile(path);
        if (recorded.Length() != 0)
        {
            grandchild = static_cast<pid_t>(CStrToInt(recorded.CStr()));
        }
        if (grandchild <= 1)
        {
            usleep(10 * 1000);
        }
    }
    unlink(path);
    REQUIRE(grandchild > 1);
    bool gone = false;
    for (int i = 0; i < 200 && !gone; ++i)
    {
        gone = (kill(grandchild, 0) != 0 && os::LastError() == ESRCH);
        if (!gone)
        {
            usleep(10 * 1000);
        }
    }
    if (!gone)
    {
        kill(grandchild, SIGKILL);  // do not leave the orphan behind when the check fails
    }
    CHECK(gone);
}

TEST_CASE("A child whose status is reaped elsewhere is reported as unobserved, not as a clean exit",
          "[death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    struct sigaction ignore;
    struct sigaction previous;
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    ignore.sa_flags = 0;
    REQUIRE(sigaction(SIGCHLD, &ignore, &previous) == 0);
    const ChildResult r = RunInChild("dt_linux_null_write");  // with SIGCHLD ignored the kernel reaps the child itself
    sigaction(SIGCHLD, &previous, nullptr);
    CHECK(r.started);
    CHECK_FALSE(r.exited);
    CHECK_FALSE(r.terminated);
    CHECK_FALSE(r.timed_out);
    CHECK(r.reason == CrashReason::None);
}
