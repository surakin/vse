# Phase 13 / Day 155 — Debugging II: Crash Handler, Death Tests, Fault Injection

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 154.

The crashing half: sentry-native's Breakpad backend on Windows, the report written from its `on_crash` hook, child processes for death tests, and the fault
kinds. The tests in the `crash` group run only with `--all` or `--crashes`.

## Tasks

- [ ] **Vendored code and the Windows options** (Day 148): sentry-native and Breakpad are added before the engine's Windows options and definitions, so they
      build with their own; check whether they need exceptions (`ENGINE_EXCEPTIONS_COMPILE_OPTIONS`) and whether their `.rc` resources compile with `rc.exe`.
- [ ] **Crash library configuration** (the files were vendored on Day 154): `SENTRY_BACKEND=breakpad`, `SENTRY_TRANSPORT=none`, static library; the no-upload check
      (no `winhttp` import in any test binary or demo: `dumpbin /imports` in a ctest) passes on Windows.
- [ ] **The report from `on_crash`:** `os/windows/debug/crash_hook.cpp` writes the engine report (reason, address, raw frames with module offsets, crash state,
      log ring) from the `EXCEPTION_POINTERS` the hook receives, through the descriptor primitive chosen on Day 147; the code is between `SIGNAL-SAFE` markers and
      passes `signal_safety_check`. **No engine exception filter** of its own (the library installs the unhandled-exception filter and vectored handler).
      `detail::InstallCrashEventHook` maps to this hook.
- [ ] **`CrashReason` decode** from NTSTATUS: `AccessViolation` (0xC0000005, with read/write and address from `ExceptionInformation`), `IllegalInstruction`
      (0xC000001D), `ArithmeticFault` (0xC0000094), `StackOverflow` (0xC00000FD, needs the stack guarantee from Day 154), `Abort` (the `ENGINE-ABORT` marker rule
      below), breakpoint, and an "other" path printing the code.
- [ ] **`DeathTestsSupported`/`RunInChild`:** `CreateProcessW` with `CREATE_SUSPENDED`, a job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` and
      `JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION`, resume; standard output and error through pipes read with overlapped reads and the same nanosecond deadline;
      the child sets `SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS)` and `_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)`; `abort()`
      is told from `exit(3)` by the `SIGABRT` handler's `ENGINE-ABORT` line; the environment block is built wide. A child that hangs is killed at the deadline
      (the job object guarantees no orphan).
- [ ] **`FaultSupported`/`InjectFault`:** null write, `__ud2()`, integer divide by a volatile zero, unbounded recursion, `abort()`; each produces the
      `BACKEND-CONFORMANCE.md` reason in a child.
- [ ] **Fast-fail gap:** a test runs `__fastfail` in a child and checks the exit code is `0xC0000409` and that no engine report was written; the gap is documented
      in `BACKEND.md` and `foundation-debugging.md`, and filed in `KNOWN-ISSUES.md` as a known limitation (not a failure).
- [ ] **The allocation check without `--wrap`** (found on Day 147, spec section 3 item 2): `tests/debug` proves the crash path does not allocate by wrapping
      `malloc`/`calloc`/`realloc` with `-Wl,--wrap`, which `link.exe` and `lld-link` do not have. Pick the Windows form (a test-only seam in the engine's heap
      entry points, or `_CrtSetAllocHook` in a Debug build), show it catches a planted allocation in the crash hook, and record the choice.
- [ ] **Conformance groups:** *death tests*, *fault injection*, *crash report*, *assert* (fatal parts), *crash state* — every case passes or skips with a reason;
      `tests/debug`'s Linux-only files stay Linux-only, and new `*_windows_test.cpp` files cover what differs (the marker rule, WER dialogs never shown, the job
      object killing a hung child).
- [ ] **Documentation:** `BACKEND.md` sections updated; `porting-debugging.md` "Windows" order of work marked done to here; reference regenerated; `docgen lint`.
- [ ] **Runs:** Windows `scripts/test-phase.sh --affected --crashes` plain and asan (ASan intercepts some faults: record which cases are ASan-specific and why);
      Linux `--affected` if shared debug code changed; quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny (the reviews of Phase 7 found real bugs here): the crash hook allocating or locking through a
      library call, the pipe reader missing the last bytes of a crashed child, a child outliving its test, a report written for the wrong thread.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** a crashing Windows process writes the engine report and a minidump through the Breakpad backend with nothing uploaded, death tests and every fault kind
work in child processes, and the crash conformance groups pass or skip with a reason.
