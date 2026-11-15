# Windows debugging backend (Phase 13)

What Phase 13 implements to bring up the debugging facilities on Windows. Day 154 built symbols, modules, the crash context, the debugger
check and thread ids and names; Day 155 the crash handler, the report, death tests and fault injection; Day 156 the post-mortem dumps,
`crash_extract` and the scripts. Every section below is marked **Built** or not applicable. Linux is the reference
(`engine/foundation/os/linux/debug/`). Everything else is shared: the facade (`debug/*.cpp`), the assert family, the report
writer, the log ring, the crash state, the scripts' logic and the tests. See `docs/guide/porting-debugging.md` for the order of
work and `tests/debug/BACKEND-CONFORMANCE.md` for what each test group needs.

The crash library (sentry-native, Breakpad backend) and libbacktrace both list Windows as supported at the pinned versions
(audit: `docs/superpowers/specs/2026-11-06-debugging-design.md`, "Platform audit"). **Open decision for Phase 13:** libbacktrace
reads DWARF only, so a clang build with DWARF works as is; an MSVC/PDB build needs a DbgHelp resolver behind
`ResolveStackTrace`. **Decided on Day 154: DbgHelp for both compilers** (`os/windows/debug/stack_trace_dbghelp.cpp`); libbacktrace is not
built on Windows, and the facade's formatting is the neutral `debug/stack_trace_format.cpp`. **Rule: DbgHelp is never called while a crash
is being reported** (it is not async-safe and takes a lock the crashed thread may hold): while `detail::CrashReportInProgress()` is true,
`ResolveStackTrace` gives each frame's module and offset from the module table read at initialization (no lock, no allocation), so the
Windows report prints `module+0x<offset>` lines, and names are resolved afterwards, in the test parent, the crash script or a debugger
(Day 155; `crash_end_to_end_windows_test.cpp` resolves the faulting frame's offset in its own process and finds `InjectFault`).

Each entry: the neutral function, the Windows API, the trap, the conformance groups that exercise it.

## Functions with a `Backend:` label (a test keeps this list in step with the headers)

### `InitCrashContext`
Fill the platform fields of the crash context (`detail::FillPlatformCrashContext` in `crash_context_platform.h`). Build id: the
PE debug directory's CodeView (RSDS) record, GUID plus age, written as lowercase hex. OS: `RtlGetVersion` (not
`GetVersionEx`, which lies without a manifest). CPU brand: `__cpuid` leaves 0x80000002-4; count: `GetActiveProcessorCount`.
Memory: `GlobalMemoryStatusEx`. Executable: `GetModuleFileNameW` converted to UTF-8. Trap: all text is UTF-8; the fields are
fixed buffers. Groups: *crash context*. **Built (Day 154)**, `os/windows/debug/crash_context.cpp`: the build id is the GUID's fields in
order (Data1, Data2, Data3 as numbers, then the 8 bytes of Data4) and the age in hex, all lowercase and without dashes; the OS version is
`major.minor.build`; the architecture `x86_64`, `aarch64` or `x86`; the CPU count is `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)`.

### `EnablePostMortemDumps`
Windows Error Reporting `LocalDumps`: the registry values under `HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\<exe>`
(`DumpFolder`, `DumpType`, `DumpCount`). Trap: writing HKLM needs elevation; without it return `Limited` with a note saying
which values to set. Never write outside the program's own key. Groups: *post-mortem*. **Built (Day 156)**, `os/windows/debug/post_mortem.cpp`,
**read-only**: measured on this Windows 11 build, WER ignored the same values under the per-user key (HKCU) for both the engine's test
program and a minimal probe, though it ran and queued its own report (asked to keep it local, `WER_FAULT_REPORTING_FLAG_QUEUE`, and to show
nothing, `WER_FAULT_REPORTING_NO_UI`; the queued reports were removed). Writing HKLM needs an administrator and changes the machine for
every user, so the function writes nothing: `Enabled` with the folder and dump type when the machine's key lists the program (or all
programs), `Limited` with a note naming the key and values to create otherwise, or when the process's error mode (`SEM_NOGPFAULTERRORBOX`,
every death-test child) or the machine (`Disabled`) keeps WER from running. `tests/debug/post_mortem_windows_test.cpp` covers both states
and checks nothing is written.

### `FaultSupported` and `InjectFault`
`NullWrite`: volatile write to address 0. `IllegalInstruction`: `__ud2()`. `DivideByZero`: integer divide with volatile operands.
`StackOverflow`: unbounded recursion with a volatile local. `Abort`: `abort()`. **Windows on arm64:** an integer divide does not trap, so
`FaultSupported(DivideByZero)` is false there, as on macOS arm64. Expected `CrashReason`: `AccessViolation` (0xC0000005), `IllegalInstruction` (0xC000001D),
`ArithmeticFault` (0xC0000094), `StackOverflow` (0xC00000FD), `Abort`. Groups: *fault injection*. **Built (Day 155)**,
`os/windows/debug/fault_injection.cpp`: `BusError` has no Windows form (no fault that every machine raises the way a truncated mapping
raises SIGBUS), so `FaultSupported(BusError)` is false and its case skips; `DivideByZero` only on x86 and x64.

### `IsDebuggerAttached`
`IsDebuggerPresent()`. Trap: it is true under any debugger, including a tracer the user did not mean. Groups: *assert*. **Built (Day 154).**

**`CrashDumpDirectory`** (moved to the file layer: see `engine/foundation/io/BACKEND.md`).
`%LOCALAPPDATA%/<app>/crashes` through `SHGetKnownFolderPath(FOLDERID_LocalAppData)`, converted to UTF-8, created private to the
user (an ACL granting only the user; the Linux 0700 check has no direct equivalent). Groups: *dump directory*. **Built (Day 152)**; Day 156
tests the crash handler's default directory, a directory that cannot be created, and dumps kept across a restart (`saved\`) on Windows.

### `RegisterThreadForCrashHandling`
`SetThreadStackGuarantee` to a size that holds the report (the library documents `SENTRY_THREAD_STACK_GUARANTEE_*`; the
build option `SENTRY_THREAD_STACK_GUARANTEE_AUTO_INIT` does it for threads that start in the DLL). Sets the name with
`SetThreadDescription` as well as `SetCrashThreadName`. Groups: *threads*. **Built (Day 154)** with a 64 KiB guarantee. **Measured (Day 155):**
the report is written on Breakpad's handler thread, which has its own stack, so the overflowed thread only runs the observer, the crash gate
and Breakpad's filter (which hands the exception over and waits): the stack-overflow cases on the main thread and on a worker pass with a
guarantee of 4, 8 and 16 KiB (three runs each, plain flavor). 64 KiB is kept, for the larger frames of the AddressSanitizer flavor and a
debugger's own use. It also records the thread's stack range and its `CrashThreadName` buffer, so an `abort()` (which raises no exception
the observer sees) is reported for the right thread, with its name. The library's own setting (`SENTRY_THREAD_STACK_GUARANTEE_AUTO_INIT`)
is turned off in the root `CMakeLists.txt`: it set a second, smaller guarantee and logged a warning into every report's log section.

### `InitDebugFacilities` and `ReloadDebugModules`
The module list for symbol lookup: `EnumProcessModules` plus `GetModuleInformation`, published through the immutable table in
`module_list.h`. Groups: *stack trace*. **Built (Day 154)**: `EnumProcessModulesEx(LIST_MODULES_ALL)`; a module's offset is relative to its
image base. `ReloadDebugModules` also calls `SymRefreshModuleList`, so a DLL loaded later gets its names (tested with the test plugin, which
is linked `/INCREMENTAL:NO`: with incremental linking `GetProcAddress` returns a jump thunk outside the function).

### `DemangleSymbol`
`UnDecorateSymbolName` for MSVC names; clang-cl with Itanium names uses the LLVM demangler. Groups: *stack trace*. **Built (Day 154)**:
clang-cl decorates the Microsoft way too (tested), so no Itanium demangler is needed; a name without the leading `?` is refused. Names that
come from DbgHelp are already undecorated (`SYMOPT_UNDNAME`, and the PDB keeps them readable).

### `CurrentThreadId`
`GetCurrentThreadId`. Groups: *crash report*. **Built (Day 154).**

### `DeathTestsSupported` and `RunInChild`
`CreateProcessW` with `CREATE_SUSPENDED`, assign to a job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, resume; pipes for
standard output and error with overlapped reads and the same nanosecond deadline. Decode the exit code to `CrashReason`:
NTSTATUS values above. `abort()` exits with code 3, which `exit(3)` also produces, so the child installs
`signal(SIGABRT, ...)` first and the handler writes a marker line (`ENGINE-ABORT`) to standard error before exiting: the decoder maps
"exit code 3 and the marker" to `Abort`. (`__fastfail` exits with `0xC0000409` and is not a `FaultKind`.) Trap: Windows Error Reporting may show a dialog for a crashed child:
set `SetErrorMode(SEM_NOGPFAULTERRORBOX)` in the child. `posix_spawn` is not available; the environment block is built as a
wide-character block. Groups: *death tests*. **Built (Day 155)**, `os/windows/debug/death_test.cpp`: the job also has
`JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION`, so a crashed child never waits on a dialog; only the pipe ends are inherited
(`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`); the pipes are drained with `PeekNamedPipe` polls against the deadline; the child also calls
`_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)`; a child past its deadline is ended with `TerminateJobObject`, which ends any
process it started too (exit code `0xDEAD0001`, reported as `timed_out`). `death_test_windows_test.cpp` checks the marker rule, the error
mode and job limits a child runs with, and that a hung child's own child is gone after the deadline.

## Pieces without a neutral label

- **Crash handler build:** `SENTRY_BACKEND=breakpad`, `SENTRY_TRANSPORT=none`; no handler executable is needed with that
  backend (Crashpad would need one and uses winhttp). Keep `SENTRY_BUILD_SHARED_LIBS=OFF`. **Built (Day 155):** the library and Breakpad
  are added before the engine's Windows options, so they compile with CMake's default `/EHsc` (Breakpad uses the C++ library) while the
  engine keeps `/EHs-c- /GR-`; the library's `sentry.rc.in` is only for its shared build, so no resource compiles. Nothing can upload: the
  ctests `debug_no_network_*` read each test binary's imports (`link /dump /imports`, or `llvm-readobj --coff-imports` for clang-cl) and its
  ASCII and UTF-16 strings for a network library (`tests/debug/check_no_network_windows.cmake`, tested on three canned dumps).
- **The report (Built, Day 155; `os/windows/debug/crash_hook.cpp`):** the library writes the dump on Breakpad's handler thread and then
  calls its `on_crash` hook there with the fault's `EXCEPTION_POINTERS` (the faulting thread waits); `crash_handler_sentry.cpp` passes them
  to `detail::ReportCrashFromExceptionPointers`, which decodes the reason (the codes above, `0x40000015` for the library's `abort()`; any
  other code prints as `None`), walks the faulting thread's stack from its `CONTEXT` with `RtlVirtualUnwind` under `__try`, and writes the
  report. A fault inside the report (a state provider) ends it with a `double fault` line; the dump is already written. Because the report
  is written on another thread, the faulting thread is found by an **observer** (a first vectored handler that only notes which thread
  raised each exception record, and that thread's crash name, in a ring of 128, and always passes the exception on) or, for `abort()`, by
  the registered stack ranges; when neither knows (an `abort()` on a thread never registered) the report says `thread: unknown` rather than
  name the handler thread it runs on. An abort is reported without an address (the record is the library's), as on Linux. A first frame at
  address 0 (a call through a null pointer) is kept and the walk goes on from the return address on top of the stack.
- **The crash gate (Day 155):** Breakpad's filter, while it handles one crash, puts back the top-level filter that was there before it, so a
  second thread faulting at that moment got the system's default handling and the process ended without a dump (seen in about one run in
  three of two threads faulting together). `detail::PrepareCrashEventHook` installs a gate filter before `sentry_init` (so it is what
  Breakpad puts back) and `InstallCrashEventHook` installs it again on top: the first crash goes to the library, a later one waits for the
  process to end, and a crash the library hands back goes on to the system (one it resumes, `EXCEPTION_CONTINUE_EXECUTION`, frees the
  gate again). The gate handles nothing itself. After `UninstallCrashHandler`
  it passes everything on to the filter that was there before it.
- **What the report calls into Windows for:** the report file is opened at crash time with `CreateFileW` (through `os::CreateFileForWriting`),
  which converts the path with a heap allocation inside Windows (`KNOWN-ISSUES.md`); everything else is fixed buffers and `WriteFile`. The
  allocation check (`tests/debug/crash_alloc_windows_test.cpp`) is the debug C runtime's `_CrtSetAllocHook`, counting the C runtime
  allocations on the reporting thread (Debug build, plain flavor; GNU ld's `--wrap`, which Linux uses, has no `link.exe` or `lld-link`
  equivalent). A planted `malloc` in a state provider shows it counts. Heap use inside Windows itself (`HeapAlloc`) is outside what it sees.
- **Fast-fail crashes** (`__fastfail`, `/GS` failures) end the process in the kernel: no vectored handler, filter or library sees them, so
  there is no report and no dump in the handler's directory (exit code `0xC0000409`; `crash_end_to_end_windows_test.cpp` checks it).
  Windows Error Reporting's LocalDumps, when an administrator has set them up (`EnablePostMortemDumps`), is the only dump such a crash can leave.
- **`detail::InstallCrashEventHook`** installs the observer and the crash gate; `RemoveCrashEventHook` removes the observer and turns the
  gate into a pass-through; after `sentry_close` (which puts the gate back on top) `FinishCrashEventHookRemoval` gives the top-level filter
  back to the one that was there before the engine installed, unless something was installed over the gate since (Phase 23 Day 354: a host
  that loaded `engine.dll` gets its own filter back, and none is left pointing into the library after `FreeLibrary`;
  `tests/engine_library/host_runtime.c` checks it).
- **Scripts (Built, Day 156):** `scripts/store-symbols.ps1` files a program's PDB and image in the layout WinDbg reads
  (`<pdb>\<GUID+age>\<pdb>`, `<exe>\<timestamp><size>\<exe>`); `crash-report.ps1` prints the exception, the modules with their build ids
  and the report with its frames named from the store (`crash_extract --resolve`, DbgHelp), and refuses a different build of the program
  (exit 3); `find-dump.ps1` lists the engine's envelopes and WER's dumps; `open-dump.ps1` opens a dump in WinDbg or cdb with the store as the
  only symbol path (`_NT_SYMBOL_PATH` cleared). `split-debug.sh` has no Windows twin: a PDB is already separate from its image. `crash_extract`
  gained `--modules`, `--report`, `--image` and `--resolve` (`tools/crash_extract/minidump.cpp`, `resolve.cpp`, `windows/resolve.cpp`);
  names it takes from a dump become store paths only as one plain file name and hex. Tests: `crash_report_script_windows_test.cpp`,
  `minidump_test.cpp`.
- **CMake (Built, Days 154-156):** the Windows source list replaced the stop; `/DEBUG` (CMake's Debug default) produces the CodeView
  record the build id comes from. `libbacktrace` is not ported: not applicable, DbgHelp resolves on Windows (Day 154's decision).
