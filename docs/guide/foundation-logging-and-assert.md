# Foundation: Logging and Assertions

> **Status:** skeleton — not yet written  
> **Audience:** engine programmers  
> **Covers:** Phase 0 (+ job system channel)

## Source of truth

- `engine/foundation/log/log.h`
- `engine/foundation/log/assert.h`

## API reference

Full reference: [Foundation: Logging and Assertions](../reference/foundation-log.md).

- [[LogChannel]] — a named, filterable log channel
- [[LogVerbosity]] — verbosity levels
- [[Log]] — write a message to a channel
- [[LoadConfig]] — configure channels from a file
- [[SetAssertHandler]], [[AssertInfo]], [[AssertAction]] — the handler every failed check goes to
- [[ENGINE_ASSERT]], [[ENGINE_ENSURE]], [[ENGINE_VERIFY]], [[ENGINE_CHECK_SLOW]], [[ENGINE_UNREACHABLE]], [[ENGINE_NOT_IMPLEMENTED]]

## Assertions: which macro to use

| Macro | Checked | On failure | Use it for |
|---|---|---|---|
| `ENGINE_ASSERT(cond, msg)` | always | report, then abort | a precondition whose violation is a bug in the caller and cannot be recovered from |
| `ENGINE_ENSURE(cond, msg)` | always | report **once per call site**, count the rest, return `false`; the program goes on | a condition that should hold but that the code can survive; it returns the condition, so `if (!ENGINE_ENSURE(p != nullptr, "no p")) return;` works |
| `ENGINE_VERIFY(expr, msg)` | debug builds (the expression runs in every build) | report, then abort | a call that must run for its effect and whose result should be true |
| `ENGINE_CHECK_SLOW(cond, msg)` | debug builds with slow checks; the condition is **not evaluated** otherwise | report, then abort | a check too expensive to leave in |
| `ENGINE_UNREACHABLE(msg)` | debug builds; a compiler hint otherwise | report, then abort (a handler cannot continue) | a `default:` that cannot happen |
| `ENGINE_NOT_IMPLEMENTED()` | always | report, then abort | code not written yet |

Debug builds are those without `NDEBUG` (the CMake Debug configuration). `-DENGINE_FORCE_DEBUG_CHECKS=ON` and
`-DENGINE_FORCE_SLOW_CHECKS=ON` keep the verify, unreachable and slow checks in every configuration.

A failure goes to **one handler**. The default prints a single line, `ASSERT FAILED: file:line function: condition:
message` (the leading words depend on the macro; at most 512 bytes), then aborts with `abort()` (so the process dies
with `SIGABRT` and a crash handler or debugger sees it), breaks into an attached debugger, or, for `ENGINE_ENSURE` only,
carries on. [[SetAssertHandler]] replaces it; a test uses that to see failures without dying:

```cpp compile standalone
#include "log/assert.h"

using namespace engine_placeholder;

namespace {

int g_failures_seen = 0;

AssertAction CountAndContinue(const AssertInfo& info) {
    (void)info; // info.kind, file, line, function, condition and message say what failed
    ++g_failures_seen;
    return AssertAction::Continue;
}

} // namespace

// Returns how many failures the checks inside produced; the program does not abort.
int FailuresSeenByChecks() {
    const AssertHandler previous = SetAssertHandler(&CountAndContinue);
    ENGINE_ASSERT(1 + 1 == 3, "arithmetic broke");
    const bool survived = ENGINE_ENSURE(g_failures_seen < 0, "this is reported once, the program goes on");
    SetAssertHandler(previous);
    return survived ? 0 : g_failures_seen;
}
```

`ENGINE_ENSURE` keeps a record per call site: [[ForEachFailedEnsure]] lists every site that has failed with its count.
`AssertFail` is the older entry point; it reports through the same handler and then always aborts.

## Already known — seed material (verify against the code when writing)

- Named channels (LogCore, LogRender, LogPlatform, LogJobs) with per-channel verbosity, conservative default Warn
- Config file override via LoadConfig; `--log-level` flag on the hello-window binary
- `ENGINE_ASSERT` is always enabled in every build and aborts (`SIGABRT`) with file/line/function/condition/message
- No allocator is needed for logging

## Outline

### Channels and verbosity

_TODO_

### Configuring from a file and the command line

_TODO_

### ENGINE_ASSERT semantics

_TODO_

### Writing good messages

_TODO_

### Build id and error traces

A program that calls [[debug::StartProgramDebugging]] logs one `build <id> <platform> <config>` line on `LogCore` first, so every
log names the binary that wrote it. `SetLogErrorTraces(true)` adds a short stack trace under every `Error` line (the output of
other lines does not change); the same call turns it on in Debug builds. Traces are in mangled form: pipe through `c++filt`.

### Adding a channel

_TODO_

