#include "debug/program_setup.h"

#include "debug/crash_context.h"
#include "debug/crash_handler.h"
#include "debug/fault_injection.h"
#include "debug/stack_trace.h"
#include "log/assert.h"
#include "log/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace engine_placeholder::debug {

namespace {

// The build line is Info, which LogCore (Warn by default) would drop; this channel has the same name and always shows it.
LogChannel LogBuild("LogCore", LogVerbosity::Info);

#if defined(ENGINE_CRASH_HANDLER_DEFAULT)
constexpr bool kBuildDefault = ENGINE_CRASH_HANDLER_DEFAULT != 0;
#elif defined(NDEBUG)
constexpr bool kBuildDefault = false;
#else
constexpr bool kBuildDefault = true;
#endif

bool StartsWith(const char* text, const char* prefix) {
    return std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}

// -1 for text that is neither on nor off.
int ParseSwitch(const char* text) {
    if (text == nullptr) {
        return -1;
    }
    if (std::strcmp(text, "1") == 0 || std::strcmp(text, "on") == 0 || std::strcmp(text, "true") == 0) {
        return 1;
    }
    if (std::strcmp(text, "0") == 0 || std::strcmp(text, "off") == 0 || std::strcmp(text, "false") == 0) {
        return 0;
    }
    return -1;
}

CrashTestKind ParseCrashTest(const char* text) {
    if (std::strcmp(text, "segv") == 0) return CrashTestKind::Segv;
    if (std::strcmp(text, "abort") == 0) return CrashTestKind::Abort;
    if (std::strcmp(text, "assert") == 0) return CrashTestKind::Assert;
    if (std::strcmp(text, "ensure") == 0) return CrashTestKind::Ensure;
    return CrashTestKind::None;
}

const char* BuildConfiguration() {
#if defined(ENGINE_BUILD_CONFIG)
    return ENGINE_BUILD_CONFIG; // from CMake: debug, release, relwithdebinfo, minsizerel
#elif defined(NDEBUG)
    return "release";
#else
    return "debug";
#endif
}

} // namespace

bool IsProgramDebugArgument(const char* argument) {
    return argument != nullptr && (StartsWith(argument, "--crash-handler=") || StartsWith(argument, "--crash-test="));
}

ProgramDebugRequest ParseProgramDebugRequest(int argc, const char* const* argv, const char* environment_value, bool build_default) {
    ProgramDebugRequest request;
    request.crash_handler = build_default;
    const int from_environment = ParseSwitch(environment_value);
    if (from_environment >= 0) {
        request.crash_handler = from_environment != 0;
    }
    for (int i = 0; argv != nullptr && i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        if (StartsWith(argv[i], "--crash-handler=")) {
            const int value = ParseSwitch(argv[i] + std::strlen("--crash-handler="));
            if (value >= 0) {
                request.crash_handler = value != 0;
            } else {
                request.ignored_argument = argv[i];
            }
        } else if (StartsWith(argv[i], "--crash-test=")) {
            request.crash_test = ParseCrashTest(argv[i] + std::strlen("--crash-test="));
            if (request.crash_test == CrashTestKind::None) {
                request.ignored_argument = argv[i];
            }
        }
    }
    return request;
}

ProgramDebugSetup StartProgramDebugging(int argc, const char* const* argv) {
    InitDebugFacilities();
    InitCrashContext(argc, argv);

    ProgramDebugSetup setup;
    const ProgramDebugRequest request = ParseProgramDebugRequest(argc, argv, std::getenv("ENGINE_CRASH_HANDLER"), kBuildDefault);
    setup.crash_test = request.crash_test;

    const CrashContext& context = GetCrashContext();
    char line[256];
    std::snprintf(line, sizeof(line), "build %s %s %s %s", context.build_id[0] != '\0' ? context.build_id : "(no build id)",
                  context.os_name, context.architecture, BuildConfiguration());
    Log(LogBuild, LogVerbosity::Info, StringView(line));

    if (request.ignored_argument != nullptr) {
        char text[256];
        std::snprintf(text, sizeof(text), "ignored %s: use --crash-handler=on|off and --crash-test=segv|abort|assert|ensure", request.ignored_argument);
        Log(LogCore, LogVerbosity::Warn, StringView(text));
    }
    if (request.crash_handler) {
        const CrashHandlerResult result = InstallCrashHandler(CrashHandlerOptions());
        setup.crash_handler_installed = result == CrashHandlerResult::Installed || result == CrashHandlerResult::AlreadyInstalled;
    }
#if !defined(NDEBUG)
    SetLogErrorTraces(true); // an Error line carries a short trace in Debug builds
#endif
    return setup;
}

void RunCrashTest(CrashTestKind kind) {
    switch (kind) {
    case CrashTestKind::None: return;
    case CrashTestKind::Segv: InjectFault(FaultKind::NullWrite);
    case CrashTestKind::Abort: InjectFault(FaultKind::Abort);
    case CrashTestKind::Assert: ENGINE_ASSERT(false, "--crash-test=assert"); return;
    case CrashTestKind::Ensure: ENGINE_ENSURE(false, "--crash-test=ensure"); return;
    }
}

} // namespace engine_placeholder::debug
