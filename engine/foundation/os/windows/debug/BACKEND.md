# Windows debugging backend (Phase 8)

What Phase 8 implements to bring up the debugging facilities on Windows. Nothing here exists yet; Linux is the reference
(`engine/foundation/os/linux/debug/`). Everything else is shared: the facade (`debug/*.cpp`), the assert family, the report
writer, the log ring, the crash state, the scripts' logic and the tests. See `docs/guide/porting-debugging.md` for the order of
work and `tests/debug/BACKEND-CONFORMANCE.md` for what each test group needs.

The crash library (sentry-native, Breakpad backend) and libbacktrace both list Windows as supported at the pinned versions
(audit: `docs/superpowers/specs/2026-11-06-debugging-design.md`, "Platform audit"). **Open decision for Phase 8:** libbacktrace
reads DWARF only, so a clang build with DWARF works as is; an MSVC/PDB build needs a DbgHelp resolver behind
`ResolveStackTrace`.

Each entry: the neutral function, the Windows API, the trap, the conformance groups that exercise it.

## Functions with a `Backend:` label (a test keeps this list in step with the headers)

### `InitCrashContext`
Fill the platform fields of the crash context (`detail::FillPlatformCrashContext` in `crash_context_platform.h`). Build id: the
PE debug directory's CodeView (RSDS) record, GUID plus age, written as lowercase hex. OS: `RtlGetVersion` (not
`GetVersionEx`, which lies without a manifest). CPU brand: `__cpuid` leaves 0x80000002-4; count: `GetActiveProcessorCount`.
Memory: `GlobalMemoryStatusEx`. Executable: `GetModuleFileNameW` converted to UTF-8. Trap: all text is UTF-8; the fields are
fixed buffers. Groups: *crash context*.

### `EnablePostMortemDumps`
Windows Error Reporting `LocalDumps`: the registry values under `HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\<exe>`
(`DumpFolder`, `DumpType`, `DumpCount`). Trap: writing HKLM needs elevation; without it return `Limited` with a note saying
which values to set. Never write outside the program's own key. Groups: *post-mortem*.

### `FaultSupported` and `InjectFault`
`NullWrite`: volatile write to address 0. `IllegalInstruction`: `__ud2()`. `DivideByZero`: integer divide with volatile operands.
`StackOverflow`: unbounded recursion with a volatile local. `Abort`: `abort()`. **Windows on arm64:** an integer divide does not trap, so
`FaultSupported(DivideByZero)` is false there, as on macOS arm64. Expected `CrashReason`: `AccessViolation` (0xC0000005), `IllegalInstruction` (0xC000001D),
`ArithmeticFault` (0xC0000094), `StackOverflow` (0xC00000FD), `Abort`. Groups: *fault injection*.

### `IsDebuggerAttached`
`IsDebuggerPresent()`. Trap: it is true under any debugger, including a tracer the user did not mean. Groups: *assert*.

### `GetCrashDumpDirectory`
`%LOCALAPPDATA%/<app>/crashes` through `SHGetKnownFolderPath(FOLDERID_LocalAppData)`, converted to UTF-8, created private to the
user (an ACL granting only the user; the Linux 0700 check has no direct equivalent). Groups: *dump directory*.

### `RegisterThreadForCrashHandling`
`SetThreadStackGuarantee` to a size that holds the report (the library documents `SENTRY_THREAD_STACK_GUARANTEE_*`; the
build option `SENTRY_THREAD_STACK_GUARANTEE_AUTO_INIT` does it for threads that start in the DLL). Sets the name with
`SetThreadDescription` as well as `SetCrashThreadName`. Groups: *threads*.

### `InitDebugFacilities` and `ReloadDebugModules`
The module list for symbol lookup: `EnumProcessModules` plus `GetModuleInformation`, published through the immutable table in
`module_list.h`. Groups: *stack trace*.

### `DemangleSymbol`
`UnDecorateSymbolName` for MSVC names; clang-cl with Itanium names uses the LLVM demangler. Groups: *stack trace*.

### `CurrentThreadId`
`GetCurrentThreadId`. Groups: *crash report*.

### `DeathTestsSupported` and `RunInChild`
`CreateProcessW` with `CREATE_SUSPENDED`, assign to a job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, resume; pipes for
standard output and error with overlapped reads and the same nanosecond deadline. Decode the exit code to `CrashReason`:
NTSTATUS values above. `abort()` exits with code 3, which `exit(3)` also produces, so the child installs
`signal(SIGABRT, ...)` first and the handler writes a marker line (`ENGINE-ABORT`) to standard error before exiting: the decoder maps
"exit code 3 and the marker" to `Abort`. (`__fastfail` exits with `0xC0000409` and is not a `FaultKind`.) Trap: Windows Error Reporting may show a dialog for a crashed child:
set `SetErrorMode(SEM_NOGPFAULTERRORBOX)` in the child. `posix_spawn` is not available; the environment block is built as a
wide-character block. Groups: *death tests*.

## Pieces without a neutral label

- **Crash handler build:** `SENTRY_BACKEND=breakpad`, `SENTRY_TRANSPORT=none`; no handler executable is needed with that
  backend (Crashpad would need one and uses winhttp). Keep `SENTRY_BUILD_SHARED_LIBS=OFF`. The library installs the
  unhandled-exception filter and the vectored handler itself; **do not add an engine recorder** like Linux's
  `crash_signal.cpp`: instead write the report from the library's `on_crash` hook, where Windows (unlike Linux) passes the
  `EXCEPTION_POINTERS`. The hook runs in the faulting process; stack overflow needs the guarantee above.
- **Fast-fail crashes** (`__fastfail`, `/GS` failures) bypass SEH: the library notes that `on_crash` is not called for them on
  Breakpad. Document the gap.
- **`detail::InstallCrashEventHook`** has no Windows implementation; the Windows `OnCrashEvent` is the `on_crash` hook.
- **Scripts:** `find-core.sh`, `open-core.sh`, `split-debug.sh` and `store-symbols.sh` are Linux shell scripts. Windows needs
  PowerShell equivalents for WER dumps and a symbol store (PDB or DWARF, chosen with the resolver decision above).
- **CMake:** the branch in `engine/foundation/CMakeLists.txt` that stops with a message names this file; replace it with the
  real source list. Add the `-Wl,--build-id` equivalent (`/DEBUG` produces the CodeView record). Port `libbacktrace` with
  `pecoff.c`, `alloc.c`, `read.c` (not `mmap.c`).
