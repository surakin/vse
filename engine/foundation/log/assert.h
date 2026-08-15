#pragma once

#include "types.h"

// Split out of log.h so headers that only need ENGINE_ASSERT (e.g.
// containers/array.h) don't have to pull in the full logging facility,
// which depends on string/string.h — that dependency direction would
// otherwise create an #include cycle for any header string.h itself
// depends on (string.h -> containers/array.h -> log.h -> string.h).
// AssertFail/ENGINE_ASSERT only ever take a plain `const char*`, so
// they have no StringView (and therefore no string.h) dependency.
namespace engine_placeholder {

// Indirection so tests can capture/intercept output without real fd
// redirection (dup2/pipe/fork). Default targets are the real os::
// functions; production code never touches these setters. Shared by
// both AssertFail and log.h's Log() — one pair of sinks for both.
using WriteSinkFn = long (*)(int fd, const void* buf, usize len);
using ExitSinkFn = void (*)(int code);
void SetWriteSinkForTesting(WriteSinkFn sink);
void SetExitSinkForTesting(ExitSinkFn sink);

[[noreturn]] void AssertFail(const char* file, int line, const char* message);

} // namespace engine_placeholder

#define ENGINE_ASSERT(cond, msg)                                            \
    do {                                                                     \
        if (!(cond)) {                                                        \
            ::engine_placeholder::AssertFail(__FILE__, __LINE__, msg);         \
        }                                                                       \
    } while (false)
