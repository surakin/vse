// The programs that ship (the demos and benches) with --crash-test=...: each kind of fault gives the report, the dump
// and the exit status it should, before any window or device is created, so no display or GPU is needed.
#include <catch2/catch_test_macros.hpp>

#include "debug/program_setup.h"
#include "log/log.h"
#include "support/death_test_skip.h"

#include "cstr/number.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "support/test_support_posix.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;
using debug_test::Cat;  // NOLINT(misc-unused-using-decls): used as Cat(command...) in a pack expansion, which the check
                        // does not see
using debug_test::Text;

namespace
{

class ProgramStateDir
{
public:
    ProgramStateDir()
    {
        FormatTo(path_, sizeof(path_), "/tmp/engine-program-XXXXXX");
        if (mkdtemp(path_) == nullptr)
        {
            path_[0] = '\0';
        }
    }
    ~ProgramStateDir()
    {
        if (path_[0] != '\0')
        {
            debug_test::RemoveTree(path_);
        }
    }
    ProgramStateDir(const ProgramStateDir&) = delete;
    ProgramStateDir& operator=(const ProgramStateDir&) = delete;
    Text Path() const { return engine_test::MakeText(path_); }

private:
    char path_[64];
};

#if defined(ENGINE_PROGRAM_RHI_DEMO)  // the shipped programs are built into the plain flavor only

struct ProgramRun
{
    Text output = engine_test::MakeText();
    int exit_code = -1;
};

// The pieces joined into one command; its output (standard error merged in) ends with the exit status the shell echoes.
template <typename... Parts> ProgramRun RunProgramCommand(const Parts&... command)
{
    ProgramRun run;
    debug_test::ShellRun shell = debug_test::RunShell(Cat(command..., " 2>&1; echo EXIT=$?").View());
    run.output = static_cast<Text&&>(shell.output);
    const usize at = debug_test::FindLast(run.output.View(), "EXIT=");
    if (at != run.output.Length())
    {
        run.exit_code = CStrToInt(run.output.CStr() + at + 5);
    }
    return run;
}

struct ShippedProgram
{
    const char* name;
    const char* path;
};

const ShippedProgram kPrograms[] = {
    {"rhi_demo", ENGINE_PROGRAM_RHI_DEMO},
    {"rhi_bench", ENGINE_PROGRAM_RHI_BENCH},
    {"renderer_demo", ENGINE_PROGRAM_RENDERER_DEMO},
    {"renderer_bench", ENGINE_PROGRAM_RENDERER_BENCH},
};

// The crash dump directory is under XDG_STATE_HOME, so each run gets its own and leaves nothing in the user's home.
Text Command(const ProgramStateDir& state, const ShippedProgram& program, const char* arguments)
{
    return Cat("XDG_STATE_HOME='", state.Path(), "' '", program.path, "' ", arguments);
}

usize CountEnvelopes(const Text& directory)
{
    const ProgramRun run = RunProgramCommand("find '", directory, "' -name '*.envelope' | wc -l");
    return static_cast<usize>(CStrToInt(run.output.CStr()));
}

#endif  // the shipped programs

}  // namespace

#if defined(ENGINE_PROGRAM_RHI_DEMO)
TEST_CASE("Every shipped program turns --crash-test=segv into a report, a dump and a SIGSEGV exit",
          "[crash][programs][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    for (const ShippedProgram& program : kPrograms)
    {
        INFO(program.name);
        ProgramStateDir state;
        const ProgramRun run = RunProgramCommand(Command(state, program, "--crash-handler=on --crash-test=segv"));
        INFO(run.output.CStr());
        CHECK(run.exit_code == 128 + 11);
        CHECK(run.output.View().Contains("=== CRASH REPORT ==="));
        CHECK(run.output.View().Contains("reason: AccessViolation"));
        CHECK(run.output.View().Contains("RunCrashTest"));
        CHECK(run.output.View().Contains("build id: "));
        CHECK(run.output.View().Contains("[LogBuild] INFO: build "));  // the build line, first thing the program logs
        CHECK(CountEnvelopes(state.Path()) == 1);
    }
}

TEST_CASE("Every shipped program turns --crash-test=abort and =assert into SIGABRT with a report and a dump",
          "[crash][programs][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    for (const ShippedProgram& program : kPrograms)
    {
        INFO(program.name);
        {
            ProgramStateDir state;
            const ProgramRun run = RunProgramCommand(Command(state, program, "--crash-handler=on --crash-test=abort"));
            INFO(run.output.CStr());
            CHECK(run.exit_code == 128 + 6);
            CHECK(run.output.View().Contains("reason: Abort"));
            CHECK(CountEnvelopes(state.Path()) == 1);
        }
        {
            ProgramStateDir state;
            const ProgramRun run = RunProgramCommand(Command(state, program, "--crash-handler=on --crash-test=assert"));
            INFO(run.output.CStr());
            CHECK(run.exit_code == 128 + 6);
            CHECK(run.output.View().Contains("ASSERT FAILED: "));
            CHECK(run.output.View().Contains("--crash-test=assert"));
            CHECK(CountEnvelopes(state.Path()) == 1);
        }
    }
}

TEST_CASE("A crash test of kind ensure reports and the program carries on", "[crash][programs][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    // rhi_bench gets as far as its own argument error: the ensure came first and did not stop it.
    ProgramStateDir state;
    const ShippedProgram& program = kPrograms[1];
    const ProgramRun run = RunProgramCommand(
        Command(state, program, "--crash-handler=on --crash-test=ensure --not-an-option"));
    INFO(run.output.CStr());
    CHECK(run.output.View().Contains("ENSURE FAILED: "));
    CHECK(run.output.View().Contains("usage:"));
    CHECK_FALSE(run.output.View().Contains("=== CRASH REPORT ==="));
    CHECK(CountEnvelopes(state.Path()) == 0);
}

TEST_CASE("A mistyped crash test is warned about, not silently ignored", "[crash][programs][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ProgramStateDir state;
    const ProgramRun run = RunProgramCommand(Command(state, kPrograms[1], "--crash-test=sgev --not-an-option"));
    INFO(run.output.CStr());
    CHECK(run.output.View().Contains("ignored --crash-test=sgev"));
}

TEST_CASE("The argument crash-handler=off leaves a fault with no report and no dump", "[crash][programs][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ProgramStateDir state;
    const ProgramRun run = RunProgramCommand(Command(state, kPrograms[1], "--crash-handler=off --crash-test=segv"));
    INFO(run.output.CStr());
    CHECK(run.exit_code == 128 + 11);
    CHECK_FALSE(run.output.View().Contains("=== CRASH REPORT ==="));
    CHECK(CountEnvelopes(state.Path()) == 0);
}

TEST_CASE("ENGINE_CRASH_HANDLER in the environment turns the handler on, and the argument overrides it",
          "[crash][programs][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    {
        ProgramStateDir state;
        const ProgramRun run = RunProgramCommand("ENGINE_CRASH_HANDLER=1 ",
                                                 Command(state, kPrograms[1], "--crash-test=segv"));
        CHECK(run.output.View().Contains("=== CRASH REPORT ==="));
    }
    {
        ProgramStateDir state;
        const ProgramRun run = RunProgramCommand("ENGINE_CRASH_HANDLER=1 ",
                                                 Command(state, kPrograms[1], "--crash-handler=off --crash-test=segv"));
        CHECK_FALSE(run.output.View().Contains("=== CRASH REPORT ==="));
    }
}

#endif  // the shipped programs

// ---- the decision itself, without starting a program
// ----------------------------------------------------------------------------

TEST_CASE("The crash handler setting comes from the argument, then the environment, then the build default",
          "[crash][programs][debug]")
{
    const char* off[] = {"prog", "--crash-handler=off"};
    const char* on[] = {"prog", "--crash-handler=on"};
    const char* none[] = {"prog"};
    // The space-separated form is the same option (os::CommandLine accepts it for a declared option).
    const char* spaced_off[] = {"prog", "--crash-handler", "off"};
    const char* spaced_test[] = {"prog", "--crash-test", "segv"};
    CHECK_FALSE(ParseProgramDebugRequest(3, spaced_off, "1", true).crash_handler);
    CHECK(ParseProgramDebugRequest(3, spaced_test, nullptr, false).crash_test == CrashTestKind::Segv);
    CHECK_FALSE(ParseProgramDebugRequest(2, off, "1", true).crash_handler);  // argument beats environment and default
    CHECK(ParseProgramDebugRequest(2, on, "0", false).crash_handler);
    CHECK(ParseProgramDebugRequest(1, none, "1", false).crash_handler);  // environment beats the default
    CHECK_FALSE(ParseProgramDebugRequest(1, none, "off", true).crash_handler);
    CHECK(ParseProgramDebugRequest(1, none, nullptr, true).crash_handler);  // the default
    CHECK_FALSE(ParseProgramDebugRequest(1, none, nullptr, false).crash_handler);
    CHECK(ParseProgramDebugRequest(0, nullptr, nullptr, true).crash_handler);  // a program with no command line
}

TEST_CASE("A value that is neither on nor off is ignored, and the crash test is parsed", "[crash][programs][debug]")
{
    const char* odd[] = {"prog", "--crash-handler=maybe", "--crash-test=assert"};
    const ProgramDebugRequest request = ParseProgramDebugRequest(3, odd, "garbage", true);
    CHECK(request.crash_handler);  // both unusable: the default stands
    CHECK(request.crash_test == CrashTestKind::Assert);
    const char* kinds[] = {"p", "--crash-test=segv"};
    CHECK(ParseProgramDebugRequest(2, kinds, nullptr, false).crash_test == CrashTestKind::Segv);
    const char* unknown[] = {"p", "--crash-test=banana"};
    CHECK(ParseProgramDebugRequest(2, unknown, nullptr, false).crash_test == CrashTestKind::None);
    CHECK(CStrCompare(request.ignored_argument, "--crash-handler=maybe") == 0);  // remembered so the program can warn
    const char* typo[] = {"p", "--crash-test=sgev"};
    CHECK(CStrCompare(ParseProgramDebugRequest(2, typo, nullptr, false).ignored_argument, "--crash-test=sgev") == 0);
    const char* fine[] = {"p", "--crash-test=segv", "--crash-handler=on"};
    CHECK(ParseProgramDebugRequest(3, fine, nullptr, false).ignored_argument == nullptr);
    CHECK(IsProgramDebugArgument("--crash-test=segv"));
    CHECK(IsProgramDebugArgument("--crash-handler=on"));
    CHECK_FALSE(IsProgramDebugArgument("--frames"));
    CHECK_FALSE(IsProgramDebugArgument(nullptr));
}

TEST_CASE("The build line has its own channel, so a config line for LogCore reaches the real LogCore",
          "[crash][programs][debug]")
{
    // Exactly one channel answers to each name; the build line's channel is LogBuild.
    StartProgramDebugging(0, nullptr);
    CHECK(LogChannel::Find(StringView("LogCore")) == &LogCore);
    CHECK(LogChannel::Find(StringView("LogBuild")) != nullptr);
}
