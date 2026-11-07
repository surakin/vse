# Phase 13 / Day 155 — Debugging II: Crash Handler, Death Tests, Fault Injection

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 154.

The crashing half: sentry-native's Breakpad backend on Windows, the report written from its `on_crash` hook, child processes for death tests, and the fault
kinds. The tests in the `crash` group run only with `--all` or `--crashes`.

## Tasks

- [x] **Vendored code and the Windows options** (Day 148): sentry-native and Breakpad are added before the engine's Windows options and definitions, so they
      build with their own; check whether they need exceptions (`ENGINE_EXCEPTIONS_COMPILE_OPTIONS`) and whether their `.rc` resources compile with `rc.exe`.
- [x] **Crash library configuration** (the files were vendored on Day 154): `SENTRY_BACKEND=breakpad`, `SENTRY_TRANSPORT=none`, static library; the no-upload check
      (no `winhttp` import in any test binary or demo: `dumpbin /imports` in a ctest) passes on Windows.
- [x] **The report from `on_crash`:** `os/windows/debug/crash_hook.cpp` writes the engine report (reason, address, raw frames with module offsets, crash state,
      log ring) from the `EXCEPTION_POINTERS` the hook receives, through the descriptor primitive chosen on Day 147; the code is between `SIGNAL-SAFE` markers and
      passes `signal_safety_check`. **No engine exception filter** of its own (the library installs the unhandled-exception filter and vectored handler).
      `detail::InstallCrashEventHook` maps to this hook. *(Corrected during the day, see the notes: the engine adds a gate filter that handles nothing
      but keeps a second crashing thread from ending the process before the dump, and an observing vectored handler that records which thread
      raised each exception; the report is written on Breakpad's handler thread, not the faulting one.)*
- [x] **`CrashReason` decode** from NTSTATUS: `AccessViolation` (0xC0000005, with read/write and address from `ExceptionInformation`), `IllegalInstruction`
      (0xC000001D), `ArithmeticFault` (0xC0000094), `StackOverflow` (0xC00000FD, needs the stack guarantee from Day 154), `Abort` (the `ENGINE-ABORT` marker rule
      below), breakpoint, and an "other" path printing the code.
- [x] **`DeathTestsSupported`/`RunInChild`:** `CreateProcessW` with `CREATE_SUSPENDED`, a job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` and
      `JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION`, resume; standard output and error through pipes read with overlapped reads and the same nanosecond deadline;
      the child sets `SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS)` and `_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)`; `abort()`
      is told from `exit(3)` by the `SIGABRT` handler's `ENGINE-ABORT` line; the environment block is built wide. A child that hangs is killed at the deadline
      (the job object guarantees no orphan).
- [x] **`FaultSupported`/`InjectFault`:** null write, `__ud2()`, integer divide by a volatile zero, unbounded recursion, `abort()`; each produces the
      `BACKEND-CONFORMANCE.md` reason in a child.
- [x] **Fast-fail gap:** a test runs `__fastfail` in a child and checks the exit code is `0xC0000409` and that no engine report was written; the gap is documented
      in `BACKEND.md` and `foundation-debugging.md`, and filed in `KNOWN-ISSUES.md` as a known limitation (not a failure).
- [x] **The allocation check without `--wrap`** (found on Day 147, spec section 3 item 2): `tests/debug` proves the crash path does not allocate by wrapping
      `malloc`/`calloc`/`realloc` with `-Wl,--wrap`, which `link.exe` and `lld-link` do not have. Pick the Windows form (a test-only seam in the engine's heap
      entry points, or `_CrtSetAllocHook` in a Debug build), show it catches a planted allocation in the crash hook, and record the choice.
- [x] **Conformance groups:** *death tests*, *fault injection*, *crash report*, *assert* (fatal parts), *crash state* — every case passes or skips with a reason;
      `tests/debug`'s Linux-only files stay Linux-only, and new `*_windows_test.cpp` files cover what differs (the marker rule, WER dialogs never shown, the job
      object killing a hung child).
- [x] **Documentation:** `BACKEND.md` sections updated; `porting-debugging.md` "Windows" order of work marked done to here; reference regenerated; `docgen lint`.
- [x] **Runs:** Windows `scripts/test-phase.sh --affected --crashes` plain and asan (ASan intercepts some faults: record which cases are ASan-specific and why);
      Linux `--affected` if shared debug code changed; quote the lines.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny (the reviews of Phase 7 found real bugs here): the crash hook allocating or locking through a
      library call, the pipe reader missing the last bytes of a crashed child, a child outliving its test, a report written for the wrong thread.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** a crashing Windows process writes the engine report and a minidump through the Breakpad backend with nothing uploaded, death tests and every fault kind
work in child processes, and the crash conformance groups pass or skip with a reason.

## Notes (2026-11-07, Windows PC)

- **Vendored code:** sentry-native and Breakpad are added before the engine's Windows options, so they compile with CMake's default `/EHsc`
  (Breakpad uses the C++ library; the engine keeps `/EHs-c- /GR- _HAS_EXCEPTIONS=0`). No resource compiles: the library's `sentry.rc.in` is used
  only by its shared build. `SENTRY_THREAD_STACK_GUARANTEE_AUTO_INIT` is turned off (root `CMakeLists.txt`): the library set a 32 KiB guarantee of
  its own at `sentry_init`, and its warning that one was already set ended up in every report's log section.
- **No upload:** `tests/debug/check_no_network_windows.cmake` reads the import table (`link /dump /imports` for MSVC, `llvm-readobj --coff-imports`
  for clang-cl, chosen in `tests/debug/CMakeLists.txt`) and the binary's ASCII and UTF-16 strings for `winhttp`, `wininet`, `ws2_32` and the
  like; it refuses a dump that names no `kernel32.dll` (the tool said nothing). Ctests `debug_no_network_debug_tests`, `_debug_tests_asan`,
  `_debug_checks_off_tests`, and three canned dumps (`fixtures/dumpbin_*.txt`: clean, a planted `WINHTTP.dll`, a planted delay-loaded
  `GetAddrInfoW`). None of the binaries imports or names a network library.
- **The report from `on_crash`** (`os/windows/debug/crash_hook.cpp`). On Windows the library writes the dump on Breakpad's handler thread and
  then calls `on_crash` there with the `EXCEPTION_POINTERS` (the faulting thread waits). So:
  - the faulting thread's id and name come from an **observer**, a first vectored handler that only records (exception record, thread id,
    crash name) in a ring of 128 and passes every exception on; an `abort()` raises no exception (the library builds the record), so the
    registered stack ranges answer then; if neither knows, the report says `thread: unknown` (new `CrashReportInfo::thread_known`,
    `CrashReportInfo::thread_name` and the matching `CrashEvent` fields, public header `debug/crash_report.h`);
  - the stack is walked from the `CONTEXT` with `RtlLookupFunctionEntry`/`RtlVirtualUnwind` under `__try`;
  - **frames are module and offset only**: the Day 154 rule "no DbgHelp while a crash is reported" was not yet true, because the neutral
    report formats through `FormatStackTrace`, which resolved through DbgHelp (a lock, heap allocations). `ResolveStackTrace` now gives the
    module and offset from the module table while `detail::CrashReportInProgress()` is set. Writing that showed a neutral bug:
    `FormatStackTrace` printed only the last hex digit of a module offset (`+0xf`); fixed in `debug/stack_trace_format.cpp`. The end-to-end
    test parses the first frame, checks the image base (address minus offset) is 64 KiB aligned, and resolves the offset in its own process
    to `InjectFault`; both fixes were confirmed by reverting them (the checks failed);
  - a fault inside the report (a state provider) is caught with `__try` and prints `double fault: ...; the dump was already written`;
  - **the crash gate**: two threads faulting together left no report and no dump in about one run in three (0 of 12 when run alone):
    Breakpad's filter puts back the previous top-level filter while it handles a crash, so the second thread got default handling and ended
    the process mid-dump. `detail::PrepareCrashEventHook` (before `sentry_init`) and `InstallCrashEventHook` (after) install a gate filter
    that handles nothing: the first crash goes to the library, a later one waits, one the library hands back goes to the system. After it:
    15 of 15. This corrects the task's "no engine exception filter" (the plan assumed the library's filter is safe for concurrent crashes).
- **`CrashReason` decode:** `AccessViolation` (with the address from `ExceptionInformation[1]`), `BusError` (`EXCEPTION_IN_PAGE_ERROR`),
  `IllegalInstruction`, `ArithmeticFault`, `StackOverflow`, `Abort` (`0x40000015`, the library's record for `abort()`; no address, as on Linux),
  anything else `None` with the instruction address. The death-test decoder maps the same codes from the exit status, and exit code 3 with
  the `ENGINE-ABORT` marker to `Abort`.
- **Death tests** (`os/windows/debug/death_test.cpp`): as the task says, plus only the pipe ends inherited (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`),
  `PeekNamedPipe` polls (not overlapped reads: the poll slices against the nanosecond deadline do the same job), the job also
  `DIE_ON_UNHANDLED_EXCEPTION`, the whole job ended at the deadline (`0xDEAD0001`), the pipes drained after the end. The sanitizer options a
  caller passes are merged with the fault flags rather than added twice. The `debug_tests` executables get an 8 MiB main stack
  (`/STACK:8388608`): a `ChildResult` is a 64 KiB value and the Debug build keeps several copies on the stack. The resource guard
  (`tests/debug/resource_guard_windows.cpp`) starts one throw-away child first (the first `CreateProcess` of a program leaves 6, then 7 handles
  open for good) and waits up to a second for a child's handles to go; its diagnostic line on a failure is kept.
- **Fault injection** (`os/windows/debug/fault_injection.cpp`): null write, `__ud2`, a volatile divide (x86/x64 only), recursion, `abort()`;
  `BusError` has no Windows form and skips. The recursion's volatile condition keeps MSVC (C4717) and clang-cl (`-Winfinite-recursion`) quiet.
- **Fast fail:** `crash_end_to_end_windows_test.cpp` runs `__fastfail(7)` with the handler installed: exit code `0xC0000409`, no report, no
  envelope. Documented in `BACKEND.md` and `foundation-debugging.md`, filed in `KNOWN-ISSUES.md` as a limitation.
- **The allocation check** (`tests/debug/crash_alloc_windows_test.cpp`): the debug C runtime's `_CrtSetAllocHook`, counting allocations on the
  reporting thread around `detail::ReportCrashFromExceptionPointers` with a made-up access violation (the same path the library's hook runs).
  Chosen over a seam in the engine's heap entry points because it also sees the C runtime and `operator new` calls of code the engine does not
  own. The report makes 0; a state provider that calls `malloc` makes the count non-zero (so the check sees a planted allocation). It needs
  the debug runtime without AddressSanitizer: it skips in the ASan flavor. It does not see `HeapAlloc` inside Windows; `CreateFileW` for the
  report file is the one such use left (filed).
- **Stack guarantee measured:** the stack-overflow cases (main thread, registered worker) pass with 4, 8 and 16 KiB (three runs each): the
  overflowed thread only runs the observer, the gate and Breakpad's hand-over. 64 KiB is kept for the ASan flavor's larger frames.
- **Conformance groups:** *death tests* (`death_test_run_test`, `death_test_registry_test`, and `death_test_windows_test.cpp` for the marker
  rule, the error mode and job limits of a child, and a hung child's own child ended with it), *fault injection*, *crash report*
  (`crash_report_test.cpp` converted to the engine's types: 43 standard-library lines gone, `tests/debug` now 279 in
  `tools/std_container_check/allowlist.txt`; its Linux-only `readelf` case moved to `crash_report_linux_test.cpp`), `crash_handler_test.cpp`,
  *assert* (fatal parts), *crash state*, and `crash_end_to_end_windows_test.cpp` (each fault kind, a worker's fault, overflow and abort, two
  threads at once, a second fault during the report, a faulting provider, an assert, uninstall, fast fail). `crash_end_to_end_linux_test.cpp`
  and the other `*_linux_*` files stay Linux-only.
- **Gates:** `signal_safety_check` now allows `std::atomic_thread_fence` (an atomic; the observer and the reader of its ring use fences);
  `file_access_check` allows `os/windows/debug/death_test.cpp` (it opens the `NUL` device, E3); `config_access_check` allows the same file (the
  death-test child protocol).
- **Runs** (after the review fixes): `scripts/test-phase.sh --affected --crashes` printed
  `ran 515 of 807 tests (phases: all; flavors: plain; display: included; crash tests: included) in 31 s`, with `--flavor asan`
  `ran 292 of 807 tests (phases: all; flavors: asan; display: included; crash tests: included) in 19 s`; `ctest --preset windows-msvc` 807 of 807,
  `windows-clang-cl` 545 of 545, non-unity 806 of 807 (`FramePool does not commit slab memory until blocks are used [asan]`, phase 2, failed
  once under `-j8` and passed 5 times alone: filed as flaky). ASan-specific: the allocation check skips under ASan; every crash case passes
  in both flavors (the child's ASan options let the faults through). The full runs found one more thing: the crash-context case expected a
  build id of whole bytes, but the Windows id is the GUID and the age without padding (odd for an age below 16; the unity build's age was
  0x10, so only the other builds failed); the check now allows that form on Windows.
- **For the Linux run at the phase close:** shared files changed today: `debug/crash_report.h` and `.cpp` (`thread_known`, `thread_name`),
  `debug/crash_event_platform.h`, `debug/crash_handler_sentry.cpp` (the Windows hook calls; `directory_path` parsed in the native style),
  `debug/stack_trace_format.cpp` (the offset digits), `engine/foundation/CMakeLists.txt`, the root `CMakeLists.txt` (Windows block only),
  `tests/debug/CMakeLists.txt`, `crash_report_test.cpp`, `crash_report_linux_test.cpp`, `death_test_run_test.cpp`, `tools/signal_safety_check/check.cmake`,
  the allow lists, the guides. On Linux: `scripts/test-phase.sh --affected --crashes` with asan and tsan (the debug suite; the report's thread
  line and the module offsets), `ctest -L docs` after regenerating the reference for `debug/crash_report.h`.

## Post-implementation code review

`/code-review high` over the day's diff: **9 findings, 5 fixed, 4 declined.**

Fixed, each with a regression test that failed before the fix (confirmed by reverting it) unless noted: (1) a call through a null function
pointer gave an empty trace (the walk stopped at a first frame of 0); it now keeps that frame and goes on from the return address (synthetic
test in `crash_alloc_windows_test.cpp`). (2) A crash on a thread the hook could not identify was reported with Breakpad's handler thread's id;
it now says `thread: unknown` (`abort()` on an unregistered worker). (3) The gate stayed claimed if the library resumed an exception
(`EXCEPTION_CONTINUE_EXECUTION`), so a later crash would have waited and left no dump; it is freed then (no test: Breakpad resumes only for
invalid-handle exceptions when configured to, which the engine does not do). (4) An abort's report printed the library's raise address as a
fault address; it prints none, as on Linux (the abort case checks `reason: Abort` ends the line). (5) The observer ring held 32 entries,
which other threads' handled exceptions could reuse before the report; it holds 128 (no deterministic test).

Declined: (6) the report file is opened with `CreateFileW` at crash time, which allocates inside Windows; opening it at install would change
the shared facade's behaviour on both platforms, so it is filed in `KNOWN-ISSUES.md` for Day 156's dump-directory work to revisit. (7) While a
crash is being reported every thread's `ResolveStackTrace` gives module and offset only: the process is ending, and a per-thread flag would
need the crash thread's identity, which is what the observer approximates. (8) The registered-thread table never shrinks (256 entries); filed.
(9) A pipe write end could be inherited by an unrelated `CreateProcess` with inheritance on another thread; the death tests run one at a time,
the job ends the child's own children, and the drain stops after its round limit.
