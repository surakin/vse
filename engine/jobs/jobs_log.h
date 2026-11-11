#pragma once

#include "cstr/varargs.h"
#include "cstr/format.h"

#include "debug/compiler.h"
#include "log/log.h"

namespace engine_placeholder::jobs
{

/// The log channel used by the job system.
extern LogChannel LogJobs;

/// Logs a printf-style message on [[jobs::LogJobs]] using a 256-byte stack buffer and no allocator.
///
/// Job-system logging happens at startup, teardown and for diagnostics,
/// never on a hot path. Output longer than the buffer is truncated, and
/// nothing is formatted when the level is filtered out.
///
/// - Param level: verbosity of the message.
/// - Param fmt: printf format string; the arguments follow it.
void JobsLogV(LogVerbosity level, const char* fmt, ...) ENGINE_PRINTF_FORMAT(2, 3);
inline void JobsLogV(LogVerbosity level, const char* fmt, ...)
{
    if (!LogJobs.ShouldLog(level))
    {
        return;
    }
    char buf[256];
    VarArgs ap;
    ENGINE_VA_START(ap, fmt);
    int n = VFormatTo(buf, sizeof(buf), fmt, ap);
    ENGINE_VA_END(ap);
    if (n < 0)
    {
        return;
    }
    usize len = static_cast<usize>(n) < sizeof(buf) ? static_cast<usize>(n) : sizeof(buf) - 1;
    Log(LogJobs, level, StringView(buf, len));
}

/// Logs a printf-style message on the job-system channel; expands to a call of [[jobs::JobsLogV]].
///
/// - Param level: verbosity of the message.
#define JobsLog(level, ...) ::engine_placeholder::jobs::JobsLogV(level, __VA_ARGS__)

}  // namespace engine_placeholder::jobs
