#pragma once

#include <cstdarg>
#include <cstdio>

#include "log/log.h"

namespace engine_placeholder::jobs {

extern LogChannel LogJobs;

// printf-style helper (stack buffer, no allocator): job-system logging
// happens at startup/teardown and on diagnostics, never on a hot path.
void JobsLogV(LogVerbosity level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
inline void JobsLogV(LogVerbosity level, const char* fmt, ...) {
    if (!LogJobs.ShouldLog(level)) {
        return;
    }
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    usize len = static_cast<usize>(n) < sizeof(buf) ? static_cast<usize>(n) : sizeof(buf) - 1;
    Log(LogJobs, level, StringView(buf, len));
}

#define JobsLog(level, ...) ::engine_placeholder::jobs::JobsLogV(level, __VA_ARGS__)

} // namespace engine_placeholder::jobs
