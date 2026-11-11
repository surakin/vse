// scripts/crash-report.sh, scripts/store-symbols.sh and scripts/split-debug.sh on a dump from a crashing child: the trace names the
// faulting function and the threads, a stripped program resolves through a symbol store, a wrong program is refused, and a
// machine without a debugger gets an instruction.
#include <catch2/catch_test_macros.hpp>

#include "debug/death_test.h"
#include "support/death_test_skip.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cstr/format.h"
#include "support/test_support_posix.h"

using namespace engine_placeholder;
using debug_test::Cat;
using debug_test::Text;

namespace {

struct ScriptRun {
    Text output = engine_test::MakeText();
    int exit_code = -1;
};

// The pieces joined into one command, run with standard error merged into its output.
template <typename... Parts>
ScriptRun RunScriptCommand(const Parts&... command) {
    ScriptRun run;
    debug_test::ShellRun shell = debug_test::RunShell(Cat(command..., " 2>&1").View());
    run.output = static_cast<Text&&>(shell.output);
    run.exit_code = shell.ExitCode();
    return run;
}

class ScriptTempDir {
public:
    ScriptTempDir() {
        FormatTo(path_, sizeof(path_), "/tmp/engine-script-XXXXXX");
        if (mkdtemp(path_) == nullptr) {
            path_[0] = '\0';
        }
    }
    ~ScriptTempDir() {
        if (path_[0] != '\0') {
            debug_test::RemoveTree(path_);
        }
    }
    ScriptTempDir(const ScriptTempDir&) = delete;
    ScriptTempDir& operator=(const ScriptTempDir&) = delete;
    Text Path() const { return engine_test::MakeText(path_); }

private:
    char path_[64];
};

Text ScriptsDir() {
    return engine_test::MakeText(ENGINE_SOURCE_DIR "/scripts");
}

Text ThisExecutable() {
    char exe[4096];
    const ssize_t length = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    return length > 0 ? engine_test::MakeText(StringView(exe, static_cast<usize>(length))) : engine_test::MakeText();
}

// Finds the first .envelope under `directory`.
Text FindEnvelopeIn(const Text& directory) {
    const ScriptRun run = RunScriptCommand("find '", directory, "' -name '*.envelope' | head -n 1");
    StringView path = run.output.View();
    while (!path.Empty() && (path[path.Length() - 1] == '\n' || path[path.Length() - 1] == '\r')) {
        path = path.Substring(0, path.Length() - 1);
    }
    return engine_test::MakeText(path);
}

bool HaveTool(const char* name) {
    return RunScriptCommand("command -v ", name).exit_code == 0;
}

// Runs the crash child (the test program itself, or a copy of it) by hand, so the dump records that program as the crashed one.
Text CrashProgram(const Text& program, const Text& dump_directory) {
    ScriptRun run = RunScriptCommand("ENGINE_DEATH_TEST_CHILD=dt_e2e_worker_fault ENGINE_DEATH_TEST_ARG='", dump_directory, "' '", program,
                                     "' '[.death-child]' --colour-mode none");
    return static_cast<Text&&>(run.output);
}

// True if the program has debug information (a Release build does not, and then there is nothing to put in a symbol store).
bool HasDebugInfo(const Text& program) {
    return RunScriptCommand("readelf -S '", program, "' | grep -q '\\.debug_info'").exit_code == 0;
}

template <typename... Parts>
Text CrashReportCommand(const Text& dump, const Parts&... more) {
    return Cat("ENGINE_CRASH_EXTRACT='" ENGINE_CRASH_EXTRACT_PATH "' ", ScriptsDir(), "/crash-report.sh '", dump, "' ", more...);
}

} // namespace

TEST_CASE("crash-report.sh turns an envelope into a trace naming the faulting function and every thread", "[crash][scripts][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    if (!HaveTool("lldb")) {
        SKIP("lldb is not installed; crash-report.sh reads the dump with it");
    }
    ScriptTempDir dir;
    const Text child_output = CrashProgram(ThisExecutable(), dir.Path());
    const Text envelope = FindEnvelopeIn(dir.Path());
    INFO("child output: " << child_output.CStr());
    REQUIRE(envelope.Length() != 0);
    const ScriptRun run = RunScriptCommand(CrashReportCommand(envelope, "'", ThisExecutable(), "'"));
    INFO(run.output.CStr());
    CHECK(run.exit_code == 0);
    // A frame prints as a call (`InjectFault(kind=...)`, or `InjectFault(kind=<unavailable>)` when optimized); a path or a module
    // name elsewhere in the output would not have the parenthesis.
    CHECK(run.output.View().Contains("InjectFault("));
    CHECK(run.output.View().Contains("stop reason = signal SIGSEGV"));
    CHECK(run.output.View().Contains("thread #1"));
    CHECK(run.output.View().Contains("thread #2"));
    CHECK(run.output.View().Contains("image list"));
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
    const Text work = dir.Path();
    // The program as built, with symbols, goes in the store; a copy is split and stripped, like a release build.
    const ScriptRun stored = RunScriptCommand(ScriptsDir(), "/store-symbols.sh '", ThisExecutable(), "' '", work, "/symbols'");
    INFO(stored.output.CStr());
    REQUIRE(stored.exit_code == 0);
    REQUIRE(RunScriptCommand("cp '", ThisExecutable(), "' '", work, "/prog'").exit_code == 0);
    REQUIRE(RunScriptCommand(ScriptsDir(), "/split-debug.sh '", work, "/prog'").exit_code == 0);
    // split-debug leaves prog.debug beside the program, which the debugger would find by itself; remove it so only the store can help.
    REQUIRE(RunScriptCommand("rm '", work, "/prog.debug'").exit_code == 0);
    // The stripped copy really has no function names.
    CHECK(RunScriptCommand("nm '", work, "/prog' 2>&1 | grep -c InjectFault").output == "0\n");

    const Text child_output = CrashProgram(Cat(work, "/prog"), Cat(work, "/dumps"));
    const Text envelope = FindEnvelopeIn(Cat(work, "/dumps"));
    INFO("child output: " << child_output.CStr());
    REQUIRE(envelope.Length() != 0);

    const ScriptRun without = RunScriptCommand(CrashReportCommand(envelope, "'", work, "/prog'"));
    INFO(without.output.CStr());
    CHECK(without.exit_code == 0);
    CHECK_FALSE(without.output.View().Contains("InjectFault"));
    CHECK(without.output.View().Contains("thread #2")); // the dump is read; it just has no names

    const ScriptRun with = RunScriptCommand(CrashReportCommand(envelope, "'", work, "/prog' '", work, "/symbols'"));
    INFO(with.output.CStr());
    CHECK(with.exit_code == 0);
    CHECK(with.output.View().Contains("InjectFault("));
}

TEST_CASE("crash-report.sh refuses a program that is not the one that crashed", "[crash][scripts][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    if (!HaveTool("lldb")) {
        SKIP("lldb is not installed; crash-report.sh reads the dump with it");
    }
    ScriptTempDir dir;
    const Text child_output = CrashProgram(ThisExecutable(), dir.Path());
    const Text envelope = FindEnvelopeIn(dir.Path());
    INFO("child output: " << child_output.CStr());
    REQUIRE(envelope.Length() != 0);
    const ScriptRun run = RunScriptCommand(CrashReportCommand(envelope, "/bin/true"));
    INFO(run.output.CStr());
    CHECK(run.exit_code == 3);
    CHECK(run.output.View().Contains("different build"));
}

TEST_CASE("Without lldb or minidump-stackwalk on the PATH crash-report.sh says what to install", "[crash][scripts][debug][linux]") {
    ScriptTempDir dir;
    // Any file will do: the tools are looked for before the file is read.
    const ScriptRun run = RunScriptCommand("PATH=/nonexistent /bin/bash ", ScriptsDir(), "/crash-report.sh /bin/true");
    INFO(run.output.CStr());
    CHECK(run.exit_code == 66);
    CHECK(run.output.View().Contains("Install lldb"));
    CHECK(run.output.View().Contains("minidump-stackwalk"));
}

TEST_CASE("store-symbols.sh refuses a program with no debug information", "[crash][scripts][debug][linux]") {
    ScriptTempDir dir;
    const ScriptRun run = RunScriptCommand(ScriptsDir(), "/store-symbols.sh /bin/true '", dir.Path(), "/symbols'");
    INFO(run.output.CStr());
    CHECK(run.exit_code == 2);
    CHECK(run.output.View().Contains("no debug information"));
}

TEST_CASE("The symbol store survives being moved, and split-debug.sh does not overwrite an existing debug file", "[crash][scripts][debug][linux]") {
    if (!HaveTool("objcopy") || !HaveTool("strip")) {
        SKIP("objcopy or strip is missing");
    }
    if (!HasDebugInfo(ThisExecutable())) {
        SKIP("this build has no debug information to put in a symbol store (it is a Release build without -g)");
    }
    ScriptTempDir dir;
    const Text work = dir.Path();
    REQUIRE(RunScriptCommand(ScriptsDir(), "/store-symbols.sh '", ThisExecutable(), "' '", work, "/symbols'").exit_code == 0);
    // Move the store: the by-build-id link must still reach the file.
    REQUIRE(RunScriptCommand("mv '", work, "/symbols' '", work, "/moved'").exit_code == 0);
    CHECK(RunScriptCommand("for f in '", work, "/moved'/.build-id/*/*.debug; do test -s \"$f\" || exit 1; done").exit_code == 0);

    // Splitting twice keeps the first, real debug file.
    REQUIRE(RunScriptCommand("cp '", ThisExecutable(), "' '", work, "/prog'").exit_code == 0);
    REQUIRE(RunScriptCommand(ScriptsDir(), "/split-debug.sh '", work, "/prog'").exit_code == 0);
    const ScriptRun size_before = RunScriptCommand("stat -c %s '", work, "/prog.debug'");
    const ScriptRun again = RunScriptCommand(ScriptsDir(), "/split-debug.sh '", work, "/prog'");
    CHECK(again.exit_code == 0);
    CHECK(again.output.View().Contains("nothing done"));
    CHECK(RunScriptCommand("stat -c %s '", work, "/prog.debug'").output == size_before.output);
}

TEST_CASE("crash-report.sh exits with an error, not a trace, for a file lldb cannot read as a dump", "[crash][scripts][debug][linux]") {
    if (!HaveTool("lldb")) {
        SKIP("lldb is not installed; crash-report.sh reads the dump with it");
    }
    ScriptTempDir dir;
    const Text bad = Cat(dir.Path(), "/bad.dmp");
    REQUIRE(RunScriptCommand("printf 'MDMPnot really a minidump' > '", bad, "'").exit_code == 0);
    const ScriptRun run = RunScriptCommand(ScriptsDir(), "/crash-report.sh '", bad, "'");
    INFO(run.output.CStr());
    CHECK(run.exit_code == 2);
    CHECK(run.output.View().Contains("could not read"));
}
