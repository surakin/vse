# Foundation: Debugging

> **Status:** draft — Phase 6 Day 92 (death tests and fault injection); the stack-trace, crash-handler and dump sections arrive on later days  
> **Audience:** engine programmers  
> **Covers:** Phase 6

## Source of truth

- `engine/foundation/debug/crash_reason.h`
- `engine/foundation/debug/fault_injection.h`
- `engine/foundation/debug/death_test.h`
- `tests/debug/` (the tests, and `tests/debug/BACKEND-CONFORMANCE.md` once Day 101 writes it)

## API reference

Full reference: [Foundation: Debugging](../reference/foundation-debug.md).

- [[debug::CrashReason]] — why a process or thread was stopped abnormally, independent of the operating system
- [[debug::FaultKind]] and [[debug::InjectFault]] — cause a deliberate fault in a test
- [[debug::RunInChild]] — run a registered function in a child process and see what happened
- [[debug::ChildResult]] — the exit code, the fault, the timeout and the captured output of a child

## Testing code that crashes

A crash kills the process that crashes, so a test cannot simply call code that is supposed to crash. A **death test**
runs that code in a child process and reports what happened to it.

1. Define the function the child will run with `ENGINE_DEATH_TEST_FN(name) { ... }`. It receives one argument string
   (`argument`) and is registered under its own name.
2. In the test, call [[debug::RunInChild]] with that name and look at the [[debug::ChildResult]]: did the child exit
   (`exited`, `exit_code`), die from a fault (`terminated`, `reason`), or get killed at the timeout (`timed_out`)? What
   did it write to standard output and error?
3. Cause the fault with [[debug::InjectFault]] and compare `reason` with a [[debug::CrashReason]]. Tests never name a
   signal or an exception code, so the same test sources run unchanged on every platform.

```cpp compile standalone
#include "debug/death_test.h"
#include "debug/fault_injection.h"

using namespace engine_placeholder::debug;

ENGINE_DEATH_TEST_FN(write_through_null) {
    InjectFault(FaultKind::NullWrite);
}

bool NullWriteIsAnAccessViolation() {
    const ChildResult result = RunInChild("write_through_null");
    return result.supported && result.terminated && result.reason == CrashReason::AccessViolation;
}
```

### How the child is started

The helper does not call `fork`. It starts a fresh copy of the running program and tells it, through the environment,
which registered function to run. This matters for two reasons: `fork` does not exist on Windows, and after a `fork` in a
program with several threads the child holds locks that no thread can ever release. A fresh process has none of the
parent's threads, locks or memory.

For Catch2 test programs, call `engine_enable_death_tests(<target>)` in the target's CMake: it adds a hidden test case
(tag `[.death-child]`) that is the child's entry point. A program that is not built on Catch2 calls
[[debug::RunDeathTestChildIfRequested]] at the start of `main` and passes the arguments that make it do so in
`ChildOptions::arguments`.

What the helper takes care of, so tests do not have to: the child's output cannot fill a pipe and block it (the capture is
drained while the child runs and cut at [[debug::kChildCaptureBytes]], with a `truncated` flag); a child that hangs is
killed at the timeout together with anything it started; core dumps are turned off in the child by default; fatal faults
reach the exit status even in the ASan and TSan flavors (their own handlers are told to stand aside); every path reaps the
child and closes every descriptor it opened.

### Platforms

| Platform | How the child is started | Notes |
|---|---|---|
| Linux | `posix_spawn` of `/proc/self/exe` | implemented (Phase 6) |
| macOS | `posix_spawn` | Phase 9; the POSIX code is shared with Linux |
| Windows | `CreateProcess` | Phase 8; exit statuses such as `0xC0000005` map to a [[debug::CrashReason]] |
| iOS, consoles | not possible | [[debug::DeathTestsSupported]] is false and death tests skip with a printed reason |

A fault shows up as an exit status, and a stack overflow is an access violation as far as the status can tell, so a death
test that wants [[debug::CrashReason::StackOverflow]] checks for [[debug::CrashReason::AccessViolation]].

## Already known — seed material (verify against the code when writing)

- `AssertFail` ends with an exit code today and becomes an `abort()` on Day 93; the crash handler (Days 95-97) builds on that.

## Outline

- Capturing and reading a stack trace (Day 94)
- Installing the crash handler, and what is never sent (Day 95)
- What a crash report contains (Day 96)
- When the program crashes (Day 97)
- Core dumps (Day 98)
- Turning a dump into a trace (Day 99)
