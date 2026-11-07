#pragma once

#include <cstdarg>
#include <cstdio>

#include "debug/compiler.h"
#include "log/log.h"

//! Logging for the RHI: the `LogRhi` channel and a printf-style helper.

namespace engine_placeholder::rhi {

/// The log channel used by the RHI and its backends.
extern LogChannel LogRhi;

/// Logs a printf-style message on [[rhi::LogRhi]] using a 512-byte stack buffer and no allocator.
///
/// RHI logging reports environment problems, validation output and device
/// diagnostics, never per-draw events. Output longer than the buffer is
/// truncated, and nothing is formatted when the level is filtered out.
///
/// Param level: verbosity of the message.
/// Param fmt: printf format string; the arguments follow it.
/// Thread-safety: thread-safe. Formats into a stack buffer and logs through the shared sink.
void RhiLogV(LogVerbosity level, const char* fmt, ...) ENGINE_PRINTF_FORMAT(2, 3);
inline void RhiLogV(LogVerbosity level, const char* fmt, ...) {
    if (!LogRhi.ShouldLog(level)) {
        return;
    }
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    usize len = static_cast<usize>(n) < sizeof(buf) ? static_cast<usize>(n) : sizeof(buf) - 1;
    Log(LogRhi, level, StringView(buf, len));
}

/// Logs a printf-style message on the RHI channel; expands to a call of [[rhi::RhiLogV]].
///
/// Param level: verbosity of the message.
#define RhiLog(level, ...) ::engine_placeholder::rhi::RhiLogV(level, __VA_ARGS__)

} // namespace engine_placeholder::rhi
