# Foundation: Debugging

> **Status:** complete for Phase 7 (Linux implemented; Windows and macOS are ready, see [porting](porting-debugging.md))  
> **Audience:** engine programmers  
> **Covers:** Phase 7

## Debugging a crash: a checklist

You have a crash report on the screen, a `.envelope` in the dump directory, or a core. In this order:

1. **Read the report.** It starts with the reason and the faulting thread (`thread: 12345 "worker-2"`), then the stack trace.
   The first frame is the function that faulted. Under it: the build id, the platform, the engine state (`gpu:`, `frame:`,
   `window:`) and the last 32 log lines. See "What a crash report contains".
2. **No names or no lines?** A Release program has names but no file and line; a stripped one has neither. Use a
   RelWithDebInfo build, or resolve with the symbol store: "Turning a dump into a trace".
3. **Need more than the report?** `scripts/crash-report.sh <envelope> <program>` prints every thread and the modules from the
   minidump (and refuses the wrong build). For heap or object state ask for a core: "Core dumps on Linux".
4. **Is it the right build?** Compare `build id:` in the report with `readelf -n <program>`; the scripts do it for you.
5. **Reproduce it.** `--crash-test=...` in any demo shows the path works; a test that expects a crash is in "Testing code that
   crashes". Make the failing case a test before you fix it.
6. **Report, don't upload.** Nothing the engine does ever sends a dump anywhere. A dump and a core hold process memory: keep
   them private.

## Source of truth

- `engine/foundation/debug/`: `crash_reason.h`, `fault_injection.h`, `death_test.h`, `stack_trace.h`, `module_list.h`, `debugger.h`,
  `compiler.h`, `crash_handler.h`, `crash_context.h`, `crash_report.h`, `crash_state.h`, `post_mortem.h`, `program_setup.h`,
  `thread_info.h`
- `engine/foundation/log/log.h`, `assert.h` (the assert family and the recent-log ring)
- `engine/foundation/os/linux/debug/` (the Linux backend) and `os/windows/debug/BACKEND.md`, `os/macos/debug/BACKEND.md`
- `third_party/libbacktrace/VENDORED.md`, `third_party/sentry_native/VENDORED.md`; `tools/crash_extract/`; `tools/signal_safety_check/`
- `scripts/`: `crash-report.sh`, `store-symbols.sh`, `split-debug.sh`, `find-core.sh`, `open-core.sh`
- `tests/debug/` (the tests, and `tests/debug/BACKEND-CONFORMANCE.md`)

## API reference

Full reference: [Foundation: Debugging](../reference/foundation-debug.md) and [Foundation: Logging](../reference/foundation-log.md).

- [[debug::StartProgramDebugging]], [[debug::RunCrashTest]] — what every program calls first
- [[debug::CaptureStackTrace]], [[debug::ResolveStackTrace]], [[debug::FormatStackTrace]] — the call stack, with names, files and lines
- [[debug::InitDebugFacilities]] — prepare the stack-trace facilities at program start
- [[debug::InstallCrashHandler]], [[debug::UninstallCrashHandler]], [[debug::CrashHandlerOptions]], [[debug::RegisterThreadForCrashHandling]]
- [[debug::WriteCrashReport]], [[debug::WriteCrashContext]], [[debug::RegisterCrashStateProvider]], [[debug::SetCrashStateText]], [[debug::CrashStateCounter]]
- [[debug::EnablePostMortemDumps]] — core dumps
- [[debug::CrashReason]], [[debug::FaultKind]], [[debug::InjectFault]], [[debug::RunInChild]], [[debug::ChildResult]] — testing code that crashes
- [[io::CrashDumpDirectory]], [[io::CreatePrivateDirectory]] — where dumps go

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
- **Only DWARF.** Binaries built for Windows with PDB debug information will need a different resolver (Phase 13).

## Installing the crash handler

[[debug::InstallCrashHandler]] makes the process leave a **minidump** on disk when it dies from a fault (a null or wild
pointer, an illegal instruction, a divide by zero, a stack overflow, an `abort`). A minidump holds every thread's registers
and stack memory and the list of loaded modules; `lldb -c <dump>` and `minidump-stackwalk` open it. After writing the dump
the process dies from the original fault, so its exit status, a core dump and a debugger all behave as before.

```cpp compile standalone
#include "debug/crash_handler.h"
#include "debug/stack_trace.h"

using namespace engine_placeholder;

// Call once, early in main.
void SetUpCrashHandling() {
    debug::InitDebugFacilities();
    debug::CrashHandlerOptions options;
    options.release = "mygame 1.2.3"; // recorded with every dump
    // options.dump_directory is left null: the default directory under the user's state directory is used.
    const debug::CrashHandlerResult result = debug::InstallCrashHandler(options);
    if (result != debug::CrashHandlerResult::Installed) {
        // The program runs on without a handler; the reason was logged as a warning.
    }
}
```

The library is sentry-native with its Breakpad backend, vendored in `third_party/sentry_native/` (`PLAN.md` assumption #20;
the choice and the numbers are in `docs/superpowers/specs/2026-11-06-debugging-design.md`). It runs inside the process, so
there is no helper program to ship next to the game.

- **Install it once, early.** Programs do it through [[debug::StartProgramDebugging]]. The engine puts one small recorder in front
  of the library's handler (to learn the fault address and the faulting thread, which the library does not tell its hook on Linux)
  and always calls the library afterwards. A handler a program installs for the same signals *after* this call must call the one
  it replaced, or the dump is not written.
- **A failure is not fatal.** If the dump directory cannot be created or the library cannot start, `InstallCrashHandler`
  returns the reason (`DirectoryFailed`, `InitFailed`), logs a warning on the `LogCrash` channel, and the program runs without
  a handler. `Disabled` is what a program returns to itself when configuration switches the handler off.
- **Dumps survive a restart.** Right after a crash the envelope sits in a `<uuid>.run/` folder inside the dump directory.
  The next time the program installs the handler, the library moves it into `<dump directory>/saved/<uuid>.envelope` and
  removes the run folder. When the handler is installed the oldest saved files beyond `CrashHandlerOptions::keep_saved_dumps` (20 by default, 0 keeps all) are deleted ([[debug::PruneSavedDumps]]).
- **Turning a dump into a trace.** The library wraps the minidump in an *envelope* file; `crash_extract <file>.envelope`
  (built as `tools/crash_extract`) writes the plain `.dmp` next to it.

### Where dumps go, and what is never sent

Dumps go to a directory on the local disk, never the working directory (a game started from a read-only or sandboxed
directory could not write there): `$XDG_STATE_HOME/<app>/crashes`, or `~/.local/state/<app>/crashes`, where `<app>` is the
environment variable `ENGINE_APP_NAME` or the executable's name (a name that is `.`, `..`, has a slash, or is very long is
refused). The directory must be a real directory (not a symbolic link) owned by the user, is created private to them
(mode 0700), is tightened to 0700 if it exists open to others, and must be writable; otherwise the handler is not
installed. A program can name another directory in the options; the same rules apply to it. A dump contains the memory of the process at the moment it crashed,
including whatever the program held in memory, so treat the files as sensitive.

**Nothing is ever sent anywhere.** The library is built with no network transport, so no upload code exists in the program;
the only "transport" it is given is a function that copies an envelope into the `saved/` folder. The options have no field
for an address or an account, and ctests (`debug_no_network_*`) fail if a test binary lists a network or TLS library or
imports their functions (the demos and benches that install the handler are added to that check when they are wired in). Sending a dump, if a project ever wants
to, is its own tool that reads the file.

## What a crash report contains

A crash report (and the extra output of a failed `ENGINE_ASSERT`, `ENGINE_VERIFY`, or `ENGINE_UNREACHABLE`) has the same
sections, always in this order:

```text
=== CRASH REPORT ===
reason: AccessViolation at 0x0000000000000010
thread: 12345 "render"
--- stack trace ---
#0  0x... engine::Foo(int) (file.cpp:42)
--- build ---
build id: 3f2a...
--- platform ---
os: Linux 7.2 x86_64
cpu: <model>, 16 cores, 32768 MiB
--- state ---
gpu: Test GPU 9000, driver 1.2.3
--- recent log (oldest first, last 32) ---
...
=== END OF REPORT ===
```

A failed `ENGINE_ENSURE` prints only its message and trace, because the program goes on.

The build id names the exact binary, so a report and a minidump can be matched to its symbols. Call
`debug::InitCrashContext(argc, argv)` once at startup (the crash handler does it for you if you have not).

### Adding your subsystem's state

Register a provider during startup with `debug::RegisterCrashStateProvider("gpu", &fn, context)`. The table holds 16
entries and never shrinks. A provider runs inside a crashing process, so it may only read memory that is always valid
(atomics or copies the subsystem keeps current), must not allocate, lock, or touch files, and must write no more than the
`capacity` it is given. Text longer than `kCrashStateTextBytes` is cut.

### The recent-log ring

Every line sent through `Log` is also kept in a lock-free ring of the newest 256 lines (256 bytes each, longer lines are cut).
Writers never wait; a reader skips a slot being overwritten. `log::ReadRecentLogLines` returns them oldest first, and the
report shows the last 32.

## Starting a program

Every program that ships calls [[debug::StartProgramDebugging]] first thing in `main` and [[debug::RunCrashTest]] right after it:

```text
const debug::ProgramDebugSetup setup = debug::StartProgramDebugging(argc, argv);
debug::RunCrashTest(setup.crash_test);
```

It prepares the stack-trace facilities and the crash context, logs the build id, and installs the crash handler **if wanted**:
`--crash-handler=on|off` wins, then the environment variable `ENGINE_CRASH_HANDLER` (`1`/`0`), then the build's default, which is
**on in Debug and RelWithDebInfo and off in Release**. A program with no command line (mobile, console) passes `0, nullptr` and
uses the environment and the default. A program with its own argument parser skips these arguments with
[[debug::IsProgramDebugArgument]].

`--crash-test=segv|abort|assert|ensure` makes the program cause that fault right away, before it opens a window or a device,
for checking the whole crash path by hand (the demos and benches all take it):

```text
rhi_bench --crash-handler=on --crash-test=segv
```

### Release builds

A Release program with the handler on (`--crash-handler=on`) still produces the report, the dump and the original signal. Its
trace has function names (the symbol table is not stripped) but no file and line numbers (no `-g`), and tail-called and inlined
functions are missing from it. For readable traces from a Release program use RelWithDebInfo, or keep the debug information of
every release you ship in a symbol store and read the dump with `scripts/crash-report.sh` ("Keeping symbols for release builds").

### Adding your subsystem's state, the short way

You do not need a provider function for a line of text or a counter: [[debug::SetCrashStateText]] sets a line once (the RHI does
this for the adapter, the window layer for its backend) and [[debug::CrashStateCounter]] gives a counter to store into (the
renderer keeps the frame number there). Use [[debug::RegisterCrashStateProvider]] when the state must be computed.

## When the program crashes

With the crash handler installed ([[debug::InstallCrashHandler]]), a fatal fault (a bad memory access, an illegal
instruction, an arithmetic trap, an abort, a stack overflow) does four things, in this order, on the thread that faulted:

1. **The report** ("What a crash report contains" above) is written to standard error and to `crash-report.txt` in the dump
   directory. It names the faulting thread, so a crash on a worker is not mistaken for one on the main thread.
2. **The library writes the minidump**: every thread, the registers, the stack memory and the module list. The dump, the
   report file and the release string travel together in one `.envelope` file under the dump directory.
3. **The original fault is delivered again**, so the process ends with the signal that killed it (`SIGSEGV`, `SIGABRT`, ...)
   and a debugger or the shell sees the real cause.
4. A failed `ENGINE_ASSERT` prints its own message and context first and then aborts, so the crash report follows it (reason
   `Abort`); the file and the dump are written as for any other crash.

If the report itself faults (a state provider that reads freed memory, say), the process prints `double fault` and carries on
to step 2, so the dump is not lost. If two threads fault at once, one report is written. A report is cut off after ten
seconds between sections; a provider that never returns is not covered (see KNOWN-ISSUES).

The engine puts one small signal handler in front of the library's, only to learn the fault address and the faulting thread
(the library's Linux backend tells its hook neither), and always calls the library's handler afterwards.

### Stack overflow and other threads

A stack overflow needs a separate stack to handle it on. [[debug::InstallCrashHandler]] sets one up for the thread that
installs the handler; call [[debug::RegisterThreadForCrashHandling]] at the start of every other thread that may recurse
deeply. Without it a stack overflow on that thread kills the process with no report and no dump (other faults still work).

### If your program has its own signal handler

Install it **after** `InstallCrashHandler` and call the handler that was there before it (the one `sigaction` returns). A handler
installed before is replaced by the library's and never runs. `UninstallCrashHandler` removes ours and puts the library's
predecessor back.

### Reading the exit status

A shell shows a program killed by a signal as 128 plus the signal number (`139` is `SIGSEGV`, `134` is `SIGABRT`).
[[debug::RunInChild]] reports it as `ChildResult::native_code` and maps it to a [[debug::CrashReason]].

### Opening a dump

`crash_extract` pulls the minidump out of the envelope; `lldb` reads it directly:

```text
crash_extract <dump directory>/<run>/<id>.envelope crash.dmp
DEBUGINFOD_URLS= lldb --no-lldbinit -b -c crash.dmp -o 'thread list' -o 'bt all' -o 'image list'
```

`thread list` shows the faulting thread with its stop reason, `bt all` every thread's stack, and `image list` the modules with
their build ids (compare the first 16 bytes with the `build id:` line of the report). Setting `DEBUGINFOD_URLS` empty keeps
the debugger from going to the network to look for symbols.

### Sanitizer builds

| Build | The crash handler | What to expect |
|---|---|---|
| plain, Release | works | report, dump, original signal |
| ASan, UBSan | works when the sanitizer's own fault handling is off (`handle_segv=0:handle_abort=0`, which the death-test child sets) | otherwise the sanitizer's report replaces ours |
| TSan | works; a fault inside the report path loses the `double fault` message and the dump | see KNOWN-ISSUES |

## Core dumps on Linux

The minidump the crash handler writes is small and always on. A **core dump** is the kernel's copy of the whole process
memory: bigger, off by default, and the right thing when you want to walk the heap in `gdb`. Ask for one with
`CrashHandlerOptions::post_mortem_dumps` (or call [[debug::EnablePostMortemDumps]] yourself). It raises this process's core size
limit to the hard limit, makes the process dumpable, and puts a `core dumps:` line in every crash report saying where the core
goes. If the system's hard limit is 0 the process cannot raise it; start it from a shell after `ulimit -c unlimited`.

Where the core goes is the system's choice (`/proc/sys/kernel/core_pattern`):

| core_pattern | Where the core is | How to open it |
|---|---|---|
| starts with `|` and names `systemd-coredump` (most distributions) | stored by the system | `coredumpctl list`, then `coredumpctl gdb <pid>` or `scripts/open-core.sh <program>` |
| a file name pattern such as `core.%e.%p` | a file in the working directory of the crash | `gdb <program> <core>` or `scripts/open-core.sh <program>` |
| starts with `|` and names another program | wherever that program puts it | see that program |

Changing `core_pattern` needs root (`sudo sysctl kernel.core_pattern=...`); the engine never does it. Two scripts (Linux shell
scripts; Windows has its own tooling in Phase 13) take the work out of the common case:

```text
scripts/find-core.sh  [--pid PID] [--wait SECONDS] [--output FILE] <program>
scripts/open-core.sh  [--pid PID] [--wait SECONDS] <program> [<core>]
```

`find-core.sh` finds the newest core of the program (through `coredumpctl` or the pattern), and **checks that the core's recorded
build id equals the program's** (a core from another build gives wrong symbols); it exits with 3 if they differ. `open-core.sh`
runs `gdb` in batch mode and prints `bt` and `thread apply all bt`. They tell `gdb` not to ask a debuginfod server for
symbols unless `ENGINE_ALLOW_DEBUGINFOD` is set.

Things to know: a core can be as large as the process (the system may refuse or truncate it; `coredumpctl` keeps them under
`/var/lib/systemd/coredump` until they age out); **a core contains everything the process had in memory, including keys and
passwords**, so do not attach one to a bug report or leave it in a shared directory; and every crashing program on a
`systemd-coredump` machine leaves a journal entry (visible in `coredumpctl list`) even when no core is stored.

## Turning a dump into a trace

`scripts/crash-report.sh` (a Linux shell script) takes a minidump or the `.envelope` a crash leaves in the dump directory:

```text
scripts/crash-report.sh <dump directory>/<run>/<id>.envelope  <program>  [<symbol store>]
```

It pulls the minidump out of an envelope with `crash_extract`, then prints the faulting thread, every thread and the modules
with their build ids using `lldb` (and, if `minidump-stackwalk` is installed, a second symbolized trace). If you give the
program, its build id is compared with the one in the dump (with `lldb`; `minidump-stackwalk` alone cannot do it and the script warns) and a different build is
refused with exit status 3, because the trace would be wrong. With neither tool installed it exits with status 66 and says what to install. The commands it runs
for you are `lldb -b -c crash.dmp -o 'thread list' -o 'bt all' -o 'image list'`, with debuginfod switched off.

To resolve a raw `module+offset` from a report by hand:

```text
llvm-symbolizer --obj=<module file> 0x<offset>
addr2line -f -C -e <module file> 0x<offset>
```

## Keeping symbols for release builds

A release program is stripped, so its dumps have no function names unless you kept the debug information of **that build**.
Configure with `-DENGINE_SPLIT_DEBUG_INFO=ON` (off by default; the demos and benches are then stripped in place, so run an unstripped copy under
valgrind) to build with `-g` and have the demos and benches split after linking
(`scripts/split-debug.sh`: `<program>.debug` next to a stripped program that keeps its GNU build id and links to the debug file).
For every build you ship, file the debug information in a symbol store:

```text
scripts/store-symbols.sh build/game/rhi_demo/rhi_demo.debug  symbols
```

This writes `symbols/<name>/<build-id>/<name>.debug` and a link under `symbols/.build-id/` (the layout `lldb` and `gdb` search by
build id). Keep the store as long as you keep the releases. `scripts/crash-report.sh` takes the store as an argument and so the
stripped program's dump is read with names and lines. The `.debug` files are as large as the unstripped program: store them
privately, not next to the download.

Breakpad `.sym` files (for `minidump-stackwalk`) are produced by `dump_syms` from the same `.debug` files; the script looks for
them in `<store>/breakpad` (a directory you create; `store-symbols.sh` does not). That path is documented but not exercised on the development machine (see KNOWN-ISSUES).

## Assert, ensure or verify

Choose by what the program can do after the failure (the macros are in [Logging and Assertions](foundation-logging-and-assert.md)):

- `ENGINE_ASSERT(cond, msg)`: the condition is a precondition whose violation is a bug and nothing sensible can follow. It
  prints the message, the trace and the crash context, then aborts, so the crash handler adds the minidump.
- `ENGINE_ENSURE(cond, msg)`: the code can survive it (a missing optional resource, a value out of range that it clamps). It
  prints the message and trace **once per call site**, counts the rest, returns `false`, and the program goes on. Use it where
  you would otherwise write a silent `if (!x) return;`: the first failure is then visible.
- `ENGINE_VERIFY(expr, msg)`: like assert, but `expr` must run in every build (it has a side effect); the check is dropped in
  Release unless forced on.

A failed ensure leaves no dump and does not print the crash context (the program is fine); a failed assert does both.

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
| Linux | `posix_spawn` of `/proc/self/exe` | implemented (Phase 7) |
| macOS | `posix_spawn` | Phase 14; the POSIX code is shared with Linux |
| Windows | `CreateProcess` | Phase 13; exit statuses such as `0xC0000005` map to a [[debug::CrashReason]] |
| iOS, consoles | not possible | [[debug::DeathTestsSupported]] is false and death tests skip with a printed reason |

A fault shows up as an exit status, and a stack overflow is an access violation as far as the status can tell, so a death
test that wants [[debug::CrashReason::StackOverflow]] checks for [[debug::CrashReason::AccessViolation]].
