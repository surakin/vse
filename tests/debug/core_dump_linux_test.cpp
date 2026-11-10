// Core dumps on Linux: the description of core_pattern, a hard limit of zero, and a real core found with scripts/find-core.sh
// and opened with gdb.
#include <catch2/catch_test_macros.hpp>

#include "debug/core_dump_linux.h"
#include "debug/death_test.h"
#include "debug/fault_injection.h"
#include "debug/post_mortem.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "build_defs.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "support/test_support_linux.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;
using debug_test::Cat;
using debug_test::Text;

#if ENGINE_SANITIZED
#define CORE_TEST_UNDER_SANITIZER 1
#endif

namespace {

Text Describe(const char* pattern) {
    char text[512];
    DescribeCorePattern(pattern, text, sizeof(text));
    return engine_test::MakeText(text);
}

// The command's output with standard error merged in; its wait status goes to `status` when given.
Text RunShell(StringView command, int* status = nullptr) {
    debug_test::ShellRun run = debug_test::RunShell(Cat(command, " 2>&1").View());
    if (status != nullptr) {
        *status = run.status;
    }
    return static_cast<Text&&>(run.output);
}

} // namespace

TEST_CASE("A piped core pattern from systemd-coredump points at coredumpctl", "[postmortem][debug][linux]") {
    const Text text = Describe("|/usr/lib/systemd/systemd-coredump %P %u %g %s %t %c %h");
    CHECK(text.View().Contains("systemd-coredump"));
    CHECK(text.View().Contains("coredumpctl"));
    CHECK(CorePatternIsPipe("|/usr/lib/systemd/systemd-coredump"));
}

TEST_CASE("Another piped core pattern names the program, and a file pattern names the pattern", "[postmortem][debug][linux]") {
    const Text piped = Describe("|/usr/local/bin/my-collector %p");
    CHECK(piped.View().Contains("/usr/local/bin/my-collector %p"));
    CHECK_FALSE(piped.View().Contains("coredumpctl"));
    CHECK(CorePatternIsPipe("|/usr/local/bin/my-collector"));

    const Text file = Describe("core.%e.%p");
    CHECK(file.View().Contains("'core.%e.%p'"));
    CHECK(file.View().Contains("working directory"));
    CHECK_FALSE(CorePatternIsPipe("core.%e.%p"));
}

TEST_CASE("An empty or missing core pattern is reported as unknown", "[postmortem][debug][linux]") {
    CHECK(Describe("").View().Contains("unknown"));
    CHECK(Describe(nullptr).View().Contains("unknown"));
}

TEST_CASE("A description longer than the buffer is cut, not overrun", "[postmortem][debug][linux]") {
    char small[16];
    DescribeCorePattern("core.%e.%p.%t.%h.%s", small, sizeof(small));
    CHECK(CStrLength(small) == sizeof(small) - 1);
}

ENGINE_DEATH_TEST_FN(dt_post_mortem_hard_zero) {
    const struct rlimit none = {0, 0};
    ::setrlimit(RLIMIT_CORE, &none);
    const PostMortemReport report = EnablePostMortemDumps(PostMortemOptions());
    char line[600];
    const int n = FormatTo(line, sizeof(line), "RESULT=%d\nNOTE=%s\n", static_cast<int>(report.result), report.note);
    os::Write(1, line, static_cast<usize>(n));
}

TEST_CASE("A hard core limit of zero is reported as limited, with what to do about it", "[postmortem][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_post_mortem_hard_zero");
    REQUIRE(r.exited);
    CHECK(r.StdoutContains("RESULT=1"));
    CHECK(r.StdoutContains("ulimit -c unlimited"));
}

ENGINE_DEATH_TEST_FN(dt_post_mortem_crash) {
    PostMortemOptions options;
    options.size_limit_bytes = 512ull * 1024 * 1024;
    EnablePostMortemDumps(options);
    char line[64];
    const int n = FormatTo(line, sizeof(line), "PID=%d\n", static_cast<int>(::getpid()));
    os::Write(1, line, static_cast<usize>(n));
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("The newest core of a crashed program is found by script, checked against the binary, and opens in gdb", "[postmortem][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
#if defined(CORE_TEST_UNDER_SANITIZER)
    SKIP("a sanitizer build's core is huge; this check runs in the plain flavor");
#endif
    int status = 0;
    if (!RunShell("command -v gdb", &status).View().Contains("gdb")) {
        SKIP("gdb is not installed; this check opens the core with it");
    }
    char exe[4096];
    const ssize_t length = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    REQUIRE(length > 0);
    exe[length] = '\0';

    ChildOptions options;
    options.disable_core_dumps = false;
    options.timeout_ms = 60000;
    const ChildResult r = RunInChild("dt_post_mortem_crash", "", options);
    CHECK(r.terminated);
    const StringView out = debug_test::StdoutView(r);
    REQUIRE(out.Contains("PID="));
    const Text pid = engine_test::MakeText(debug_test::ValueAfter(out, "PID="));

    // Whether this machine can be expected to keep the core: if not, skip with the reason; if it can, a missing core is a failure.
    struct rlimit hard;
    REQUIRE(::getrlimit(RLIMIT_CORE, &hard) == 0);
    if (hard.rlim_max == 0) {
        SKIP("the hard core size limit is 0 for this test program (ulimit -Hc), so the child cannot write a core");
    }
    const Text pattern = RunShell("cat /proc/sys/kernel/core_pattern");
    if (pattern.View().StartsWith("|") && !pattern.View().Contains("systemd-coredump")) {
        SKIP("core_pattern hands cores to a program other than systemd-coredump; find-core.sh cannot fetch them: " << pattern.CStr());
    }

    // Where scripts/ is: the source tree, passed by the build. The core is copied into a private directory of ours.
    const StringView scripts = ENGINE_SOURCE_DIR "/scripts";
    char directory[] = "/tmp/engine-core-test-XXXXXX";
    REQUIRE(mkdtemp(directory) != nullptr);
    struct Cleanup {
        const char* directory;
        ~Cleanup() { RunShell(Cat("rm -rf '", directory, "'").View()); }
    } cleanup{directory};
    const Text output_file = Cat(directory, "/crash.core");
    const Text find = Cat(scripts, "/find-core.sh --pid ", pid, " --wait 90 --output ", output_file, " '", exe, "'");
    const Text found = RunShell(find.View(), &status);
    INFO(found.CStr());
    REQUIRE(status == 0);
    CHECK(found.View().Contains(output_file.View()));

    const Text opened = RunShell(Cat(scripts, "/open-core.sh '", exe, "' '", output_file, "'").View(), &status);
    INFO(opened.CStr());
    CHECK(opened.View().Contains("InjectFault"));
    CHECK(opened.View().Contains("SIGSEGV"));

    // The same core offered with another program: the build ids differ and the script says so instead of opening it.
    const Text other = RunShell(Cat(scripts, "/find-core.sh --pid ", pid, " /bin/true").View(), &status);
    CHECK(WEXITSTATUS(status) == 3);
    CHECK(other.View().Contains("different build"));
}

TEST_CASE("find-core.sh explains its usage when the program does not exist", "[postmortem][death][debug][linux]") {
    // A path that is not an executable at all is a usage error; no core is needed for that.
    int status = 0;
    const Text out = RunShell(ENGINE_SOURCE_DIR "/scripts/find-core.sh /nonexistent/program", &status);
    CHECK(WEXITSTATUS(status) == 64);
    CHECK(out.View().Contains("usage"));
}
