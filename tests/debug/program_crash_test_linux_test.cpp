// The programs that ship (the demos and benches) with --crash-test=...: each kind of fault gives the report, the dump and the exit
// status it should, before any window or device is created, so no display or GPU is needed.
#include <catch2/catch_test_macros.hpp>

#include "debug/program_setup.h"
#include "log/log.h"
#include "support/death_test_skip.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ftw.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <vector>

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace {

struct ProgramRun {
    std::string output;
    int exit_code = -1;
};

ProgramRun RunProgramCommand(const std::string& command) {
    ProgramRun run;
    FILE* pipe = popen((command + " 2>&1; echo EXIT=$?").c_str(), "r");
    if (pipe == nullptr) {
        return run;
    }
    char buffer[4096];
    usize n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        run.output.append(buffer, n);
    }
    pclose(pipe);
    const usize at = run.output.rfind("EXIT=");
    if (at != std::string::npos) {
        run.exit_code = std::atoi(run.output.c_str() + at + 5);
    }
    return run;
}

class ProgramStateDir {
public:
    ProgramStateDir() {
        std::snprintf(path_, sizeof(path_), "/tmp/engine-program-XXXXXX");
        if (mkdtemp(path_) == nullptr) {
            path_[0] = '\0';
        }
    }
    ~ProgramStateDir() {
        if (path_[0] != '\0') {
            nftw(path_, &Remove, 16, FTW_DEPTH | FTW_PHYS);
        }
    }
    ProgramStateDir(const ProgramStateDir&) = delete;
    ProgramStateDir& operator=(const ProgramStateDir&) = delete;
    std::string Path() const { return path_; }

private:
    static int Remove(const char* entry, const struct stat*, int, struct FTW*) { return ::remove(entry); }
    char path_[64];
};

#if defined(ENGINE_PROGRAM_RHI_DEMO) // the shipped programs are built into the plain flavor only
struct ShippedProgram {
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
std::string Command(const ProgramStateDir& state, const ShippedProgram& program, const char* arguments) {
    return "XDG_STATE_HOME='" + state.Path() + "' '" + program.path + "' " + arguments;
}

usize CountEnvelopes(const std::string& directory) {
    const ProgramRun run = RunProgramCommand("find '" + directory + "' -name '*.envelope' | wc -l");
    return static_cast<usize>(std::atoi(run.output.c_str()));
}

#endif // the shipped programs

} // namespace

#if defined(ENGINE_PROGRAM_RHI_DEMO)
TEST_CASE("Every shipped program turns --crash-test=segv into a report, a dump and a SIGSEGV exit", "[crash][programs][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    for (const ShippedProgram& program : kPrograms) {
        INFO(program.name);
        ProgramStateDir state;
        const ProgramRun run = RunProgramCommand(Command(state, program, "--crash-handler=on --crash-test=segv"));
        INFO(run.output);
        CHECK(run.exit_code == 128 + 11);
        CHECK(run.output.find("=== CRASH REPORT ===") != std::string::npos);
        CHECK(run.output.find("reason: AccessViolation") != std::string::npos);
        CHECK(run.output.find("RunCrashTest") != std::string::npos);
        CHECK(run.output.find("build id: ") != std::string::npos);
        CHECK(run.output.find("[LogBuild] INFO: build ") != std::string::npos); // the build line, first thing the program logs
        CHECK(CountEnvelopes(state.Path()) == 1);
    }
}

TEST_CASE("Every shipped program turns --crash-test=abort and =assert into SIGABRT with a report and a dump", "[crash][programs][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    for (const ShippedProgram& program : kPrograms) {
        INFO(program.name);
        {
            ProgramStateDir state;
            const ProgramRun run = RunProgramCommand(Command(state, program, "--crash-handler=on --crash-test=abort"));
            INFO(run.output);
            CHECK(run.exit_code == 128 + 6);
            CHECK(run.output.find("reason: Abort") != std::string::npos);
            CHECK(CountEnvelopes(state.Path()) == 1);
        }
        {
            ProgramStateDir state;
            const ProgramRun run = RunProgramCommand(Command(state, program, "--crash-handler=on --crash-test=assert"));
            INFO(run.output);
            CHECK(run.exit_code == 128 + 6);
            CHECK(run.output.find("ASSERT FAILED: ") != std::string::npos);
            CHECK(run.output.find("--crash-test=assert") != std::string::npos);
            CHECK(CountEnvelopes(state.Path()) == 1);
        }
    }
}

TEST_CASE("A crash test of kind ensure reports and the program carries on", "[crash][programs][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    // rhi_bench gets as far as its own argument error: the ensure came first and did not stop it.
    ProgramStateDir state;
    const ShippedProgram& program = kPrograms[1];
    const ProgramRun run = RunProgramCommand(Command(state, program, "--crash-handler=on --crash-test=ensure --not-an-option"));
    INFO(run.output);
    CHECK(run.output.find("ENSURE FAILED: ") != std::string::npos);
    CHECK(run.output.find("usage:") != std::string::npos);
    CHECK(run.output.find("=== CRASH REPORT ===") == std::string::npos);
    CHECK(CountEnvelopes(state.Path()) == 0);
}

TEST_CASE("A mistyped crash test is warned about, not silently ignored", "[crash][programs][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ProgramStateDir state;
    const ProgramRun run = RunProgramCommand(Command(state, kPrograms[1], "--crash-test=sgev --not-an-option"));
    INFO(run.output);
    CHECK(run.output.find("ignored --crash-test=sgev") != std::string::npos);
}

TEST_CASE("The argument crash-handler=off leaves a fault with no report and no dump", "[crash][programs][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ProgramStateDir state;
    const ProgramRun run = RunProgramCommand(Command(state, kPrograms[1], "--crash-handler=off --crash-test=segv"));
    INFO(run.output);
    CHECK(run.exit_code == 128 + 11);
    CHECK(run.output.find("=== CRASH REPORT ===") == std::string::npos);
    CHECK(CountEnvelopes(state.Path()) == 0);
}

TEST_CASE("ENGINE_CRASH_HANDLER in the environment turns the handler on, and the argument overrides it", "[crash][programs][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    {
        ProgramStateDir state;
        const ProgramRun run = RunProgramCommand("ENGINE_CRASH_HANDLER=1 " + Command(state, kPrograms[1], "--crash-test=segv"));
        CHECK(run.output.find("=== CRASH REPORT ===") != std::string::npos);
    }
    {
        ProgramStateDir state;
        const ProgramRun run = RunProgramCommand("ENGINE_CRASH_HANDLER=1 " + Command(state, kPrograms[1], "--crash-handler=off --crash-test=segv"));
        CHECK(run.output.find("=== CRASH REPORT ===") == std::string::npos);
    }
}

#endif // the shipped programs

// ---- the decision itself, without starting a program ----------------------------------------------------------------------------

TEST_CASE("The crash handler setting comes from the argument, then the environment, then the build default", "[crash][programs][debug]") {
    const char* off[] = {"prog", "--crash-handler=off"};
    const char* on[] = {"prog", "--crash-handler=on"};
    const char* none[] = {"prog"};
    CHECK_FALSE(ParseProgramDebugRequest(2, off, "1", true).crash_handler);   // argument beats environment and default
    CHECK(ParseProgramDebugRequest(2, on, "0", false).crash_handler);
    CHECK(ParseProgramDebugRequest(1, none, "1", false).crash_handler);      // environment beats the default
    CHECK_FALSE(ParseProgramDebugRequest(1, none, "off", true).crash_handler);
    CHECK(ParseProgramDebugRequest(1, none, nullptr, true).crash_handler);   // the default
    CHECK_FALSE(ParseProgramDebugRequest(1, none, nullptr, false).crash_handler);
    CHECK(ParseProgramDebugRequest(0, nullptr, nullptr, true).crash_handler); // a program with no command line
}

TEST_CASE("A value that is neither on nor off is ignored, and the crash test is parsed", "[crash][programs][debug]") {
    const char* odd[] = {"prog", "--crash-handler=maybe", "--crash-test=assert"};
    const ProgramDebugRequest request = ParseProgramDebugRequest(3, odd, "garbage", true);
    CHECK(request.crash_handler); // both unusable: the default stands
    CHECK(request.crash_test == CrashTestKind::Assert);
    const char* kinds[] = {"p", "--crash-test=segv"};
    CHECK(ParseProgramDebugRequest(2, kinds, nullptr, false).crash_test == CrashTestKind::Segv);
    const char* unknown[] = {"p", "--crash-test=banana"};
    CHECK(ParseProgramDebugRequest(2, unknown, nullptr, false).crash_test == CrashTestKind::None);
    CHECK(std::strcmp(request.ignored_argument, "--crash-handler=maybe") == 0); // remembered so the program can warn
    const char* typo[] = {"p", "--crash-test=sgev"};
    CHECK(std::strcmp(ParseProgramDebugRequest(2, typo, nullptr, false).ignored_argument, "--crash-test=sgev") == 0);
    const char* fine[] = {"p", "--crash-test=segv", "--crash-handler=on"};
    CHECK(ParseProgramDebugRequest(3, fine, nullptr, false).ignored_argument == nullptr);
    CHECK(IsProgramDebugArgument("--crash-test=segv"));
    CHECK(IsProgramDebugArgument("--crash-handler=on"));
    CHECK_FALSE(IsProgramDebugArgument("--frames"));
    CHECK_FALSE(IsProgramDebugArgument(nullptr));
}

TEST_CASE("The build line has its own channel, so a config line for LogCore reaches the real LogCore", "[crash][programs][debug]") {
    // Exactly one channel answers to each name; the build line's channel is LogBuild.
    StartProgramDebugging(0, nullptr);
    CHECK(LogChannel::Find(StringView("LogCore")) == &LogCore);
    CHECK(LogChannel::Find(StringView("LogBuild")) != nullptr);
}
