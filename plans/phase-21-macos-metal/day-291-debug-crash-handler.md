# Phase 21 / Day 291 — Debugging II: Crash Handler, Death Tests, Fault Injection

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 290.

sentry-native's Breakpad backend on macOS (Mach exception ports, no transport), the report written from `on_crash`, death tests without
`PR_SET_PDEATHSIG`, fault injection and the debugger check.

## Tasks

- [x] **Re-vendor Breakpad's macOS private headers** (`KNOWN-ISSUES.md`, Phase 7 Day 95): the files sentry's CMake needs for `client/mac` and
      `common/mac` at the pinned sentry-native 0.17.6, recorded in the vendoring notes with their hashes; nothing else under `third_party/` changes.
- [x] **Crash library build:** `SENTRY_BACKEND=breakpad`, `SENTRY_TRANSPORT=none` on macOS; the no-network check (`debug_no_network_*`) learns Mach-O
      (`nm -u` instead of the ELF reader) and passes for the test binaries and `crash_extract`.
- [x] **Report from `on_crash`** (`crash_handler_sentry.cpp`'s macOS branch): the hook receives a `ucontext` on macOS; write the report from it, do not
      install the Linux signal recorder; `detail::InstallCrashEventHook` maps to the hook. The report's stack comes from the context's registers.
      Everything between `SIGNAL-SAFE-BEGIN`/`END` stays signal-safe (the hook runs on Breakpad's exception thread, so the rules still apply).
- [x] **`RegisterThreadForCrashHandling`:** sets the thread's name and returns true (Mach exceptions need no alternate stack); a stack overflow on a worker
      thread produces a report (tested).
- [x] **`DeathTestsSupported` and `RunInChild`:** `posix_spawn` shared with Linux in `os/posix/debug/` (macOS has no `pipe2`: `pipe` then `FD_CLOEXEC`,
      also in `tests/debug/support/test_support_linux.h`, which becomes `_posix`); the child watches a pipe from the parent and exits
      when it reaches EOF (a parent killed with `SIGKILL` leaves no orphan: tested); `RLIMIT_CORE` 0 in the child; signal decoding as Linux.
- [x] **`FaultSupported` and `InjectFault`:** as Linux on x86-64 (`DivideByZero` traps there; the arm64 answer, false, is written and covered by the
      arm64 compile check of Day 311); `IllegalInstruction` with `ud2` on x86-64 and `brk` on arm64; expected reasons checked.
- [x] **`IsDebuggerAttached`:** `sysctl(CTL_KERN, KERN_PROC, KERN_PROC_PID)` and `P_TRACED`; tested under `lldb` by the existing debugger test (skips
      without `lldb`).
- [x] **Crash reports and the system:** the macOS crash reporter (`ReportCrash`) also writes `~/Library/Logs/DiagnosticReports`; the death tests must
      not wait on it (measure a crashing child's exit time with and without the handler; record).
- [x] **Conformance groups** *assert*, *crash report*, *death tests*, *fault injection*, *threads* pass or skip with a reason; the `crash` label tests run
      with `--crashes`.
- [x] **Documentation:** `BACKEND.md` "as built (Day 291)" notes; `foundation-debugging.md` macOS section (Mach exceptions, ReportCrash); `docgen lint`
      zero.
- [x] **Runs:** `scripts/test-phase.sh --affected --crashes --flavor all` (quote); shared-file runs filed.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: signal safety of the hook, the pipe-watching child's races (parent dies before the child
      reads), the vendored headers matching the pinned version.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md` (close the Phase 7 Day 95 macOS item); commit and push.

**Done:** a crash on macOS (segfault, abort, assert, stack overflow on a worker) writes our report through Breakpad's `on_crash` hook with no network
symbol present, death tests leave no orphan, the five conformance groups pass or skip with a reason in three flavors, and the Breakpad headers are
vendored with their hashes.

## Result (2026-11-11, the Mac)

- **Breakpad's macOS headers:** the pinned `sentry-native.zip` (0.17.6) downloaded again, its SHA-256 matching the recorded one; four private
  headers copied unmodified, found by building `sentry` until no include was missing, each with its hash in `third_party/sentry_native/VENDORED.md`.
  Nothing else under `third_party/` changed.
- **The crash library** builds on macOS (`SENTRY_BACKEND=breakpad`, `SENTRY_TRANSPORT=none`) and `foundation_debug` links it; the temporary
  `not_yet_ported.cpp` of Day 290 is gone.
- **The report from `on_crash`** (`os/macos/debug/crash_hook.cpp`; `crash_handler_sentry.cpp` sets the hook on macOS): sentry passes only the
  crashed thread's register context. Built from it: the abort path (the context's stack pointer on the current thread's stack) or a Mach exception,
  whose thread is found among the task's threads by its registers. **Found: the context's exception state is zero** (Breakpad fills only the
  thread state), so every fault first read as `ArithmeticFault` (trap 0); the reason and fault address now come from that thread's own exception
  state (`x86_EXCEPTION_STATE64`; `ARM_EXCEPTION_STATE64` on arm64). The stack is walked from the frame pointer through `vm_read_overwrite`. A bus
  error is a fault in a mapped no-access region above `__PAGEZERO`, as macOS's own signal says.
- **Found: ASan stopped every crashing child** with a `stack-buffer-underflow` in Breakpad's `MinidumpFileWriter::Copy`: Breakpad copies the crashed
  stack with libc `write()`, and ASan's interceptor checks the redzones (Linux's Breakpad uses raw syscalls). Suppressed for that call through
  `__asan_default_suppressions` in the hook's object (ASan builds only).
- **Thread registration:** the engine's crash name and the pthread name (the report reads the latter from Breakpad's thread); returns whether the
  handler is installed.
- **Death tests:** Linux's backend moved to `os/posix/debug/death_test_posix.cpp` with hooks (`posix_debug.h`): `pipe`+`FD_CLOEXEC`,
  `_NSGetExecutablePath`, `_NSGetEnviron`, and on macOS a **parent-watch pipe** (descriptor 3 in the child; a thread there kills it at end of file).
  Tested: a child whose parent is killed with SIGKILL is gone (`macOS: a death-test child does not outlive a parent killed with SIGKILL`).
  `tests/debug/support/test_support_linux.h` is now `test_support_posix.h`.
- **Fault injection:** Linux's, with the bus error from a `PROT_NONE` page. **Debugger:** `sysctl` and `P_TRACED`; true under `lldb` (checked by hand:
  the normal-run case skipped under `lldb --batch -o run`) and in a `PT_TRACE_ME` child (tested).
- **Post-mortem dumps** (brought forward from Day 292: the post-mortem death tests started to run today and needed it): `core_dump_macos.cpp`;
  `Limited` here (`/cores` is root's).
- **No network:** `check_no_network_macos.cmake` (`otool -L`, `nm -u`; also CFNetwork, Network.framework and `NSURLSession`/`NSURLConnection`):
  passes for `debug_tests` in three flavors, `debug_checks_off_tests` and `crash_extract`; two planted fixtures fail for the right reason.
- **Crash reports and the system, measured:** a crashing child without the handler ended in 35-40 ms (signal 11); with it in 252-269 ms (exit code 1:
  Breakpad's `_exit(EXC_BAD_ACCESS)` after the dump, so `ReportCrash` is not involved and nothing waits on it).
- **Conformance groups** *assert*, *crash report*, *death tests*, *fault injection*, *threads*: pass in three flavors (`tests/debug`, with the new
  `macos/crash_end_to_end_macos_test.cpp` and `macos/debugger_macos_test.cpp`).
- **Runs (macOS, `/bin/bash scripts/test-phase.sh --all --flavor all`, crash tests included):** before the review unity `ran 1288 of 1288 tests
  (phases: all; flavors: all; display: included; crash tests: included)`, non-unity the same; after it `ran 1291 of 1291` in both modes, the TSan
  death-test timeout the one failure; in both the only failures were the two load-sensitive timing tests recorded in
  `KNOWN-ISSUES.md` (`Throughput scales with participant count`; the TSan death-test timeout, whose cause is TSan's start-up time under load). No
  warnings.

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings. **9 fixed, 1 in part.**

1. **Fixed (a real hang, with a test shown to fail first): a fault inside the report hung the process.** The report runs on Breakpad's exception
   thread; a second fault there went to the port only that thread serves. The task's exception ports for the fault types are now cleared while the
   report runs on that thread (restored after), so such a fault becomes a signal that ends the process. `macOS: a state provider that faults inside
   the report ends the process instead of hanging it` timed out before and passes.
2. **Fixed: the thread's name was read with `pthread_from_mach_thread_np`/`pthread_getname_np`**, which take libpthread's thread-list lock a
   suspended thread may hold: now `thread_info(THREAD_EXTENDED_INFO)`, a Mach call.
3. **In part: telling the abort path from a Mach exception by the stack pointer.** A context on the current thread's signal stack now counts as the
   abort path too (an assert in a handler running on `sigaltstack`). Passing Breakpad's exception type and thread through `on_crash` would remove
   the guess but means changing the vendored sentry (declined: `third_party/` stays unmodified).
4. **Fixed: `_NSGetExecutablePath` can be relative or a link**: the death tests spawn `proc_pidpath`'s absolute path.
5. **Fixed: the `pipe`-to-`fcntl` window could leak a pipe end into another thread's child**: macOS spawns with `POSIX_SPAWN_CLOEXEC_DEFAULT`
   (standard input inherited explicitly), through a new hook `PrepareSpawnFlags` (Linux: none).
6. **Fixed: the parent watch killed the child on any read error and left descriptor 3 inheritable**: it kills only at end of file, and descriptor 3
   is close-on-exec in the child.
7. **Fixed: `ARCHITECTURE.md`** names `os/posix/debug/`.
8. **Fixed: `fault_injection.cpp` was a copy of Linux's**: one `os/posix/debug/fault_injection_posix.cpp` with a `TriggerBusError` hook (Linux:
   memfd; macOS: a `PROT_NONE` page), `::` prefixes.
9. **Fixed: duplicated helpers**: `CopyInto` is `CStrCopyTo`; the thread-number rule is one header (`os/macos/debug/thread_id_macos.h`) for the crash
   context and the hook; the test helper's pipe checks `fcntl`. (The e2e test's `TempDir` stays a local copy, as in the Linux and Windows tests.)
10. **Fixed: the sentry configuration was copied in three places**: `engine_add_sentry()` in `cmake/EngineSentry.cmake`, called by each platform.

Found while fixing: the new traced-child test, run in one process with others, left tracer state behind (the shared "Repeated children leave no
process or descriptor behind" failed after it); the traced child now runs one level down.
