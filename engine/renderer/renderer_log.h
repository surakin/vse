#pragma once

#include "cstr/varargs.h"
#include "cstr/format.h"

#include "debug/compiler.h"
#include "log/log.h"

//! The renderer: a deferred, render-graph-driven renderer built on the RHI.

namespace engine_placeholder::renderer {

/// Logs a printf-style message on the foundation's `LogRender` channel using a 512-byte stack buffer and no allocator.
///
/// The renderer reports environment problems (a format the device cannot render to, a failed pipeline), never per-draw
/// events. It shares `LogRender` with the rest of the engine, so `--log-level` applies. Output longer than the buffer is
/// truncated, and nothing is formatted when the level is filtered out.
///
/// Param level: verbosity of the message.
/// Param fmt: printf format string; the arguments follow it.
/// Thread-safety: thread-safe. Formats into a stack buffer and logs through the shared sink.
void RenderLogV(LogVerbosity level, const char* fmt, ...) ENGINE_PRINTF_FORMAT(2, 3);
inline void RenderLogV(LogVerbosity level, const char* fmt, ...) {
    if (!LogRender.ShouldLog(level)) {
        return;
    }
    char buf[512];
    VarArgs ap;
    ENGINE_VA_START(ap, fmt);
    int n = VFormatTo(buf, sizeof(buf), fmt, ap);
    ENGINE_VA_END(ap);
    if (n < 0) {
        return;
    }
    usize len = static_cast<usize>(n) < sizeof(buf) ? static_cast<usize>(n) : sizeof(buf) - 1;
    Log(LogRender, level, StringView(buf, len));
}

/// Logs a printf-style message on the render channel; expands to a call of [[renderer::RenderLogV]].
///
/// Param level: verbosity of the message.
#define RenderLog(level, ...) ::engine_placeholder::renderer::RenderLogV(level, __VA_ARGS__)

} // namespace engine_placeholder::renderer
