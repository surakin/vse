# Phase 26 / Day 291 — Debugging II: Crash Handler, Death Tests, Fault Injection

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 290.

sentry-native's Breakpad backend on macOS (Mach exception ports, no transport), the report written from `on_crash`, death tests without
`PR_SET_PDEATHSIG`, fault injection and the debugger check.

## Tasks

- [ ] **Re-vendor Breakpad's macOS private headers** (`KNOWN-ISSUES.md`, Phase 7 Day 95): the files sentry's CMake needs for `client/mac` and
      `common/mac` at the pinned sentry-native 0.17.6, recorded in the vendoring notes with their hashes; nothing else under `third_party/` changes.
- [ ] **Crash library build:** `SENTRY_BACKEND=breakpad`, `SENTRY_TRANSPORT=none` on macOS; the no-network check (`debug_no_network_*`) learns Mach-O
      (`nm -u` instead of the ELF reader) and passes for the test binaries and `crash_extract`.
- [ ] **Report from `on_crash`** (`crash_handler_sentry.cpp`'s macOS branch): the hook receives a `ucontext` on macOS; write the report from it, do not
      install the Linux signal recorder; `detail::InstallCrashEventHook` maps to the hook. The report's stack comes from the context's registers.
      Everything between `SIGNAL-SAFE-BEGIN`/`END` stays signal-safe (the hook runs on Breakpad's exception thread, so the rules still apply).
- [ ] **`RegisterThreadForCrashHandling`:** sets the thread's name and returns true (Mach exceptions need no alternate stack); a stack overflow on a worker
      thread produces a report (tested).
- [ ] **`DeathTestsSupported` and `RunInChild`:** `posix_spawn` shared with Linux in `os/posix/debug/` (macOS has no `pipe2`: `pipe` then `FD_CLOEXEC`,
      also in `tests/debug/support/test_support_linux.h`, which becomes `_posix`); the child watches a pipe from the parent and exits
      when it reaches EOF (a parent killed with `SIGKILL` leaves no orphan: tested); `RLIMIT_CORE` 0 in the child; signal decoding as Linux.
- [ ] **`FaultSupported` and `InjectFault`:** as Linux on x86-64 (`DivideByZero` traps there; the arm64 answer, false, is written and covered by the
      arm64 compile check of Day 310); `IllegalInstruction` with `ud2` on x86-64 and `brk` on arm64; expected reasons checked.
- [ ] **`IsDebuggerAttached`:** `sysctl(CTL_KERN, KERN_PROC, KERN_PROC_PID)` and `P_TRACED`; tested under `lldb` by the existing debugger test (skips
      without `lldb`).
- [ ] **Crash reports and the system:** the macOS crash reporter (`ReportCrash`) also writes `~/Library/Logs/DiagnosticReports`; the death tests must
      not wait on it (measure a crashing child's exit time with and without the handler; record).
- [ ] **Conformance groups** *assert*, *crash report*, *death tests*, *fault injection*, *threads* pass or skip with a reason; the `crash` label tests run
      with `--crashes`.
- [ ] **Documentation:** `BACKEND.md` "as built (Day 291)" notes; `foundation-debugging.md` macOS section (Mach exceptions, ReportCrash); `docgen lint`
      zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --crashes --flavor all` (quote); shared-file runs filed.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: signal safety of the hook, the pipe-watching child's races (parent dies before the child
      reads), the vendored headers matching the pinned version.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md` (close the Phase 7 Day 95 macOS item); commit and push.

**Done:** a crash on macOS (segfault, abort, assert, stack overflow on a worker) writes our report through Breakpad's `on_crash` hook with no network
symbol present, death tests leave no orphan, the five conformance groups pass or skip with a reason in three flavors, and the Breakpad headers are
vendored with their hashes.
