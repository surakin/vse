// The crash and core scripts on macOS (Phase 21 Day 292): scripts/crash-report.sh on a dump from a crashing child (the
// trace names the faulting function and every thread, cross-checked with atos), a stripped program resolving through a
// dSYM symbol store (scripts/store-symbols.sh, split-debug.sh) and not without one, a wrong program refused; and
// find-core.sh and open-core.sh on a core lldb saves (the kernel's /cores is root's on a stock Mac). Every script runs
// under /bin/bash, macOS's bash 3.2, in a directory whose name has a space (as ~/Library/Application Support does).
#include <catch2/catch_test_macros.hpp>

#include "debug/death_test.h"
#include "support/death_test_skip.h"

#include <mach-o/dyld.h>
#include <unistd.h>
#include "build_defs.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "envelope.h"
#include "minidump.h"
#include "cstr/mem_ops.h"
#include "os/console.h"
#include "support/test_support_posix.h"

using namespace vse;
using debug_test::Cat;
using debug_test::Text;

namespace
{

struct ScriptRun
{
    Text output = engine_test::MakeText();
    int exit_code = -1;
};

// The pieces joined into one command, run with standard error merged into its output.
template <typename... Parts> ScriptRun Run(const Parts&... command)
{
    ScriptRun run;
    debug_test::ShellRun shell = debug_test::RunShell(Cat(command..., " 2>&1").View());
    run.output = static_cast<Text&&>(shell.output);
    run.exit_code = shell.ExitCode();
    return run;
}

// A private directory under TMPDIR whose name has a space in it.
class SpacedTempDir
{
public:
    SpacedTempDir()
    {
        const char* tmp = os::GetEnvironment("TMPDIR");
        FormatTo(path_, sizeof(path_), "%s/engine scripts XXXXXX", tmp != nullptr ? tmp : "/tmp");
        if (::mkdtemp(path_) == nullptr)
        {
            path_[0] = '\0';
        }
    }
    ~SpacedTempDir()
    {
        if (path_[0] != '\0')
        {
            debug_test::RemoveTree(path_);
        }
    }
    SpacedTempDir(const SpacedTempDir&) = delete;
    SpacedTempDir& operator=(const SpacedTempDir&) = delete;
    Text Path() const { return engine_test::MakeText(path_); }

private:
    char path_[512];
};

Text Script(const char* name)
{
    return Cat("/bin/bash '" ENGINE_SOURCE_DIR "/scripts/", name, "'");
}

Text ThisExecutable()
{
    char exe[4096];
    u32 size = sizeof(exe);
    return _NSGetExecutablePath(exe, &size) == 0 ? engine_test::MakeText(exe) : engine_test::MakeText();
}

Text Trimmed(const Text& text)
{
    StringView view = text.View();
    while (!view.Empty() && (view[view.Length() - 1] == '\n' || view[view.Length() - 1] == ' '))
    {
        view = view.Substring(0, view.Length() - 1);
    }
    return engine_test::MakeText(view);
}

bool HaveTool(const char* name)
{
    return Run("command -v ", name).exit_code == 0;
}

// Runs the crash child (the test program itself, or a copy of it) by hand, so the dump records that program as the
// crashed one; returns the envelope it left.
Text CrashProgram(const Text& program, const Text& dump_directory)
{
    Run("ENGINE_DEATH_TEST_CHILD=dt_mac_worker_fault ENGINE_DEATH_TEST_ARG='", dump_directory, "' '", program,
        "' '[.death-child]' --colour-mode none");
    return Trimmed(Run("find '", dump_directory, "' -name '*.envelope' | head -n 1").output);
}

template <typename... Parts> Text CrashReport(const Text& dump, const Parts&... more)
{
    return Cat("ENGINE_CRASH_EXTRACT='" ENGINE_CRASH_EXTRACT_PATH "' ", Script("crash-report.sh"), " '", dump, "' ",
               more...);
}

// The value after `key` up to the end of its line, in `text`.
Text LineValue(StringView text, StringView key)
{
    return engine_test::MakeText(debug_test::ValueAfter(text, key));
}

}  // namespace

TEST_CASE("macOS: a crash's minidump lists the program with its LC_UUID, and its registers become readable to lldb",
          "[crash][scripts][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    SpacedTempDir dir;
    const Text envelope = CrashProgram(ThisExecutable(), dir.Path());
    REQUIRE(envelope.Length() != 0);
    const Text content = debug_test::ReadWholeFile(envelope.View());
    crash_extract::Minidump dump;
    REQUIRE(crash_extract::FindMinidump(content.CStr(), content.Length(), &dump) == crash_extract::EnvelopeResult::Ok);
    static crash_extract::DumpModule modules[512];
    usize count = 0;
    REQUIRE(crash_extract::ReadDumpModules(dump.data, dump.size, modules, 512, &count) ==
            crash_extract::DumpResult::Ok);
    REQUIRE(count > 1);
    // The program is the first module; its id is the UUID the crash report printed (`build id:`), then the age, 0.
    CHECK(CStrCompare(modules[0].name, ThisExecutable().CStr()) == 0);
    const Text report = debug_test::ReadWholeFile(Cat(dir.Path(), "/crash-report.txt").View());
    const Text build_id = LineValue(report.View(), "build id: ");
    INFO("report: " << report.CStr());
    REQUIRE(build_id.Length() == 32);
    CHECK(Cat(build_id, "0") == modules[0].build_id);
    CHECK(modules[0].base % 4096 == 0);
    crash_extract::DumpException exception;
    REQUIRE(crash_extract::ReadDumpException(dump.data, dump.size, &exception) == crash_extract::DumpResult::Ok);
    CHECK(exception.present);
    CHECK(exception.code == 1u);  // EXC_BAD_ACCESS
    // The register flags: the crashed thread's context, every other thread's and the exception's are completed.
    Vector<u8> copy;
    REQUIRE(copy.TryResize(dump.size));
    MemCopy(copy.Data(), dump.data, dump.size);
    usize completed = 0;
    REQUIRE(crash_extract::CompleteMacContextFlags(reinterpret_cast<char*>(copy.Data()), copy.Size(), &completed) ==
            crash_extract::DumpResult::Ok);
#if ENGINE_ARCH_X64
    CHECK(completed >= 3);  // the main thread, the worker that faulted, and the exception's copy of its context
#endif
    usize again = 0;
    REQUIRE(crash_extract::CompleteMacContextFlags(reinterpret_cast<char*>(copy.Data()), copy.Size(), &again) ==
            crash_extract::DumpResult::Ok);
    CHECK(again == 0);  // done once
}

TEST_CASE("macOS: crash-report.sh turns an envelope into a trace naming the faulting function and every thread",
          "[crash][scripts][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    if (!HaveTool("lldb"))
    {
        SKIP("lldb is not installed; crash-report.sh reads the dump with it (xcode-select --install)");
    }
    SpacedTempDir dir;
    const Text envelope = CrashProgram(ThisExecutable(), dir.Path());
    REQUIRE(envelope.Length() != 0);
    const ScriptRun run = Run(CrashReport(envelope, "'", ThisExecutable(), "'"));
    INFO(run.output.CStr());
    CHECK(run.exit_code == 0);
    CHECK(run.output.View().Contains("InjectFault("));
    CHECK(run.output.View().Contains("fault_injection_posix.cpp:"));
    CHECK(run.output.View().Contains("stop reason = EXC_BAD_ACCESS"));
    CHECK(run.output.View().Contains("thread #1"));
    CHECK(run.output.View().Contains("thread #2"));
    CHECK(run.output.View().Contains("image list"));

    // atos names the faulting frame's address the same way, from the program's dSYM and the load address the dump
    // records.
    if (!HaveTool("atos"))
    {
        return;
    }
    const StringView out = run.output.View();
    const usize frame = out.Find("* frame #0: 0x");
    REQUIRE(frame + 30 <= out.Length());
    const Text address = engine_test::MakeText(out.Substring(frame + 12, 18));
    const ScriptRun modules = Run("'" ENGINE_CRASH_EXTRACT_PATH "' --modules '", envelope, "' | sed -n 2p");
    const StringView module_line = modules.output.View();
    REQUIRE(module_line.StartsWith("module: 0x"));
    const Text base = engine_test::MakeText(module_line.Substring(8, 18));
    const ScriptRun atos = Run("atos -o '", ThisExecutable(), ".dSYM' -l ", base, " ", address);
    INFO("atos: " << atos.output.CStr());
    CHECK(atos.exit_code == 0);
    CHECK(atos.output.View().Contains("InjectFault("));
    CHECK(atos.output.View().Contains("fault_injection_posix.cpp:"));
}

TEST_CASE("macOS: a stripped program resolves file and line through a dSYM store, and not without one",
          "[crash][scripts][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    if (!HaveTool("lldb") || !HaveTool("dsymutil") || !HaveTool("dwarfdump"))
    {
        SKIP("lldb, dsymutil or dwarfdump is missing (xcode-select --install)");
    }
    SpacedTempDir dir;
    const Text work = dir.Path();
    const ScriptRun stored = Run(Script("store-symbols.sh"), " '", ThisExecutable(), "' '", work, "/symbols'");
    INFO(stored.output.CStr());
    REQUIRE(stored.exit_code == 0);
    // The store holds the bundle under the program's UUID, as dwarfdump prints it.
    const Text uuid = Trimmed(
        Run("dwarfdump --uuid '", ThisExecutable(), "' | sed -n 's/^UUID: \\([0-9A-F-]*\\) .*/\\1/p'").output);
    REQUIRE(uuid.Length() == 36);
    CHECK(stored.output.View().Contains(uuid.View()));
    const Text program = ThisExecutable();
    const StringView name = program.View().Substring(program.View().LastIndexOf('/') + 1);
    CHECK(Run("test -d '", work, "/symbols/", name, "/", uuid, "/", name, ".dSYM'").exit_code == 0);
    // A copy is split and stripped like a release build: split-debug.sh leaves prog.dSYM beside it, which lldb would
    // find by itself, so it is removed and only the store can help.
    REQUIRE(Run("cp '", ThisExecutable(), "' '", work, "/prog'").exit_code == 0);
    const ScriptRun split = Run(Script("split-debug.sh"), " '", work, "/prog'");
    INFO(split.output.CStr());
    REQUIRE(split.exit_code == 0);
    REQUIRE(Run("rm -rf '", work, "/prog.dSYM'").exit_code == 0);
    CHECK(Run("nm -ap '", work, "/prog' | grep -c ' OSO '").output == "0\n");  // no debug map left

    const Text envelope = CrashProgram(Cat(work, "/prog"), Cat(work, "/dumps"));
    REQUIRE(envelope.Length() != 0);

    const ScriptRun without = Run(CrashReport(envelope, "'", work, "/prog'"));
    INFO(without.output.CStr());
    CHECK(without.exit_code == 0);
    CHECK(without.output.View().Contains("InjectFault"));  // strip -S keeps the symbol table
    CHECK_FALSE(without.output.View().Contains("fault_injection_posix.cpp:"));
    CHECK(without.output.View().Contains("thread #2"));

    const ScriptRun with = Run(CrashReport(envelope, "'", work, "/prog' '", work, "/symbols'"));
    INFO(with.output.CStr());
    CHECK(with.exit_code == 0);
    CHECK(with.output.View().Contains("target symbols add"));
    CHECK(with.output.View().Contains("fault_injection_posix.cpp:"));
}

TEST_CASE("macOS: crash-report.sh refuses a program that is not the one that crashed",
          "[crash][scripts][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    if (!HaveTool("lldb"))
    {
        SKIP("lldb is not installed; crash-report.sh reads the dump with it (xcode-select --install)");
    }
    SpacedTempDir dir;
    const Text envelope = CrashProgram(ThisExecutable(), dir.Path());
    REQUIRE(envelope.Length() != 0);
    const ScriptRun run = Run(CrashReport(envelope, "'" UNIVERSAL_PROGRAM_PATH "'"));
    INFO(run.output.CStr());
    CHECK(run.exit_code == 3);
    CHECK(run.output.View().Contains("different build"));
}

TEST_CASE("macOS: a universal program's dSYM is stored once per architecture, each copy holding only its own",
          "[crash][scripts][debug][macos]")
{
    if (!HaveTool("dwarfdump") || !HaveTool("ditto"))
    {
        SKIP("dwarfdump or ditto is missing (xcode-select --install)");
    }
    const ScriptRun uuids = Run("dwarfdump --uuid '" UNIVERSAL_PROGRAM_PATH ".dSYM' | grep -c '^UUID: '");
    if (uuids.output != "2\n")
    {
        SKIP("the universal test program has no two-architecture dSYM in this build (a Release build without -g)");
    }
    SpacedTempDir dir;
    const ScriptRun stored = Run(Script("store-symbols.sh"), " '" UNIVERSAL_PROGRAM_PATH "' '", dir.Path(), "/sym'");
    INFO(stored.output.CStr());
    REQUIRE(stored.exit_code == 0);
    // Two directories, one per UUID, each bundle with one architecture whose UUID is its directory's name.
    const ScriptRun check = Run("cd '", dir.Path(),
                                "/sym/debug_universal_program' && n=0 && for d in */; do "
                                "u=\"${d%/}\"; l=\"$(dwarfdump --uuid \"$u/debug_universal_program.dSYM\")\"; "
                                "[ \"$(printf '%s\\n' \"$l\" | wc -l | tr -d ' ')\" = 1 ] || exit 1; "
                                "printf '%s' \"$l\" | grep -q \"^UUID: $u \" || exit 2; n=$((n+1)); done; echo $n");
    INFO(check.output.CStr());
    CHECK(check.exit_code == 0);
    CHECK(check.output == "2\n");
}

TEST_CASE("macOS: store-symbols.sh keeps the bundle already stored when a new copy fails",
          "[crash][scripts][debug][macos]")
{
    if (!HaveTool("dwarfdump") || !HaveTool("ditto"))
    {
        SKIP("dwarfdump or ditto is missing (xcode-select --install)");
    }
    if (Run("test -d '" UNIVERSAL_PROGRAM_PATH ".dSYM'").exit_code != 0)
    {
        SKIP("the universal test program has no dSYM in this build (a Release build without -g)");
    }
    SpacedTempDir dir;
    const Text work = dir.Path();
    REQUIRE(Run("ditto '" UNIVERSAL_PROGRAM_PATH ".dSYM' '", work, "/p.dSYM'").exit_code == 0);
    REQUIRE(Run(Script("store-symbols.sh"), " '", work, "/p.dSYM' '", work, "/sym'").exit_code == 0);
    // A bundle ditto cannot read in full (its Info.plist unreadable): the copy fails after the checks pass. The old
    // bundle used to be deleted first (review finding, Day 292).
    REQUIRE(Run("chmod 000 '", work, "/p.dSYM/Contents/Info.plist'").exit_code == 0);
    const ScriptRun failed = Run(Script("store-symbols.sh"), " '", work, "/p.dSYM' '", work, "/sym'");
    Run("chmod 644 '", work, "/p.dSYM/Contents/Info.plist'");
    INFO(failed.output.CStr());
    CHECK(failed.exit_code == 2);
    CHECK(failed.output.View().Contains("could not copy"));
    CHECK(
        Run("for d in '", work, "'/sym/p/*/p.dSYM; do test -s \"$d/Contents/Info.plist\" || exit 1; done").exit_code ==
        0);
    CHECK(Run("ls -A '", work, "'/sym/p/* | grep -c '\\.new'").output == "0\n");
}

TEST_CASE("macOS: store-symbols.sh refuses a program with no debug information, and split-debug.sh does not "
          "overwrite a dSYM",
          "[crash][scripts][debug][macos]")
{
    if (!HaveTool("dsymutil") || !HaveTool("nm") || !HaveTool("strip"))
    {
        SKIP("dsymutil, nm or strip is missing (xcode-select --install)");
    }
    SpacedTempDir dir;
    const Text work = dir.Path();
    const ScriptRun refused = Run(Script("store-symbols.sh"), " /bin/ls '", work, "/symbols'");
    INFO(refused.output.CStr());
    CHECK(refused.exit_code == 2);
    CHECK(refused.output.View().Contains("no debug information"));

    REQUIRE(Run("cp '", ThisExecutable(), "' '", work, "/prog'").exit_code == 0);
    REQUIRE(Run(Script("split-debug.sh"), " '", work, "/prog'").exit_code == 0);
    const ScriptRun size_before = Run("stat -f %z '", work, "/prog.dSYM/Contents/Resources/DWARF/prog'");
    CHECK(size_before.output != "0\n");
    const ScriptRun again = Run(Script("split-debug.sh"), " '", work, "/prog'");
    CHECK(again.exit_code == 0);
    CHECK(again.output.View().Contains("nothing done"));
    CHECK(Run("stat -f %z '", work, "/prog.dSYM/Contents/Resources/DWARF/prog'").output == size_before.output);
}

TEST_CASE("macOS: crash-report.sh exits with an error for a file lldb cannot read, and says what to install without "
          "lldb",
          "[crash][scripts][debug][macos]")
{
    SpacedTempDir dir;
    const ScriptRun no_tools = Run("PATH=/nonexistent ", Script("crash-report.sh"), " /bin/ls");
    INFO(no_tools.output.CStr());
    CHECK(no_tools.exit_code == 66);
    CHECK(no_tools.output.View().Contains("Install lldb"));
    if (!HaveTool("lldb"))
    {
        return;
    }
    const Text bad = Cat(dir.Path(), "/bad.dmp");
    REQUIRE(Run("printf 'MDMPnot really a minidump' > '", bad, "'").exit_code == 0);
    const ScriptRun run = Run(CrashReport(bad));
    INFO(run.output.CStr());
    CHECK(run.exit_code == 2);
}

TEST_CASE("macOS: without crash_extract, crash-report.sh warns and still reads a minidump with lldb",
          "[crash][scripts][death][debug][macos]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    if (!HaveTool("lldb"))
    {
        SKIP("lldb is not installed; crash-report.sh reads the dump with it (xcode-select --install)");
    }
    SpacedTempDir dir;
    const Text envelope = CrashProgram(ThisExecutable(), dir.Path());
    REQUIRE(envelope.Length() != 0);
    const Text dump = Cat(dir.Path(), "/crash.dmp");
    REQUIRE(Run("'" ENGINE_CRASH_EXTRACT_PATH "' '", envelope, "' '", dump, "'").exit_code == 0);
    // Without the tool the registers stay unread (pc 0), but the threads and modules are listed (review finding,
    // Day 292: it used to stop with exit status 2).
    const ScriptRun run = Run("ENGINE_CRASH_EXTRACT=/nonexistent ", Script("crash-report.sh"), " '", dump, "'");
    INFO(run.output.CStr());
    CHECK(run.exit_code == 0);
    CHECK(run.output.View().Contains("warning: crash_extract was not found"));
    CHECK(run.output.View().Contains("thread #1"));
    CHECK(run.output.View().Contains("image list"));
}

TEST_CASE("macOS: find-core.sh finds a program's newest core and checks its UUID, and open-core.sh prints its threads",
          "[postmortem][scripts][debug][macos]")
{
#if ENGINE_SANITIZER_ADDRESS || ENGINE_SANITIZER_THREAD
    SKIP("this check saves a core of a plain program and runs in the plain flavor");
#endif
    if (!HaveTool("lldb") || !HaveTool("dwarfdump"))
    {
        SKIP("lldb or dwarfdump is missing (xcode-select --install)");
    }
    SpacedTempDir dir;
    const Text work = dir.Path();
    // A core as the kernel would leave it in kern.corefile's pattern (core.%P), saved by lldb at the program's main: on
    // a stock Mac /cores belongs to root, so no process of this user can write one there. The pid's marker is printed
    // as "PID" + "=", so lldb's echo of the command does not contain it.
    REQUIRE(Run("cp '" UNIVERSAL_PROGRAM_PATH "' '", work, "/prog'").exit_code == 0);
    const ScriptRun saved = Run(
        "cd '", work,
        "' && lldb --no-lldbinit -b -O 'settings set symbols.enable-external-lookup false' "
        "-o 'breakpoint set -n main' -o 'process launch' "
        "-o 'script print(\"PID\" + \"=\" + str(lldb.debugger.GetSelectedTarget().GetProcess().GetProcessID()))' "
        "-o 'process save-core core.saved' -o 'process kill' -- ./prog");
    const Text pid = LineValue(saved.output.View(), "PID=");
    if (pid.Length() == 0 || Run("test -s '", work, "/core.saved'").exit_code != 0)
    {
        INFO(saved.output.CStr());
        SKIP("lldb could not run the program and save its core here (is debugging allowed for this user?)");
    }
    REQUIRE(Run("mv '", work, "/core.saved' '", work, "/core.", pid, "'").exit_code == 0);
    const Text pattern = Cat("ENGINE_CORE_PATTERN='", work, "/core.%P' ");

    const ScriptRun found = Run(pattern, Script("find-core.sh"), " --pid ", pid, " '", work, "/prog'");
    INFO(found.output.CStr());
    CHECK(found.exit_code == 0);
    CHECK(found.output.View().Contains(Cat(work, "/core.", pid).View()));
    CHECK(found.output.View().Contains("lldb -o bt"));
    CHECK_FALSE(found.output.View().Contains("not checked"));

    // Another program of the same name: a different build, refused.
    REQUIRE(Run("mkdir '", work, "/other' && cp '", ThisExecutable(), "' '", work, "/other/prog'").exit_code == 0);
    const ScriptRun wrong = Run(pattern, Script("find-core.sh"), " --pid ", pid, " '", work, "/other/prog'");
    INFO(wrong.output.CStr());
    CHECK(wrong.exit_code == 3);
    CHECK(wrong.output.View().Contains("different build"));

    const ScriptRun opened = Run(pattern, Script("open-core.sh"), " --pid ", pid, " '", work, "/prog'");
    INFO(opened.output.CStr());
    CHECK(opened.exit_code == 0);
    CHECK(opened.output.View().Contains("frame #0"));
    CHECK(opened.output.View().Contains("main"));

    // No core for another pid: the reason names the pattern.
    const ScriptRun none = Run(pattern, Script("find-core.sh"), " --pid 1 '", work, "/prog'");
    CHECK(none.exit_code == 2);
    CHECK(none.output.View().Contains("kern.corefile"));

    // A pattern naming the process (%N) with a program whose name is longer than the kernel keeps (32 bytes), and a
    // copy under that name, which is not the name the core records: the core is found by the cut name, and the
    // program's UUID is the core's first image outside the system's own (review findings, Day 292).
    const Text long_name = engine_test::MakeText("engine_physics_benchmark_tool_x64_long");
    REQUIRE(Run("cp '", work, "/prog' '", work, "/", long_name, "'").exit_code == 0);
    REQUIRE(Run("mv '", work, "/core.", pid, "' '", work, "/", long_name.View().Substring(0, 32), ".", pid, "'")
                .exit_code == 0);
    const ScriptRun cut = Run("ENGINE_CORE_PATTERN='", work, "/%N.%P' ", Script("find-core.sh"), " --pid ", pid, " '",
                              work, "/", long_name, "'");
    INFO(cut.output.CStr());
    CHECK(cut.exit_code == 0);
    CHECK_FALSE(cut.output.View().Contains("not checked"));
}

TEST_CASE("macOS: find-core.sh explains why there is no core under the system's pattern",
          "[postmortem][scripts][debug][macos]")
{
    const ScriptRun run = Run(Script("find-core.sh"), " --pid 1 /bin/ls");
    INFO(run.output.CStr());
    CHECK(run.exit_code == 2);
    CHECK(run.output.View().Contains("kern.corefile"));
    CHECK(run.output.View().Contains("kern.coredump"));
    const ScriptRun usage = Run(Script("find-core.sh"), " /nonexistent/program");
    CHECK(usage.exit_code == 64);
}
