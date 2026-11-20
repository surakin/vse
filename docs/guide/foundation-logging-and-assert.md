# Foundation: Logging and Assertions

Status: written (Phases 0, 7 and 11).

**Audience:** every engine programmer. Two tools report what the program is doing and what went wrong: **logging** for things a human reads, and
**assertions** for conditions the code relies on. Read [Strings](foundation-strings.md) first if `StringView` is new to you.

## Source of truth

- `engine/foundation/log/log.h`
- `engine/foundation/log/assert.h`

## API reference

Full reference: [Foundation: Logging and Assertions](../reference/foundation-log.md).

- [[LogChannel]] — a named, filterable log channel
- [[LogVerbosity]] — verbosity levels
- [[Log]] — write a message to a channel
- [[SetAssertHandler]], [[AssertInfo]], [[AssertAction]] — the handler every failed check goes to
- [[ENGINE_ASSERT]], [[ENGINE_ENSURE]], [[ENGINE_VERIFY]], [[ENGINE_CHECK_SLOW]], [[ENGINE_UNREACHABLE]], [[ENGINE_NOT_IMPLEMENTED]]

## Logging

### Channels and verbosity

Log lines go to a **channel**: a named source with its own threshold. Channels let you turn up one subsystem (say, the renderer) without drowning in
everything else. The engine defines `LogCore`, `LogRender` and `LogPlatform` in the foundation, and modules add theirs (`LogJobs`, `LogRhi`, `LogIo`, `LogCrash`, `LogBuild`).

A message has a **severity**, and a channel prints it when the severity is at least as important as the channel's verbosity:

| Verbosity | Use it for |
|---|---|
| `Error` | a failure the program cannot ignore (a file that must exist is missing) |
| `Warn` | something suspicious that the program handled (the default threshold of every channel) |
| `Info` | normal progress (a window opened, a backend chosen) |
| `Debug` | developer diagnostics |
| `Trace` | very fine-grained, high-volume output |

```cpp compile run standalone
#include "log/log.h"
#include "os/console.h"

using namespace vse;

int main() {
    Log(LogCore, LogVerbosity::Warn, "this prints: Warn is the default threshold");
    Log(LogCore, LogVerbosity::Info, "this does not: Info is below the threshold");

    LogCore.SetVerbosity(LogVerbosity::Info);               // turn this channel up
    Log(LogCore, LogVerbosity::Info, "now it does");

    LogCore.SetEnabled(false);                              // or silence it
    Log(LogCore, LogVerbosity::Error, "never shown");
    os::Print("enabled: %s\n", LogCore.Enabled() ? "yes" : "no");
    return 0;
}
```

```output
[LogCore] WARN: this prints: Warn is the default threshold
[LogCore] INFO: now it does
enabled: no
```

`Log` takes a finished message, not a format string: build it with a `StringBuilder` (see [Strings](foundation-strings.md)) when it has numbers in it. The
line, with the channel and severity, is written in one piece to standard output and is cut at 512 bytes. Checking `channel.ShouldLog(level)` first avoids building a
message that would be thrown away, which matters for `Debug` and `Trace` messages in hot code.

The last 256 lines are also kept in memory, so a crash report can show what the program said just before it died.

### Configuring from the command line and files

Channel verbosity and enabled state are **settings** ([Settings](settings.md)): `log.<channel>.verbosity` and `log.<channel>.enabled`, with the channel name in lower case. So you never write code to read
them; use whatever the settings system offers, for example `--set log.logrender.verbosity=Info`, a settings file or the environment variable
`ENGINE_SETTING_LOG__LOGRENDER__VERBOSITY`. Programs such as `hello_window` also take a plain `--log-level=info` that sets every channel.

### Adding a channel

Define a global in one `.cpp` file and declare it `extern` in a header the module's code includes:

```cpp
// my_module_log.h
extern LogChannel LogMyModule;

// my_module.cpp
LogChannel LogMyModule("LogMyModule", LogVerbosity::Warn);   // the name must outlive the channel: use a literal
```

Constructing the channel registers it, so the settings system finds it and creates the two settings above. Channels are meant to live for the whole
program: never put one on the stack or destroy it while code can still log to it. They are not thread-safe to *change*; set verbosity at startup.

### Writing good messages

- Say **what** failed and **with what**: "cannot open 'level1.mesh': not found" beats "load failed".
- One line, no trailing newline (the logger adds it), no decoration; the channel and severity are already in front.
- Pick the severity by who should care: `Error` is for failures someone must fix, `Warn` for handled surprises, and everything chatty is `Debug` or `Trace`.
- Do not log inside the innermost loop of a frame at `Info`; one line per frame at 60 frames per second is 3,600 lines a minute.

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

using namespace vse;

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

## More

### Build id and error traces

A program that calls [[debug::StartProgramDebugging]] logs one `build <id> <platform> <config>` line on its own `LogBuild` channel first, so every
log names the binary that wrote it. `SetLogErrorTraces(true)` adds a short stack trace under every `Error` line (the output of
other lines does not change); the same call turns it on in Debug builds. Traces are in mangled form: pipe through `c++filt`.

### Who completes an assert report

`log` knows nothing about stack traces: the default handler prints the message line and, with nothing else installed, aborts (an ensure prints and
continues). `debug::InstallDiagnosticsHooks`, which `debug::InitDebugFacilities` (and so `StartProgramDebugging`) calls, installs a
`FailureReportHooks` table (`log/diagnostics_hooks.h`) that adds the stack trace, the crash context and a look at the debugger. New threads announce
themselves with `NotifyThreadStart(name)`; the same call installs crash-handler registration. A program that never starts the debugging facilities gets
the bare message, which is complete on its own.

## Common mistakes

- **Using `ENGINE_ASSERT` for something that can legitimately happen** (a missing file, bad user input). Return a failure value; assertions are for bugs.
- **Putting side effects in `ENGINE_CHECK_SLOW`.** In release builds the condition is not evaluated at all. Use `ENGINE_VERIFY` when the expression must run.
- **Writing `ENGINE_ASSERT(Load(file), ...)` and expecting the load to happen elsewhere.** `ENGINE_ASSERT` always evaluates its condition, but keep it a *check*, not the work.
- **Relying on `ENGINE_ENSURE` to stop execution.** It reports once and returns `false`; the code after it must handle the failure.
- **Printing with `printf` or `std::cout`.** Use `Log` (diagnostics) or `os::Print` (program output).
- **Logging from a signal handler or a crash path.** Only the functions marked async-signal-safe may be called there ([Debugging](foundation-debugging.md)).

## See also

[Debugging](foundation-debugging.md) (what happens after a failure), [Settings](settings.md), [Strings](foundation-strings.md); reference:
[Foundation: Logging and Assertions](../reference/foundation-log.md).
