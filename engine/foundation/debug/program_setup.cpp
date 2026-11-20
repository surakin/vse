#include "debug/program_setup.h"

#include "debug/crash_context.h"
#include "debug/crash_handler.h"
#include "debug/fault_injection.h"
#include "debug/stack_trace.h"
#include "log/assert.h"
#include "log/log.h"

#include "cstr/format.h"
#include "os/console.h"
#include "cstr/cstr.h"

namespace vse::debug
{

namespace
{

// The build line is Info, which LogCore (Warn by default) would drop, so it has a channel of its own.
LogChannel LogBuild("LogBuild", LogVerbosity::Info);

#if defined(ENGINE_CRASH_HANDLER_DEFAULT)
constexpr bool kBuildDefault = ENGINE_CRASH_HANDLER_DEFAULT != 0;
#elif defined(NDEBUG)
constexpr bool kBuildDefault = false;
#else
constexpr bool kBuildDefault = true;
#endif

bool StartsWith(const char* text, const char* prefix)
{
    return CStrCompareN(text, prefix, CStrLength(prefix)) == 0;
}

// -1 for text that is neither on nor off.
int ParseSwitch(const char* text)
{
    if (text == nullptr)
    {
        return -1;
    }
    if (CStrCompare(text, "1") == 0 || CStrCompare(text, "on") == 0 || CStrCompare(text, "true") == 0)
    {
        return 1;
    }
    if (CStrCompare(text, "0") == 0 || CStrCompare(text, "off") == 0 || CStrCompare(text, "false") == 0)
    {
        return 0;
    }
    return -1;
}

CrashTestKind ParseCrashTest(const char* text)
{
    if (CStrCompare(text, "segv") == 0)
    {
        return CrashTestKind::Segv;
    }
    if (CStrCompare(text, "abort") == 0)
    {
        return CrashTestKind::Abort;
    }
    if (CStrCompare(text, "assert") == 0)
    {
        return CrashTestKind::Assert;
    }
    if (CStrCompare(text, "ensure") == 0)
    {
        return CrashTestKind::Ensure;
    }
    return CrashTestKind::None;
}

const char* BuildConfiguration()
{
#if defined(ENGINE_BUILD_CONFIG)
    return ENGINE_BUILD_CONFIG;  // from CMake: debug, release, relwithdebinfo, minsizerel
#elif defined(NDEBUG)
    return "release";
#else
    return "debug";
#endif
}

}  // namespace

bool IsProgramDebugArgument(const char* argument)
{
    return argument != nullptr && (StartsWith(argument, "--crash-handler=") || StartsWith(argument, "--crash-test="));
}

ProgramDebugRequest ParseProgramDebugRequest(int argc, const char* const* argv, const char* environment_value,
                                             bool build_default)
{
    ProgramDebugRequest request;
    request.crash_handler = build_default;
    const int from_environment = ParseSwitch(environment_value);
    if (from_environment >= 0)
    {
        request.crash_handler = from_environment != 0;
    }
    for (int i = 0; argv != nullptr && i < argc; ++i)
    {
        if (argv[i] == nullptr)
        {
            continue;
        }
        // `--name=value` and `--name value` (the form os::CommandLine accepts for an option that takes a value).
        const char* value = nullptr;
        const char* shown = nullptr;  // what a warning quotes: the whole argument, or the value of the two-word form
        bool crash_handler_option = false;
        bool crash_test_option = false;
        if (StartsWith(argv[i], "--crash-handler="))
        {
            crash_handler_option = true;
            value = argv[i] + CStrLength("--crash-handler=");
            shown = argv[i];
        }
        else if (StartsWith(argv[i], "--crash-test="))
        {
            crash_test_option = true;
            value = argv[i] + CStrLength("--crash-test=");
            shown = argv[i];
        }
        else if (CStrCompare(argv[i], "--crash-handler") == 0 && i + 1 < argc && argv[i + 1] != nullptr)
        {
            crash_handler_option = true;
            value = argv[++i];
            shown = value;
        }
        else if (CStrCompare(argv[i], "--crash-test") == 0 && i + 1 < argc && argv[i + 1] != nullptr)
        {
            crash_test_option = true;
            value = argv[++i];
            shown = value;
        }
        if (crash_handler_option)
        {
            const int on = ParseSwitch(value);
            if (on >= 0)
            {
                request.crash_handler = on != 0;
            }
            else
            {
                request.ignored_argument = shown;
            }
        }
        else if (crash_test_option)
        {
            request.crash_test = ParseCrashTest(value);
            if (request.crash_test == CrashTestKind::None)
            {
                request.ignored_argument = shown;
            }
        }
    }
    return request;
}

ProgramDebugSetup StartProgramDebugging(int argc, const char* const* argv)
{
    InitDebugFacilities();
    InitCrashContext(argc, argv);

    ProgramDebugSetup setup;
    const ProgramDebugRequest request = ParseProgramDebugRequest(argc, argv, os::GetEnvironment("ENGINE_CRASH_HANDLER"),
                                                                 kBuildDefault);
    setup.crash_test = request.crash_test;

    const CrashContext& context = GetCrashContext();
    char line[512];  // the build id and the two context strings are each up to 95 characters
    FormatTo(line, sizeof(line), "build %s %s %s %s", context.build_id[0] != '\0' ? context.build_id : "(no build id)",
             context.os_name, context.architecture, BuildConfiguration());
    Log(LogBuild, LogVerbosity::Info, StringView(line));

    if (request.ignored_argument != nullptr)
    {
        char text[256];
        FormatTo(text, sizeof(text), "ignored %s: use --crash-handler=on|off and --crash-test=segv|abort|assert|ensure",
                 request.ignored_argument);
        Log(LogCore, LogVerbosity::Warn, StringView(text));
    }
    if (request.crash_handler)
    {
        const CrashHandlerResult result = InstallCrashHandler(CrashHandlerOptions());
        setup.crash_handler_installed = result == CrashHandlerResult::Installed ||
                                        result == CrashHandlerResult::AlreadyInstalled;
    }
#if !defined(NDEBUG)
    SetLogErrorTraces(true);  // an Error line carries a short trace in Debug builds
#endif
    return setup;
}

void RunCrashTest(CrashTestKind kind)
{
    switch (kind)
    {
    case CrashTestKind::None: return;
    case CrashTestKind::Segv: InjectFault(FaultKind::NullWrite);
    case CrashTestKind::Abort: InjectFault(FaultKind::Abort);
    case CrashTestKind::Assert: ENGINE_ASSERT(false, "--crash-test=assert"); return;
    case CrashTestKind::Ensure: ENGINE_ENSURE(false, "--crash-test=ensure"); return;
    }
}

}  // namespace vse::debug
