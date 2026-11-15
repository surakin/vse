#include "debug/failure_hooks.h"

#include "debug/compiler.h"
#include "debug/crash_context.h"
#include "debug/crash_event_platform.h"
#include "debug/crash_handler.h"
#include "debug/crash_report.h"
#include "debug/debugger.h"
#include "debug/stack_trace.h"
#include "log/diagnostics_hooks.h"

namespace engine_placeholder::debug
{

void UnregisterThreadForCrashHandling()
{
    detail::ReleaseThreadAlternateStack();
}

namespace
{

// One frame deeper than the old inline call in the assert handler, so it asks CaptureStackTrace to skip one more.
ENGINE_NOINLINE usize AppendTrace(char* buffer, usize capacity, u32 skip_frames)
{
    StackTrace trace;
    if (!CaptureStackTrace(trace, skip_frames + 1))
    {
        return 0;
    }
    return FormatStackTrace(trace, buffer, capacity);
}

void WriteContext(void (*write)(void* context, const char* data, usize length), void* context)
{
    if (!CrashContextInitialized())
    {
        InitCrashContext(0, nullptr);  // an assert is not a signal handler, so reading /proc is fine here
    }
    CrashReportSink sink;
    sink.write = write;
    sink.context = context;
    WriteCrashContext(sink);
}

bool DebuggerAttached()
{
    return IsDebuggerAttached();
}

void RegisterThread(const char* name)
{
    RegisterThreadForCrashHandling(name);
}

void UnregisterThread()
{
    UnregisterThreadForCrashHandling();
}

const FailureReportHooks kHooks = {&AppendTrace, &WriteContext, &DebuggerAttached};

}  // namespace

void InstallDiagnosticsHooks()
{
    SetFailureReportHooks(&kHooks);
    SetThreadStartHook(&RegisterThread);
    AddThreadEndHook(&UnregisterThread);
}

}  // namespace engine_placeholder::debug
