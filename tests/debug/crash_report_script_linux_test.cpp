// scripts/crash-report.sh, scripts/store-symbols.sh and scripts/split-debug.sh on a dump from a crashing child: the trace names the
// faulting function and the threads, a stripped program resolves through a symbol store, a wrong program is refused, and a
// machine without a debugger gets an instruction.
#include <catch2/catch_test_macros.hpp>

#include "debug/death_test.h"
#include "support/death_test_skip.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <ftw.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace engine_placeholder;

namespace {

struct ScriptRun {
    std::string output;
    int exit_code = -1;
};

ScriptRun RunScriptCommand(const std::string& command) {
    ScriptRun run;
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        return run;
    }
    char buffer[4096];
    usize n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        run.output.append(buffer, n);
    }
    const int status = pclose(pipe);
    run.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return run;
}

class ScriptTempDir {
public:
    ScriptTempDir() {
        std::snprintf(path_, sizeof(path_), "/tmp/engine-script-XXXXXX");
        if (mkdtemp(path_) == nullptr) {
            path_[0] = '\0';
        }
    }
    ~ScriptTempDir() {
        if (path_[0] != '\0') {
            nftw(path_, &Remove, 16, FTW_DEPTH | FTW_PHYS);
        }
    }
    ScriptTempDir(const ScriptTempDir&) = delete;
    ScriptTempDir& operator=(const ScriptTempDir&) = delete;
    std::string Path() const { return path_; }

private:
    static int Remove(const char* entry, const struct stat*, int, struct FTW*) { return ::remove(entry); }
    char path_[64];
};

std::string ScriptsDir() {
    return std::string(ENGINE_SOURCE_DIR) + "/scripts";
}

std::string ThisExecutable() {
    char exe[4096];
    const ssize_t length = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    return length > 0 ? std::string(exe, static_cast<usize>(length)) : std::string();
}

// Finds the first .envelope under `directory`.
std::string FindEnvelopeIn(const std::string& directory) {
    const ScriptRun run = RunScriptCommand("find '" + directory + "' -name '*.envelope' | head -n 1");
    std::string path = run.output;
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
        path.pop_back();
    }
    return path;
}

bool HaveTool(const char* name) {
    return RunScriptCommand(std::string("command -v ") + name).exit_code == 0;
}

// Runs the crash child (the test program itself, or a copy of it) by hand, so the dump records that program as the crashed one.
std::string CrashProgram(const std::string& program, const std::string& dump_directory) {
    return RunScriptCommand("ENGINE_DEATH_TEST_CHILD=dt_e2e_worker_fault ENGINE_DEATH_TEST_ARG='" + dump_directory + "' '" + program +
                     "' '[.death-child]' --colour-mode none")
        .output;
}

// True if the program has debug information (a Release build does not, and then there is nothing to put in a symbol store).
bool HasDebugInfo(const std::string& program) {
    return RunScriptCommand("readelf -S '" + program + "' | grep -q '\\.debug_info'").exit_code == 0;
}

std::string CrashReportCommand(const std::string& dump, const std::string& more) {
    return "ENGINE_CRASH_EXTRACT='" ENGINE_CRASH_EXTRACT_PATH "' " + ScriptsDir() + "/crash-report.sh '" + dump + "' " + more;
}

} // namespace

TEST_CASE("crash-report.sh turns an envelope into a trace naming the faulting function and every thread", "[crash][scripts][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    if (!HaveTool("lldb")) {
        SKIP("lldb is not installed; crash-report.sh reads the dump with it");
    }
    ScriptTempDir dir;
    const std::string child_output = CrashProgram(ThisExecutable(), dir.Path());
    const std::string envelope = FindEnvelopeIn(dir.Path());
    INFO("child output: " << child_output);
    REQUIRE_FALSE(envelope.empty());
    const ScriptRun run = RunScriptCommand(CrashReportCommand(envelope, "'" + ThisExecutable() + "'"));
    INFO(run.output);
    CHECK(run.exit_code == 0);
    // A frame prints as a call (`InjectFault(kind=...)`, or `InjectFault(kind=<unavailable>)` when optimized); a path or a module
    // name elsewhere in the output would not have the parenthesis.
    CHECK(run.output.find("InjectFault(") != std::string::npos);
    CHECK(run.output.find("stop reason = signal SIGSEGV") != std::string::npos);
    CHECK(run.output.find("thread #1") != std::string::npos);
    CHECK(run.output.find("thread #2") != std::string::npos);
    CHECK(run.output.find("image list") != std::string::npos);
}

TEST_CASE("A stripped program resolves through a symbol store, and not without one", "[crash][scripts][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    if (!HaveTool("lldb") || !HaveTool("objcopy") || !HaveTool("strip")) {
        SKIP("lldb, objcopy or strip is missing; this check splits and strips a program and reads the dump with lldb");
    }
    if (!HasDebugInfo(ThisExecutable())) {
        SKIP("this build has no debug information to put in a symbol store (it is a Release build without -g)");
    }
    ScriptTempDir dir;
    const std::string work = dir.Path();
    // The program as built, with symbols, goes in the store; a copy is split and stripped, like a release build.
    const ScriptRun stored = RunScriptCommand(ScriptsDir() + "/store-symbols.sh '" + ThisExecutable() + "' '" + work + "/symbols'");
    INFO(stored.output);
    REQUIRE(stored.exit_code == 0);
    REQUIRE(RunScriptCommand("cp '" + ThisExecutable() + "' '" + work + "/prog'").exit_code == 0);
    REQUIRE(RunScriptCommand(ScriptsDir() + "/split-debug.sh '" + work + "/prog'").exit_code == 0);
    // split-debug leaves prog.debug beside the program, which the debugger would find by itself; remove it so only the store can help.
    REQUIRE(RunScriptCommand("rm '" + work + "/prog.debug'").exit_code == 0);
    // The stripped copy really has no function names.
    CHECK(RunScriptCommand("nm '" + work + "/prog' 2>&1 | grep -c InjectFault").output == "0\n");

    const std::string child_output = CrashProgram(work + "/prog", work + "/dumps");
    const std::string envelope = FindEnvelopeIn(work + "/dumps");
    INFO("child output: " << child_output);
    REQUIRE_FALSE(envelope.empty());

    const ScriptRun without = RunScriptCommand(CrashReportCommand(envelope, "'" + work + "/prog'"));
    INFO(without.output);
    CHECK(without.exit_code == 0);
    CHECK(without.output.find("InjectFault") == std::string::npos);
    CHECK(without.output.find("thread #2") != std::string::npos); // the dump is read; it just has no names

    const ScriptRun with = RunScriptCommand(CrashReportCommand(envelope, "'" + work + "/prog' '" + work + "/symbols'"));
    INFO(with.output);
    CHECK(with.exit_code == 0);
    CHECK(with.output.find("InjectFault(") != std::string::npos);
}

TEST_CASE("crash-report.sh refuses a program that is not the one that crashed", "[crash][scripts][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    if (!HaveTool("lldb")) {
        SKIP("lldb is not installed; crash-report.sh reads the dump with it");
    }
    ScriptTempDir dir;
    const std::string child_output = CrashProgram(ThisExecutable(), dir.Path());
    const std::string envelope = FindEnvelopeIn(dir.Path());
    INFO("child output: " << child_output);
    REQUIRE_FALSE(envelope.empty());
    const ScriptRun run = RunScriptCommand(CrashReportCommand(envelope, "/bin/true"));
    INFO(run.output);
    CHECK(run.exit_code == 3);
    CHECK(run.output.find("different build") != std::string::npos);
}

TEST_CASE("Without lldb or minidump-stackwalk on the PATH crash-report.sh says what to install", "[crash][scripts][debug][linux]") {
    ScriptTempDir dir;
    // Any file will do: the tools are looked for before the file is read.
    const ScriptRun run = RunScriptCommand("PATH=/nonexistent /bin/bash " + ScriptsDir() + "/crash-report.sh /bin/true");
    INFO(run.output);
    CHECK(run.exit_code == 66);
    CHECK(run.output.find("Install lldb") != std::string::npos);
    CHECK(run.output.find("minidump-stackwalk") != std::string::npos);
}

TEST_CASE("store-symbols.sh refuses a program with no debug information", "[crash][scripts][debug][linux]") {
    ScriptTempDir dir;
    const ScriptRun run = RunScriptCommand(ScriptsDir() + "/store-symbols.sh /bin/true '" + dir.Path() + "/symbols'");
    INFO(run.output);
    CHECK(run.exit_code == 2);
    CHECK(run.output.find("no debug information") != std::string::npos);
}

TEST_CASE("The symbol store survives being moved, and split-debug.sh does not overwrite an existing debug file", "[crash][scripts][debug][linux]") {
    if (!HaveTool("objcopy") || !HaveTool("strip")) {
        SKIP("objcopy or strip is missing");
    }
    if (!HasDebugInfo(ThisExecutable())) {
        SKIP("this build has no debug information to put in a symbol store (it is a Release build without -g)");
    }
    ScriptTempDir dir;
    const std::string work = dir.Path();
    REQUIRE(RunScriptCommand(ScriptsDir() + "/store-symbols.sh '" + ThisExecutable() + "' '" + work + "/symbols'").exit_code == 0);
    // Move the store: the by-build-id link must still reach the file.
    REQUIRE(RunScriptCommand("mv '" + work + "/symbols' '" + work + "/moved'").exit_code == 0);
    CHECK(RunScriptCommand("for f in '" + work + "/moved'/.build-id/*/*.debug; do test -s \"$f\" || exit 1; done").exit_code == 0);

    // Splitting twice keeps the first, real debug file.
    REQUIRE(RunScriptCommand("cp '" + ThisExecutable() + "' '" + work + "/prog'").exit_code == 0);
    REQUIRE(RunScriptCommand(ScriptsDir() + "/split-debug.sh '" + work + "/prog'").exit_code == 0);
    const ScriptRun size_before = RunScriptCommand("stat -c %s '" + work + "/prog.debug'");
    const ScriptRun again = RunScriptCommand(ScriptsDir() + "/split-debug.sh '" + work + "/prog'");
    CHECK(again.exit_code == 0);
    CHECK(again.output.find("nothing done") != std::string::npos);
    CHECK(RunScriptCommand("stat -c %s '" + work + "/prog.debug'").output == size_before.output);
}

TEST_CASE("crash-report.sh exits with an error, not a trace, for a file lldb cannot read as a dump", "[crash][scripts][debug][linux]") {
    if (!HaveTool("lldb")) {
        SKIP("lldb is not installed; crash-report.sh reads the dump with it");
    }
    ScriptTempDir dir;
    const std::string bad = dir.Path() + "/bad.dmp";
    REQUIRE(RunScriptCommand("printf 'MDMPnot really a minidump' > '" + bad + "'").exit_code == 0);
    const ScriptRun run = RunScriptCommand(ScriptsDir() + "/crash-report.sh '" + bad + "'");
    INFO(run.output);
    CHECK(run.exit_code == 2);
    CHECK(run.output.find("could not read") != std::string::npos);
}
