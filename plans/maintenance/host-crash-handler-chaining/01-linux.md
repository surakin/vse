# Crash handler chaining / Day 1: Linux: chain to the host's handler; tests; the guide

**Series:** [the crash handler should chain to the handler installed before it](../host-crash-handler-chaining.md)
**Depends on:** Phase 7 (the crash handler, `debug/crash_handler_sentry.cpp`, `os/linux/debug/crash_signal.cpp`), Phase 23 (the C API's
`crash_handler_install`).

## Tasks

- [ ] **Note the host's handlers** (`crash_handler_sentry.cpp`, `os/linux/debug/crash_signal.cpp`, `debug/crash_event_platform.h`):
  - a platform call, before `sentry_init`, that reads each fatal signal's disposition (`kFatalSignals`) into a per-signal table of the host's
    handlers; an entry that is the recorder (`Recorder`) or Breakpad's handler is stored as none (check the latter by comparing with what the
    library installs: read the disposition again after `sentry_init`);
  - cleared by uninstall, noted again by the next install; an install that returns `AlreadyInstalled` changes nothing.
- [ ] **Call it after the dump** (in `Chain`/`Recorder`, between the SIGNAL-SAFE markers):
  - after the library's handler returns: if the host's handler for this signal is a function, set a "called" flag (an atomic exchange, once per
    process) and call it with the original `signo`, `siginfo` and context (`sa_sigaction` with `SA_SIGINFO`, else `sa_handler`); a fault inside
    it reaches the recorder again (`SA_NODEFER`), which sees the flag and goes straight to the default action;
  - `SIG_DFL` and `SIG_IGN`: nothing to call, as today;
  - then return or re-raise exactly as today (the default action Breakpad left ends the process with the original signal);
  - the double-fault and second-thread paths do not call it; the report says nothing new (the host's handler runs after it).
- [ ] **Tests** (`tests/debug/crash_end_to_end_linux_test.cpp`, death tests):
  - the existing "A handler installed before the crash handler is replaced by the library's and does not run" becomes "... runs after the
    report and the dump": the marker is printed after `=== CRASH REPORT ===`, an envelope with a minidump exists, the process dies of `SIGSEGV`;
  - two earlier handlers, `SIGSEGV` and `SIGABRT`: a null write calls only the `SIGSEGV` one, an `abort()` only the `SIGABRT` one;
  - an earlier `SA_SIGINFO` handler receives a non-null `siginfo` whose `si_addr` is the fault address;
  - an earlier handler that itself faults: no loop, the process dies of the original signal, the report and dump exist;
  - install, uninstall, install: the earlier handler runs once, not the recorder twice; a second install (`AlreadyInstalled`) does not chain to
    itself;
  - no earlier handler: nothing changes (the existing tests);
  - the C API: `tests/engine_library/crash_host.c` (its host already installs a `SIGSEGV` handler) checks the host's marker and the envelope
    after a crash inside a job, through `crash_handler_install` with `crash.dump_directory` given at creation; plain, asan and tsan where the
    crash tests run.
- [ ] **The guide** (`docs/guide/foundation-debugging.md`, "If your program has its own signal handler", and `docs/guide/engine-library.md`'s
  crash section): the order (report, dump, your handler, the original fault), that your handler runs in signal context on the faulting thread
  and must be signal-safe, that it runs once, that uninstall restores it; and how a C API host sets `crash.dump_directory` at creation and
  where the dump and `crash-report.txt` land.
- [ ] **The request answered:** a "Done" section in `plans/maintenance/host-crash-handler-chaining.md` with what changed and the
  the host-side steps.
- [ ] **Standing rows:**
  - `scripts/test-phase.sh --affected` (phase 7, the engine library), then `--flavor asan` and `--flavor tsan`, crash tests included
    (`--crash-tests` or the label the script uses); quote each `ran X of Y` line;
  - Clang unity, GCC non-unity, `signal_safety_check`, `platform_neutral_check`;
  - `/code-review high`, with care for the fault path (reentrancy, once-only, async-signal safety);
  - `STATUS.md`, `KNOWN-ISSUES.md` (Days 2 and 3 waiting on the PC and the Mac); commit and push.

## Notes

(written during the day)

**Done when:** on Linux, a host handler installed before `crash_handler_install` runs once after the report and the dump with the original
`siginfo`, the process still dies of the original signal, nothing changes without one, and the guide says so.
