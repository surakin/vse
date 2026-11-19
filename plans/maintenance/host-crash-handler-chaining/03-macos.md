# Crash handler chaining / Day 3: macOS: the host's signal handlers after a Mach exception (the Mac)

**Series:** [the crash handler should chain to the handler installed before it](../host-crash-handler-chaining.md)
**Depends on:** Day 1; Phase 21 Day 291 (`os/macos/debug/crash_hook.cpp`: Breakpad takes faults as Mach exceptions on its own thread and
aborts through a `SIGABRT` handler).

## Tasks

- [x] **Read first:** with Breakpad holding the task's exception ports, a hardware fault never becomes a signal while the handler is in, so a
  host's `SIGSEGV` handler is never reached; find what Breakpad does after the dump (forward to the previous exception ports, or let the
  thread die) and record it. Decide between: (a) after the dump, restore the host's signal handlers and let the fault be delivered as a
  signal (the thread resumed with the exception unhandled, so the kernel turns it into the BSD signal), or (b) call the host's handler from
  Breakpad's thread (wrong thread: rejected unless (a) cannot work). Record the decision.
- [x] **Note the host's handlers** (signals, and the previous exception ports if (a) needs them) before `sentry_init`.
- [x] **Chain:** per the decision; `abort()` (the `SIGABRT` path, on the crashing thread) calls the host's `SIGABRT` handler as on Linux.
- [x] **Tests:** the Day 1 cases on macOS (x86-64 runs; arm64 compiles), and `crash_host.c`'s macOS host.
- [x] **The guide:** the macOS order and what differs (the thread the handler runs on, if it does).
- [x] **Standing rows:** as Day 1, on the Mac (plain only, the Mac's rule).

**Done when:** on macOS, a host handler installed before `crash_handler_install` runs once after the report and dump for a hardware fault and
for an abort, and the process still dies of the original signal; or, if (a) proves impossible, the limit is documented and recorded.

## Notes (the Mac, 2026-11-19)

- **Read first: what Breakpad does after the dump.** For a Mach exception (on its own thread) and for an abort (its `SIGABRT` handler, on the
  aborting thread) it calls sentry's minidump callback with `exit_after_write` set; the callback (our `on_crash`, the report, then the envelope)
  returns `succeeded`, and Breakpad then `_exit`s with the exception type: exit code 1 for `EXC_BAD_ACCESS`, 5 for an abort
  (`exception_handler.cc`, `WriteMinidumpWithException`). It never forwards the fault and never restores the host's handlers first, so no host
  handler can run, and option (a) cannot work with Breakpad as vendored. The two ways without a change were (b) (the host's handler on
  Breakpad's thread, before the envelope: a handler that raises loses the envelope) and documenting the limit.
- **Decision (the user, 2026-11-19): (a) with a local change to the vendored Breakpad** (`third_party/sentry_native/VENDORED.md`, "Local
  changes"). A switch `breakpad_mac_continue_after_dump`, set by the engine during the callback only when the host has a handler for the crash's
  signal: no `_exit` after the dump; after a Mach exception, resume the threads Breakpad suspended and forward the exception itself (to the ports
  from before it, or unhandled), so the kernel delivers the BSD signal on the faulting thread; after an abort, return from the `SIGABRT` handler.
- **The engine side** (`os/macos/debug/crash_hook.cpp`): `NoteHostSignalHandlers` (now Linux and macOS) reads the dispositions before
  `sentry_init`. After the report, `ArmHostRelay` puts a one-shot relay on the crash's signals that have a host handler and sets the switch: an
  access fault arms `SIGSEGV` and `SIGBUS` (the kernel chooses), an illegal instruction `SIGILL`, a divide `SIGFPE`, a breakpoint `SIGTRAP`, an
  unknown thread all of them, an abort `SIGABRT` (plus a `raise(SIGABRT)` that stays blocked until Breakpad's handler returns, so the relay runs
  after the envelope). The relay puts the default action back, calls the host's handler once with the signal's own `siginfo` and context, and
  re-raises a signal no faulting instruction will repeat (an abort). Order: **report, dump and envelope, the host's handler, the original fault**,
  as on Linux; the report runs on Breakpad's thread for a fault (as before), the host's handler on the faulting thread.
- **Found while testing:**
  - a fault inside the host's handler hung: the relay ran with the signal blocked, so the faulting instruction faulted forever; the relay now
    has `SA_NODEFER`, and the fault reaches the default action;
  - in the shared library the forwarded exception never reached Breakpad's `catch_exception_raise`: libSystem's `exc_server` finds it by symbol,
    and the library exports only `engine_get_api`, so the child exited with code 1 instead of the signal. With the switch on, Breakpad now
    forwards with `ForwardException` directly and builds the reply itself;
  - a death-test child runs inside a Catch2 test case, so Catch2's fatal-signal handlers are earlier handlers there: every macOS crash test
    now chains to them after the dump. The no-chaining test restores `SIGSEGV` and `SIGBUS` to the default first.
- **Tests:**
  - `tests/debug/macos/crash_end_to_end_macos_test.cpp`: "a handler installed before the crash handler runs once, after the report and the
    dump", "earlier handlers on two signals: each runs for its own signal only" (a null write and an `abort()`), "an earlier SA_SIGINFO handler
    gets the fault's siginfo, with its address, on the faulting thread" (`si_addr` 0x1238), "an earlier handler that faults itself ends the
    process with the original signal, once", "install, uninstall, install: ...", and "with no earlier handler for the crash's signal, the process
    ends as before (no chaining)" (exit code 1).
  - **Shown failing without the change:** with the arming disabled, the five chaining tests fail (no marker; exit code 1, not the signal) and
    the no-chaining test passes.
  - `tests/engine_library/crash_host.c` (`engine_crash_across_boundary`, shared tree): `ENGINE_HOST_HANDLER_CHAINED` is on for macOS; the crash
    child installs a `SIGSEGV` handler (`host_install_own_fault_handler`) besides the `SIGABRT` one, and the parent finds its marker beside the
    envelope. It failed before the `ForwardException` change (exit code 1) and passes after it. `ProbeEndedByFault` accepts `SIGSEGV` on macOS
    too (a chained crash).
- **Docs:** `docs/guide/foundation-debugging.md` ("If your program has its own signal handler": the macOS order and what differs),
  `docs/guide/engine-library.md`, the `InstallCrashHandler` comment (`crash_handler.h`) and `crash_handler_install` (`engine_c.h`),
  `os/macos/debug/BACKEND.md`, `third_party/sentry_native/VENDORED.md`; `docs/reference` regenerated (`foundation-debug.md`).

## Runs (the Mac, 2026-11-19; plain only, unity only, the Mac's rules; the console was locked, so no display tests)

- `scripts/test-phase.sh --affected --crashes --no-display` (everything: foundation headers changed): **`ran 4139 of 11254 tests (phases: all;
  flavors: plain; display: excluded; crash tests: included) in 946 s`**; one failure, the known `Snapshot: the cost of a voice at each quality,
  with and without effects` (the Debug bound, `KNOWN-ISSUES.md`). A reduced run: no sanitizer flavors.
- After the review's fixes: the macOS crash tests (`-R macOS -L crash`) 36 of 36; phase 7 plain, crash tests included, 159 of 159; the
  shared tree's `engine_crash_across_boundary` and `engine_c_runtime` 2 of 2.
- Built clean (warnings are errors): `build-macos` (x86-64, unity), `build-macos-shared` (the targets above), `build-macos-arm64`
  (compile only). Gates (`signal_safety_check`, `platform_neutral_check`, `std_usage_check`, `file_access_check`, `debug_backend_lists`,
  `docs_check` and the rest by name): 29 of 29. `ctest -L docs`: 7 of 7.

## Post-implementation code review

`/code-review high` over the day's diff: 10 findings; 8 fixed, 1 declined, 1 recorded.

- **Fixed:**
  - the relay did not raise a `SIGTRAP` again (the PC is past `int3`, so a returning host handler let the thread go on), nor any signal on a
    second call, and took a signal sent with `kill`/`raise` for a fault, because macOS's `SI_USER` is positive (0x10001): `RepeatsByItself`
    decides now, and the relay raises every signal nothing repeats, first call or not;
  - a Mach trap this file could not classify (reason `Abort`: an unknown trap number, an alignment fault) armed only `SIGTRAP`: it arms every
    hardware signal now;
  - on the abort path Breakpad is still installed when the host's `SIGABRT` handler runs, so a fault inside it gave a second dump and
    envelope: the relay clears the task's fault ports before the host's call. New test `an earlier SIGABRT handler that faults ends the process
    without a second dump`, shown failing without the fix (two envelopes, an exit instead of a signal);
  - the switch stayed set after the crash: the relay clears it;
  - `SA_NODEFER` on every relay changed a host `SIGABRT` handler's deferral (a `raise` inside it took effect at once): only the fault relays
    have it now, as Linux's recorder does for faults and holds an abort;
  - `ProbeEndedByFault` accepting `_exit(1)` or `SIGSEGV` let a chained crash that ended by `_exit` pass: `crash_host.c` now also requires
    `SIGSEGV` after the host's handler (`host_crash_child_died_of_segv`), and `host_run_crash_child`'s comment says what macOS ends with;
  - `STATUS.md` and the `ran` line were missing: added.
- **Declined:** none beyond the one below.
- **Recorded, not fixed** (`KNOWN-ISSUES.md`): the host-handler code (note, classify, call) is a near copy of Linux's
  (`os/linux/debug/crash_signal.cpp`); a shared POSIX helper would change Linux code, which this machine cannot build.
- **Not a defect:** `SA_NODEFER` for the fault relays is kept (without it a fault inside the host's handler of the same kind loops forever on
  the blocked signal, the hang the tests found); it matches Linux.

**Done:** on macOS a host handler installed before `crash_handler_install` runs once after the report and the dump (and the envelope), for a
hardware fault and for an abort, on the faulting thread with the kernel's `siginfo`, and the process dies of the original signal; with no host
handler for the crash's signal nothing changes. Through the vendored Breakpad's local change (the user's decision).

