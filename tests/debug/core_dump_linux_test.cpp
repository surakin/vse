// Core dumps on Linux: the description of core_pattern, a hard limit of zero, and a real core found with scripts/find-core.sh
// and opened with gdb.
#include <catch2/catch_test_macros.hpp>

#include "debug/core_dump_linux.h"
#include "debug/death_test.h"
#include "debug/fault_injection.h"
#include "debug/post_mortem.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/resource.h>
#include <unistd.h>
#include "build_defs.h"
#include "cstr/cstr.h"
#include "cstr/format.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

#if ENGINE_SANITIZED
#define CORE_TEST_UNDER_SANITIZER 1
#endif

namespace {

std::string Describe(const char* pattern) {
    char text[512];
    DescribeCorePattern(pattern, text, sizeof(text));
    return text;
}

std::string RunShell(const std::string& command, int* status = nullptr) {
    std::string output;
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        return output;
    }
    char buffer[4096];
    usize n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        output.append(buffer, n);
    }
    const int code = pclose(pipe);
    if (status != nullptr) {
        *status = code;
    }
    return output;
}

} // namespace

TEST_CASE("A piped core pattern from systemd-coredump points at coredumpctl", "[postmortem][debug][linux]") {
    const std::string text = Describe("|/usr/lib/systemd/systemd-coredump %P %u %g %s %t %c %h");
    CHECK(text.find("systemd-coredump") != std::string::npos);
    CHECK(text.find("coredumpctl") != std::string::npos);
    CHECK(CorePatternIsPipe("|/usr/lib/systemd/systemd-coredump"));
}

TEST_CASE("Another piped core pattern names the program, and a file pattern names the pattern", "[postmortem][debug][linux]") {
    const std::string piped = Describe("|/usr/local/bin/my-collector %p");
    CHECK(piped.find("/usr/local/bin/my-collector %p") != std::string::npos);
    CHECK(piped.find("coredumpctl") == std::string::npos);
    CHECK(CorePatternIsPipe("|/usr/local/bin/my-collector"));

    const std::string file = Describe("core.%e.%p");
    CHECK(file.find("'core.%e.%p'") != std::string::npos);
    CHECK(file.find("working directory") != std::string::npos);
    CHECK_FALSE(CorePatternIsPipe("core.%e.%p"));
}

TEST_CASE("An empty or missing core pattern is reported as unknown", "[postmortem][debug][linux]") {
    CHECK(Describe("").find("unknown") != std::string::npos);
    CHECK(Describe(nullptr).find("unknown") != std::string::npos);
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
    if (RunShell("command -v gdb", &status).find("gdb") == std::string::npos) {
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
    const std::string out(r.stdout_text, r.stdout_length);
    const usize at = out.find("PID=");
    REQUIRE(at != std::string::npos);
    const std::string pid = out.substr(at + 4, out.find('\n', at) - at - 4);

    // Whether this machine can be expected to keep the core: if not, skip with the reason; if it can, a missing core is a failure.
    struct rlimit hard;
    REQUIRE(::getrlimit(RLIMIT_CORE, &hard) == 0);
    if (hard.rlim_max == 0) {
        SKIP("the hard core size limit is 0 for this test program (ulimit -Hc), so the child cannot write a core");
    }
    const std::string pattern = RunShell("cat /proc/sys/kernel/core_pattern");
    if (pattern.rfind("|", 0) == 0 && pattern.find("systemd-coredump") == std::string::npos) {
        SKIP("core_pattern hands cores to a program other than systemd-coredump; find-core.sh cannot fetch them: " + pattern);
    }

    // Where scripts/ is: the source tree, passed by the build. The core is copied into a private directory of ours.
    const std::string scripts = ENGINE_SOURCE_DIR "/scripts";
    char directory[] = "/tmp/engine-core-test-XXXXXX";
    REQUIRE(mkdtemp(directory) != nullptr);
    struct Cleanup {
        const char* directory;
        ~Cleanup() { RunShell(std::string("rm -rf '") + directory + "'"); }
    } cleanup{directory};
    const std::string output_file = std::string(directory) + "/crash.core";
    const std::string find = scripts + "/find-core.sh --pid " + pid + " --wait 90 --output " + output_file + " '" + exe + "'";
    const std::string found = RunShell(find, &status);
    INFO(found);
    REQUIRE(status == 0);
    CHECK(found.find(output_file) != std::string::npos);

    const std::string opened = RunShell(scripts + "/open-core.sh '" + exe + "' '" + output_file + "'", &status);
    INFO(opened);
    CHECK(opened.find("InjectFault") != std::string::npos);
    CHECK(opened.find("SIGSEGV") != std::string::npos);

    // The same core offered with another program: the build ids differ and the script says so instead of opening it.
    const std::string other = RunShell(scripts + "/find-core.sh --pid " + pid + " /bin/true", &status);
    CHECK(WEXITSTATUS(status) == 3);
    CHECK(other.find("different build") != std::string::npos);
}

TEST_CASE("find-core.sh explains its usage when the program does not exist", "[postmortem][death][debug][linux]") {
    // A path that is not an executable at all is a usage error; no core is needed for that.
    int status = 0;
    const std::string out = RunShell(std::string(ENGINE_SOURCE_DIR) + "/scripts/find-core.sh /nonexistent/program", &status);
    CHECK(WEXITSTATUS(status) == 64);
    CHECK(out.find("usage") != std::string::npos);
}
