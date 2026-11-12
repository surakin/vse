# Foundation: Debugging

Status: written (Phase 7 on Linux; Phase 13 added Windows; macOS from Phase 26: stack traces and the crash context on Day 306, the crash handler and
death tests on Day 307, dumps on Day 308; see [porting](porting-debugging.md)).

**Audience:** every engine programmer who has to find out why a program crashed, or who writes a test for code that is supposed to crash. Read
[Logging and Assertions](foundation-logging-and-assert.md) first: assertions are where most crashes start.

When a program dies you want three things: *what* went wrong, *where* in the code, and *whether you are looking at the right build*. The
debugging facilities give you a **crash report** (text), a **minidump** (a small file holding every thread's registers and stack memory, which a
debugger such as `lldb` or WinDbg can open) and, if you ask, a **core dump** (the whole process memory). **Symbolication** means turning the raw
addresses in a trace into function names, files and lines; it needs the **debug information** of the exact build that crashed, and the **build
id** (a hash stored in the binary) is how a dump is matched to that build. Nothing here ever sends a dump anywhere.

## Something went wrong: where do I look?

| What happened | Look at | Section |
|---|---|---|
| A program crashed and left a report on the screen | the first frame of the stack trace and the `reason:` line | "What a crash report contains" |
| The report has no file and line numbers | build with RelWithDebInfo, or use a symbol store | "Release builds", "Keeping symbols for release builds" |
| I have an `.envelope` file and need every thread | `scripts/crash-report.sh` (Linux), `scripts/crash-report.ps1` (Windows) | "Turning a dump into a trace" |
| I need heap or object state | a core dump | "Core dumps on Linux" |
| An assertion fired | the message, then the trace under it | "Assert, ensure or verify" |
| No report and no dump at all | the handler was off (Release default), a stack overflow on an unregistered thread, or a Windows fast fail | "Installing the crash handler", "Stack overflow and other threads" |
| I want to test that code crashes | a death test | "Testing code that crashes" |
| I want a trace of my own, without a crash | [[debug::CaptureStackTrace]] | "Capturing and reading a stack trace" |

## At a glance

| I need | Use | Notes |
|---|---|---|
| to start a program with crash handling | [[debug::StartProgramDebugging]] first in `main` | handler on in Debug and RelWithDebInfo, off in Release |
| to cause a crash on purpose and check the path | `--crash-test=segv\|abort\|assert\|ensure` | all demos and benches take it |
| a stack trace of the current thread | [[debug::CaptureStackTrace]] + [[debug::FormatStackTrace]] | call [[debug::InitDebugFacilities]] once at start |
| my subsystem's state in the report | [[debug::SetCrashStateText]], [[debug::CrashStateCounter]] | a provider only when the text must be computed |
| a test for code that must crash | `ENGINE_DEATH_TEST_FN` + [[debug::RunInChild]] | the child is a fresh process, not a `fork` |
| a compiler builtin or attribute | a macro from `debug/compiler.h` | never write `__attribute__` yourself |

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
- **Linux reads DWARF (libbacktrace); Windows reads the PDBs through DbgHelp** (Phase 13 Day 154). DbgHelp is single-threaded,
  so every call takes one lock, and it is never used while a crash is being reported: a crash report on Windows records raw
  addresses and module offsets (`#0  0x00007ff6...  C:\path\program.exe+0x1a2b3`), and the names are found afterwards (by the crash
  script, a debugger or a test parent). On Windows
  the PDB stores function names readable already (`engine::Foo`, without the parameter list), so `demangle` changes nothing there;
  an MSVC-decorated name (`?Foo@@YAXH@Z`) passed to [[debug::DemangleSymbol]] is undecorated with `UnDecorateSymbolName`.

On **macOS** file and line come from the dSYM next to the binary: the build makes one for each test and program (`engine_dsym`, on by default as
`ENGINE_DSYM`; it needs `dsymutil` from the Command Line Tools). Without it a frame names its function only. Load a plug-in as a dylib (`SHARED`):
libbacktrace cannot read a bundle (`MODULE`). [Porting](porting-debugging.md) has the details.

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
  it replaced, or the dump is not written. **On Windows** the library writes the dump first and then the engine writes the report,
  from the library's own handler thread; the engine adds an observer (a vectored handler that only notes which thread raised each
  exception) and a gate filter (so a second thread crashing meanwhile waits instead of ending the process before the dump). A program
  that installs its own unhandled-exception filter after this call replaces the library's, so do not.
- **What no handler sees (Windows).** A fast fail (`__fastfail`, the `/GS` stack-cookie check, some C runtime checks) ends the process
  in the kernel with exit code `0xC0000409`: no report and no dump in the dump directory. Windows Error Reporting's local dumps
  (post-mortem dumps) are the only record such a crash leaves.
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
imports their functions (on Windows: imports one of `winhttp`, `wininet`, `ws2_32` and the like, or names one in its data) (the demos and benches that install the handler are added to that check when they are wired in). Sending a dump, if a project ever wants
to, is its own tool that reads the file.


### On macOS

The crash handler is the same library and the same report (Phase 26 Day 307), with these differences:

- **Mach exceptions, not signals.** Breakpad catches a fault as a Mach exception on its own thread while the crashed thread is suspended; an abort
  comes through its `SIGABRT` handler. The report is built from the crashed thread's registers, so it names that thread, its reason and its stack,
  and a stack overflow needs no alternate stack.
- **The process ends with an exit code.** After the dump Breakpad calls `_exit` with the Mach exception type (1 for a bad access): a parent sees an
  exit code, not a signal, and the system's crash reporter (`ReportCrash`) writes nothing to `~/Library/Logs/DiagnosticReports` for it. Without the
  handler a fault ends the process by signal and `ReportCrash` writes its report. On the development Mac a crashing child ended in about 260 ms with
  the handler (the dump) and 40 ms without.
- **Core files** are written to `/cores` only when it is writable by the user (`EnablePostMortemDumps` reports `Limited` otherwise, with the fix);
  see "Reading a crash on macOS" below.
- **The minidump needs one fix before `lldb` reads it.** Breakpad's macOS writer records every thread's registers but flags the context as
  holding none, so `lldb -c` shows each thread at pc 0. `crash_extract --lldb <envelope> crash.dmp` writes the dump with the flags completed;
  `scripts/crash-report.sh` does it for you.
- **Under a debugger** sentry installs no exception ports, so `lldb` sees the fault itself.
- **ASan:** the handler's object suppresses one ASan report, on Breakpad's copy of the crashed stack into the dump (`__asan_default_suppressions`).

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

Changing `core_pattern` needs root (`sudo sysctl kernel.core_pattern=...`); the engine never does it. Two scripts (shell scripts
for Linux and macOS; Windows has its own tooling, see "Reading a crash on Windows") take the work out of the common case:

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

`scripts/crash-report.sh` (a shell script for Linux and macOS) takes a minidump or the `.envelope` a crash leaves in the dump directory:

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

### Reading a crash on Windows

**Where the dumps are.** The crash handler's envelopes are in `%LOCALAPPDATA%\<app>\crashes` (the `<uuid>.run` folder of the last run,
then `saved\` once the program starts again). Windows Error Reporting keeps a dump of its own only where an administrator set up
`LocalDumps` under `HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps` (the same values under the user's own key are
ignored); [[debug::EnablePostMortemDumps]] says whether that is so, and never writes the registry. `scripts/find-dump.ps1 <app>` lists both
kinds, newest first.

**Keeping symbols.** For every build you ship, file the program and its PDB in a symbol store:

```text
powershell -File scripts\store-symbols.ps1 build-msvc\game\rhi_demo\rhi_demo.exe symbols
```

This writes `symbols\<name>.pdb\<GUID+age>\<name>.pdb` and `symbols\<name>.exe\<timestamp><size>\<name>.exe`, the layout WinDbg and
Visual Studio read with the symbol path `srv*symbols`. The GUID and age are the build id the crash report and the minidump record.

**Reading the crash.**

```text
powershell -File scripts\crash-report.ps1 <envelope> [<program>] [<symbol store>]
```

prints the exception, the modules with their build ids, and the engine's crash report with each `module+0x<offset>` frame named with its
function, file and line from the store (`crash_extract --resolve`, through DbgHelp, with the store as the only symbol path: nothing is
fetched from a symbol server). Given the program, it refuses a dump made by a different build (exit status 3).
`scripts\open-dump.ps1 <envelope> <symbol store>` opens the dump in WinDbg (or cdb with `-Batch`), with `_NT_SYMBOL_PATH` cleared so the
debugger stays offline too; without either debugger it exits with status 66 and says what to install.

### Reading a crash on macOS

**Where the dumps are.** The envelopes are in `~/Library/Application Support/<app>/crashes` (the `<uuid>.run` folder of the last run, then
`saved/`). With the crash handler installed, the system's crash reporter writes nothing for the crash (the process ends by `_exit`); without it,
`~/Library/Logs/DiagnosticReports` gets an `.ips` report.

**Keeping symbols.** The build writes a dSYM beside each program (`engine_dsym`). For every build you ship, file it in a symbol store:

```text
scripts/store-symbols.sh build-macos/game/rhi_demo/rhi_demo symbols
```

This writes `symbols/<name>/<UUID>/<name>.dSYM`, where the UUID is the program's `LC_UUID` as `dwarfdump --uuid` prints it (the build id the
crash report and the minidump record). It takes the dSYM beside the program when its UUID matches, or makes one with `dsymutil` (which reads the
build's object files: store before you clean). A universal program is filed once per architecture, each copy holding only that architecture's
DWARF (each has its own UUID). `scripts/split-debug.sh <program>` is for a program built without the dSYM: `dsymutil`, then `strip -S`, which
drops the debug map and keeps the symbol table, so a crash report still names functions, only without files and lines.

**Reading the crash.**

```text
scripts/crash-report.sh <envelope> [<program>] [<symbol store>]
```

completes the dump's register flags (`crash_extract --lldb`), loads each module's dSYM from the store with `target symbols add` (matched by
UUID, so a renamed copy of the program still finds its own), and prints the threads, every stack and the modules with `lldb`. `lldb`'s own
symbol search (Spotlight, `DebugSymbols`) is turned off, so only the store and a dSYM beside a module's file are used, and nothing is fetched.
Given the program, a dump from another build is refused (exit status 3; any architecture of a universal program counts). By hand:

```text
crash_extract --lldb <envelope> crash.dmp
lldb --no-lldbinit -b -O 'settings set symbols.enable-external-lookup false' -c crash.dmp \
  -o 'target symbols add symbols/<name>/<UUID>/<name>.dSYM' -o 'thread list' -o 'bt all' -o 'image list'
atos -o <name>.dSYM -l <module base> <address>      # one address, with the base from crash_extract --modules
```

The system libraries in the dump have a zero build id (`crash_extract --modules`): they live in the dyld shared cache, with no file for
Breakpad to read. `lldb` names their frames from this Mac's own copies, which is right only on the same macOS build.

**Core files.** The kernel writes a core to `sysctl kern.corefile` (normally `/cores/core.%P`) only when `kern.coredump` is 1, the core limit
is above 0 and the user can write to `/cores`, which on a stock Mac belongs to root (`sudo chmod 1777 /cores` opens it; a core is the whole
address space, often gigabytes). `scripts/find-core.sh` follows that pattern (`ENGINE_CORE_PATTERN` replaces it), checks the core's UUID for
the program against the program's with `lldb`, and says what is missing when there is no core; `scripts/open-core.sh` prints every thread's
stack with `lldb`. A core can also be saved from a stopped program in `lldb` with `process save-core <file>`.

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

### What installs the assert report

The assert macros live in the log module, below this one, so they reach stack traces and the crash context through hooks
(`log/diagnostics_hooks.h`). `debug::InitDebugFacilities` installs them (`debug::InstallDiagnosticsHooks`): after that a failed assert prints its
trace and context, and the I/O thread and job workers register with the crash handler when they start. Before, an assert prints its message and
aborts. A thread that started before installation is not registered later; registration only succeeds once the crash handler is installed anyway.

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
| macOS | `posix_spawn` | Phase 26; the POSIX code is shared with Linux |
| Windows | `CreateProcess` | Phase 13; exit statuses such as `0xC0000005` map to a [[debug::CrashReason]] |
| iOS, consoles | not possible | [[debug::DeathTestsSupported]] is false and death tests skip with a printed reason |

A fault shows up as an exit status, and a stack overflow is an access violation as far as the status can tell, so a death
test that wants [[debug::CrashReason::StackOverflow]] checks for [[debug::CrashReason::AccessViolation]].

## Common mistakes

- **Not calling [[debug::StartProgramDebugging]]** (or calling it late): the handler is not installed, and a crash before it leaves nothing.
- **Expecting a Release program to crash with a handler.** The default is off in Release; pass `--crash-handler=on` or set `ENGINE_CRASH_HANDLER=1`.
- **Reading a dump with the wrong build's symbols.** The trace names the wrong functions. Compare the build ids; the scripts refuse a mismatch (exit status 3).
- **Installing your own signal handler before `InstallCrashHandler`.** It is replaced and never runs; install it after and call the one it returns.
- **Forgetting [[debug::RegisterThreadForCrashHandling]]** on a thread that may recurse deeply: its stack overflow leaves no report.
- **A state provider that allocates, locks or reads freed memory.** It runs inside a crashing process; keep it to atomics and copies.
- **Attaching a dump or core to a public bug report.** Both hold process memory, possibly keys and passwords.
- **Naming a signal or exception code in a death test.** Compare with [[debug::CrashReason]] so the test runs on every platform.

## Compiler-specific constructs (`compiler.h`)

Code that needs a compiler builtin, an attribute or inline assembly uses a macro from `debug/compiler.h` instead, so the same source
builds with GCC and Clang on Linux and with MSVC and clang-cl on Windows. Do not write `__attribute__`, `__builtin_...` or
`__declspec` elsewhere.

| Macro | For | GCC/Clang | MSVC | clang-cl |
|---|---|---|---|---|
| [[ENGINE_DEBUG_BREAK]] | stop in a debugger here | `__builtin_debugtrap` / `int3` | `__debugbreak` | `__debugbreak` |
| [[ENGINE_TRAP]] | end the process at once, no handler | `__builtin_trap` | `__ud2` | `__builtin_trap` |
| [[ENGINE_UNREACHABLE_HINT]] | an optimizer hint, unchecked | `__builtin_unreachable` | `__assume(0)` | `__assume(0)` |
| [[ENGINE_COLD]] | a rarely called function | `__attribute__((cold))` | nothing | nothing |
| [[ENGINE_NOINLINE]], [[ENGINE_NOINLINE_LAMBDA]] | keep a frame in stack traces | `__attribute__((noinline))` | `__declspec(noinline)` | GNU form |
| [[ENGINE_PRINTF_FORMAT]] | check `printf`-style arguments | `__attribute__((format(printf, f, a)))` | nothing | GNU form |
| [[ENGINE_NO_UNIQUE_ADDRESS]] | an empty member that takes no space | `[[no_unique_address]]` | `[[msvc::no_unique_address]]` | `[[msvc::no_unique_address]]` |
| [[ENGINE_COMPILER_BARRIER]] | no reordering or tail call across this point | an empty `asm` with a memory clobber | `_ReadWriteBarrier` | `_ReadWriteBarrier` |

Two of these were found by building with MSVC (Phase 13 Day 148), and both fail silently: MSVC ignores `[[msvc::noinline]]` placed
between a lambda's parameters and its `->` and inlines the lambda anyway, and both MSVC and clang-cl ignore the standard
`[[no_unique_address]]`, which made a `UniquePtr` twice the size of a pointer.

## See also

[Logging and Assertions](foundation-logging-and-assert.md), [Porting the debugging facilities](porting-debugging.md), [Windows](windows.md),
[Building and testing](building-and-testing.md) (sanitizers, death tests in a test target).

