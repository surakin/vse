# Foundation: Debugging

> **Status:** draft — Phase 6 Day 92 (death tests and fault injection); the stack-trace, crash-handler and dump sections arrive on later days  
> **Audience:** engine programmers  
> **Covers:** Phase 6

## Source of truth

- `engine/foundation/debug/crash_reason.h`
- `engine/foundation/debug/fault_injection.h`
- `engine/foundation/debug/death_test.h`
- `engine/foundation/debug/stack_trace.h`, `module_list.h`, `debugger.h`, `compiler.h`
- `tests/debug/` (the tests, and `tests/debug/BACKEND-CONFORMANCE.md` once Day 101 writes it)

## API reference

Full reference: [Foundation: Debugging](../reference/foundation-debug.md).

- [[debug::CrashReason]] — why a process or thread was stopped abnormally, independent of the operating system
- [[debug::FaultKind]] and [[debug::InjectFault]] — cause a deliberate fault in a test
- [[debug::RunInChild]] — run a registered function in a child process and see what happened
- [[debug::ChildResult]] — the exit code, the fault, the timeout and the captured output of a child
- [[debug::CaptureStackTrace]], [[debug::ResolveStackTrace]], [[debug::FormatStackTrace]] — the call stack, with names, files and lines
- [[debug::InitDebugFacilities]] — prepare the stack-trace facilities at program start

## Capturing and reading a stack trace

[[debug::CaptureStackTrace]] records the return addresses of the current thread (up to [[debug::kMaxStackFrames]], innermost
first); [[debug::ResolveStackTrace]] turns them into function, file, line and module; [[debug::FormatStackTrace]] writes them
as text. They use libbacktrace behind the interface (`PLAN.md` assumption #20), which walks the stack with the compiler's own
unwinder, so the program does **not** need frame pointers, and which reads the DWARF debug information of the executable
and of the libraries it loaded.

```cpp compile standalone
#include "debug/stack_trace.h"
#include "os/os.h"

using namespace engine_placeholder;

// Writes the call stack of the caller to standard output.
void PrintMyCallStack() {
    debug::InitDebugFacilities(); // once, at program start; see below
    debug::StackTrace trace;
    if (!debug::CaptureStackTrace(trace)) {
        return;
    }
    char text[4096];
    debug::FormatOptions options;
    options.demangle = true; // readable C++ names; allocates, so not for a signal handler
    const usize length = debug::FormatStackTrace(trace, text, sizeof(text), options);
    os::Write(1, text, length);
}
```

Each line has a fixed layout so tools can read it: `#N  0xADDRESS  function  file:line`. A function inlined into another
repeats the frame number and ends with ` [inlined]`; `file:line` is left out when the binary has no debug information (build
with `-g`; the default Debug configuration has it); when even the function name is unknown the line shows the module and
the offset (`/lib/libfoo.so+0x1234`), which `addr2line -e /lib/libfoo.so 0x1234` turns into a file and line.

What to know:

- **Call [[debug::InitDebugFacilities]] at program start.** It reads the executable's debug information and the list of
  loaded libraries, and takes and resolves one trace to prime the unwinder. After it, capturing, resolving and formatting
  (without `demangle`) never call `malloc` (the library maps memory with `mmap` when it first reads the debug information
  of another part of the program), so a crash handler can use them. They do take the library's internal lock, so a fault
  inside them on the same thread cannot be survived. A trace taken without the call does that work on first use. Reading
  the debug information of a large binary can take a noticeable moment, once.
- **Libraries loaded later are not symbolized** until [[debug::ReloadDebugModules]] runs; their frames show as `??`.
- **An assertion failure prints a trace.** A failed `ENGINE_ASSERT` and the first failure of each `ENGINE_ENSURE` site print
  the trace under the message, starting at the failing function, in one write. The names are left mangled (demangling
  allocates, and an assertion can fail inside the allocator): pipe the output through `c++filt` to read them.
- **Only DWARF.** Binaries built for Windows with PDB debug information will need a different resolver (Phase 8).

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

- A failed `ENGINE_ASSERT` ends the process with `abort()` (Day 93) after printing its stack trace (Day 94), so it dies with `SIGABRT`; the crash handler (Days 95-97) builds on that. See [Foundation: Logging and Assertions](foundation-logging-and-assert.md) for the macros.

## Outline

- Installing the crash handler, and what is never sent (Day 95)
- What a crash report contains (Day 96)
- When the program crashes (Day 97)
- Core dumps (Day 98)
- Turning a dump into a trace (Day 99)
