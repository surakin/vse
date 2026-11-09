#include <catch2/catch_test_macros.hpp>

#include "debug/crash_reason.h"
#include "debug/death_test.h"
#include "debug/fault_injection.h"
#include "os/os.h"
#include "resource_guard.h"
#include "support/death_test_skip.h"

#include <cstdlib>
#include <cstring>
#include "sync/thread.h"
#include "build_defs.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

// ---- the functions the children run -------------------------------------------------------------------------------

ENGINE_DEATH_TEST_FN(dt_return_normally) {}

ENGINE_DEATH_TEST_FN(dt_exit_seven) {
    ExitChildProcess(7);
}

// The argument is the number of the FaultKind to cause.
ENGINE_DEATH_TEST_FN(dt_cause_fault) {
    InjectFault(static_cast<FaultKind>(argument[0] - '0'));
}

ENGINE_DEATH_TEST_FN(dt_flood_stderr) {
    char block[4096];
    std::memset(block, 'x', sizeof(block));
    for (int i = 0; i < 256; ++i) { // 1 MB, far more than a pipe holds
        os::Write(2, block, sizeof(block));
    }
}

ENGINE_DEATH_TEST_FN(dt_hang) {
    for (;;) {
        os::SleepNanos(10ull * 1000 * 1000);
    }
}

ENGINE_DEATH_TEST_FN(dt_close_stderr_then_crash) {
    os::Close(2);
    InjectFault(FaultKind::NullWrite);
}

ENGINE_DEATH_TEST_FN(dt_echo) {
    const char* extra = std::getenv("DT_EXTRA");
    const char* nested = std::getenv("ENGINE_DEATH_TEST_NOCORE");
    char line[256];
    usize n = 0;
    auto put = [&](const char* text) {
        const usize len = std::strlen(text);
        for (usize i = 0; i < len && n + 1 < sizeof(line); ++i) {
            line[n++] = text[i];
        }
    };
    put("arg=");
    put(argument);
    put(" extra=");
    put(extra != nullptr ? extra : "(unset)");
    put(" nocore=");
    put(nested != nullptr ? nested : "(unset)");
    put("\n");
    os::Write(2, line, n);
    os::Write(1, "to-stdout\n", 10);
}

// Prints the sanitizer options the child ended up with.
ENGINE_DEATH_TEST_FN(dt_echo_asan_options) {
    const char* value = std::getenv("ASAN_OPTIONS");
    char line[512];
    usize n = 0;
    for (const char* text : {"asan_options=", value != nullptr ? value : "(unset)", "\n"}) {
        const usize len = std::strlen(text);
        for (usize i = 0; i < len && n + 1 < sizeof(line); ++i) {
            line[n++] = text[i];
        }
    }
    os::Write(2, line, n);
}

// Writes to stderr from several threads without pause and never returns: fast enough that a reader which only stops when
// the pipe is empty would never stop.
ENGINE_DEATH_TEST_FN(dt_flood_forever) {
    auto flood = [] {
        static char block[65536];
        std::memset(block, 'y', sizeof(block));
        for (;;) {
            os::Write(2, block, sizeof(block));
        }
    };
    Thread a(flood);
    Thread b(flood);
    Thread c(flood);
    flood();
}

namespace {

const char* FaultArgument(FaultKind kind) {
    static char arg[2];
    arg[0] = static_cast<char>('0' + static_cast<int>(kind));
    arg[1] = '\0';
    return arg;
}

} // namespace

// ---- tests ----------------------------------------------------------------------------------------------------------

TEST_CASE("A child that returns normally exits with code 0", "[death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    const ChildResult r = RunInChild("dt_return_normally");
    CHECK(r.supported);
    CHECK(r.started);
    CHECK(r.exited);
    CHECK(r.exit_code == 0);
    CHECK_FALSE(r.terminated);
    CHECK_FALSE(r.timed_out);
    CHECK(r.reason == CrashReason::None);
    CHECK(guard.Clean());
}

TEST_CASE("A child that calls ExitChildProcess reports that exit code", "[death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    const ChildResult r = RunInChild("dt_exit_seven");
    CHECK(r.exited);
    CHECK(r.exit_code == 7);
    CHECK_FALSE(r.terminated);
    CHECK(guard.Clean());
}

TEST_CASE("Each fault kind ends the child with the matching crash reason", "[death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    struct Case {
        FaultKind kind;
        CrashReason expected;
    };
    // A stack overflow is an access violation as far as a POSIX exit status can tell; Windows' exit code says STATUS_STACK_OVERFLOW.
#if ENGINE_PLATFORM_WINDOWS
    constexpr CrashReason kOverflow = CrashReason::StackOverflow;
#else
    constexpr CrashReason kOverflow = CrashReason::AccessViolation;
#endif
    const Case cases[] = {
        {FaultKind::NullWrite, CrashReason::AccessViolation},
        {FaultKind::Abort, CrashReason::Abort},
        {FaultKind::IllegalInstruction, CrashReason::IllegalInstruction},
        {FaultKind::DivideByZero, CrashReason::ArithmeticFault},
        {FaultKind::StackOverflow, kOverflow},
        {FaultKind::BusError, CrashReason::BusError},
    };
    for (const Case& c : cases) {
        DYNAMIC_SECTION("fault " << static_cast<int>(c.kind)) {
            if (!FaultSupported(c.kind)) {
                SKIP("this fault is not available on this platform");
            }
            ChildResourceGuard guard;
            const ChildResult r = RunInChild("dt_cause_fault", FaultArgument(c.kind));
            INFO("reason is " << CrashReasonName(r.reason) << ", exit code " << r.exit_code << ", native code " << r.native_code);
            CHECK(r.started);
            CHECK_FALSE(r.exited);
            CHECK(r.terminated);
            CHECK(r.reason == c.expected);
            CHECK_FALSE(r.timed_out);
            CHECK(guard.Clean());
        }
    }
}

TEST_CASE("A child that writes a megabyte to stderr does not deadlock and the capture is flagged truncated", "[death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    const ChildResult r = RunInChild("dt_flood_stderr");
    CHECK(r.exited);
    CHECK(r.exit_code == 0);
    CHECK(r.stderr_length == kChildCaptureBytes);
    CHECK(r.stderr_truncated);
    CHECK(r.stderr_text[0] == 'x');
    CHECK(r.stderr_text[kChildCaptureBytes - 1] == 'x');
    CHECK(guard.Clean());
}

TEST_CASE("A child that hangs is killed at the timeout", "[death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    ChildOptions options;
    options.timeout_ms = 300;
    const u64 start = os::MonotonicTimeNanos();
    const ChildResult r = RunInChild("dt_hang", "", options);
    const u64 elapsed_ms = (os::MonotonicTimeNanos() - start) / 1000000ull;
    CHECK(r.started);
    CHECK(r.timed_out);
    CHECK(r.terminated);
    CHECK(r.reason == CrashReason::None); // killed from outside, not a fault
    CHECK(elapsed_ms >= 300);
    CHECK(elapsed_ms < 3000);
    CHECK(guard.Clean());
}

TEST_CASE("A child that closes stderr and then crashes is still reported", "[death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    const ChildResult r = RunInChild("dt_close_stderr_then_crash");
    CHECK(r.terminated);
    CHECK(r.reason == CrashReason::AccessViolation);
    CHECK(guard.Clean());
}

TEST_CASE("The child receives its argument and environment and its output is captured", "[death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    const char* env[] = {"DT_EXTRA=hello"};
    ChildOptions options;
    options.extra_env = env;
    options.extra_env_count = 1;
    const ChildResult r = RunInChild("dt_echo", "the-argument", options);
    CHECK(r.exited);
    CHECK(r.exit_code == 0);
    CHECK(r.StderrContains("arg=the-argument"));
    CHECK(r.StderrContains("extra=hello"));
    CHECK(r.StderrContains("nocore=1")); // core dumps are turned off by default
    CHECK(r.StdoutContains("to-stdout"));
    CHECK(guard.Clean());
}

TEST_CASE("Standard output can be discarded and core dumps left on", "[death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    ChildOptions options;
    options.capture_stdout = false;
    options.disable_core_dumps = false;
    const ChildResult r = RunInChild("dt_echo", "x", options);
    CHECK(r.exited);
    CHECK(r.stdout_length == 0);
    CHECK_FALSE(r.StdoutContains("to-stdout"));
    CHECK(r.StderrContains("nocore=(unset)"));
    CHECK(guard.Clean());
}

TEST_CASE("Repeated children leave no process or descriptor behind", "[death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    for (int i = 0; i < 25; ++i) {
        const ChildResult r = RunInChild("dt_return_normally");
        REQUIRE(r.exited);
    }
    CHECK(guard.Clean());
}

TEST_CASE("A sanitizer option the caller passes is merged with the fault flags, not duplicated", "[death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const char* env[] = {"ASAN_OPTIONS=abort_on_error=1"};
    ChildOptions options;
    options.extra_env = env;
    options.extra_env_count = 1;
    const ChildResult r = RunInChild("dt_echo_asan_options", "", options);
    CHECK(r.exited);
    // One entry holds both: the caller's flag first, then the flags that let faults reach the exit status.
    CHECK(r.StderrContains("abort_on_error=1"));
    CHECK(r.StderrContains("handle_segv=0"));
}

TEST_CASE("A child that writes without pause is still killed at the timeout", "[death][debug]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ChildResourceGuard guard;
    ChildOptions options;
    options.timeout_ms = 300;
    const u64 start = os::MonotonicTimeNanos();
    const ChildResult r = RunInChild("dt_flood_forever", "", options);
    const u64 elapsed_ms = (os::MonotonicTimeNanos() - start) / 1000000ull;
    CHECK(r.timed_out);
    CHECK(r.stderr_truncated);
    CHECK(elapsed_ms >= 300);
    CHECK(elapsed_ms < 3000);
    CHECK(guard.Clean());
}
