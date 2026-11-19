# Crash handler chaining / Day 1: Linux: chain to the host's handler; tests; the guide

**Series:** [the crash handler should chain to the handler installed before it](../host-crash-handler-chaining.md)
**Depends on:** Phase 7 (the crash handler, `debug/crash_handler_sentry.cpp`, `os/linux/debug/crash_signal.cpp`), Phase 23 (the C API's
`crash_handler_install`).

## Tasks

- [x] **Note the host's handlers** (`crash_handler_sentry.cpp`, `os/linux/debug/crash_signal.cpp`, `debug/crash_event_platform.h`):
  - a platform call, before `sentry_init`, that reads each fatal signal's disposition (`kFatalSignals`) into a per-signal table of the host's
    handlers; an entry that is the recorder (`Recorder`) or Breakpad's handler is stored as none (check the latter by comparing with what the
    library installs: read the disposition again after `sentry_init`);
  - cleared by uninstall, noted again by the next install; an install that returns `AlreadyInstalled` changes nothing.
- [x] **Call it after the dump** (in `Chain`/`Recorder`, between the SIGNAL-SAFE markers):
  - after the library's handler returns: if the host's handler for this signal is a function, set a "called" flag (an atomic exchange, once per
    process) and call it with the original `signo`, `siginfo` and context (`sa_sigaction` with `SA_SIGINFO`, else `sa_handler`); a fault inside
    it reaches the recorder again (`SA_NODEFER`), which sees the flag and goes straight to the default action;
  - `SIG_DFL` and `SIG_IGN`: nothing to call, as today;
  - then return or re-raise exactly as today (the default action Breakpad left ends the process with the original signal);
  - the double-fault and second-thread paths do not call it; the report says nothing new (the host's handler runs after it).
- [x] **Tests** (`tests/debug/crash_end_to_end_linux_test.cpp`, death tests):
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
- [x] **The guide** (`docs/guide/foundation-debugging.md`, "If your program has its own signal handler", and `docs/guide/engine-library.md`'s
  crash section): the order (report, dump, your handler, the original fault), that your handler runs in signal context on the faulting thread
  and must be signal-safe, that it runs once, that uninstall restores it; and how a C API host sets `crash.dump_directory` at creation and
  where the dump and `crash-report.txt` land.
- [x] **The request answered:** a "Done" section in `plans/maintenance/host-crash-handler-chaining.md` with what changed and the
  the host-side steps.
- [x] **Standing rows:**
  - `scripts/test-phase.sh --affected` (phase 7, the engine library), then `--flavor asan` and `--flavor tsan`, crash tests included
    (`--crash-tests` or the label the script uses); quote each `ran X of Y` line;
  - Clang unity, GCC non-unity, `signal_safety_check`, `platform_neutral_check`;
  - `/code-review high`, with care for the fault path (reentrancy, once-only, async-signal safety);
  - `STATUS.md`, `KNOWN-ISSUES.md` (Days 2 and 3 waiting on the PC and the Mac); commit and push.

## Notes

- **The change** (`os/linux/debug/crash_signal.cpp`, `debug/crash_handler_sentry.cpp`, `debug/crash_event_platform.h`):
  - `detail::NoteHostSignalHandlers` (Linux only) reads each fatal signal's disposition just before `sentry_init`. `SIG_DFL`, `SIG_IGN` and
    the recorder itself are noted as none; `RemoveCrashEventHook` forgets them.
  - After the report the recorder calls the library (the dump), then `CallHostHandler`: once per process (an atomic exchange), with the
    fault's `signo`, `siginfo` and context.
  - It does not call the host's handler when that handler is what the recorder chains to anyway (the library installed nothing over it).
  - It also does not call it when the library put it back because the dump failed (the review's finding, below): the fault delivered again
    reaches it by itself.
- **The re-raised signals:** Breakpad sends `SIGABRT` (and any signal from `kill`/`raise`) again after its dump, and the recorder runs with
  `SA_NODEFER`, so that re-send would kill the process before the host's handler.
  - The recorder now blocks the signal for those cases before calling the library; the kernel restores the mask when the recorder returns,
    and the held signal ends the process then.
  - Hardware faults are not blocked: a fault inside the library or the host's handler must still reach the recorder.
- **A fault inside the host's handler** takes the existing "give up" path (`g_chaining_thread` now covers the host's call as well as the
  library's): default action, no second report. The message is now "double fault: a handler after the crash report faulted; giving up".
- **ASan:** with `handle_segv=1`, ASan's handler is in place before the engine's and is now called after the dump (its report, then its
  exit code). `engine_c_runtime_asan` (whose crash child expects the fault) now runs with `ASAN_OPTIONS=handle_segv=0`, as the death tests
  do; the guide says so.
- **Tests** (`tests/debug/crash_end_to_end_linux_test.cpp`):
  - the old "... is replaced by the library's and does not run" became "A handler installed before the crash handler runs once, after the
    report and the dump" (the host's style of handler: a marker, `SIG_DFL`, raise);
  - "Earlier handlers on two signals: each runs for its own signal only" (a null write and an `abort()`);
  - "An earlier SA_SIGINFO handler gets the fault's siginfo, with its address" (`si_addr` 0x1238);
  - "An earlier handler that faults itself ends the process with the original signal, once";
  - "Install, uninstall, install: ..." (and a second install is refused);
  - "When the dump cannot be written, the earlier handler still runs once, after the report" (the run directories made read-only).
  - **Shown failing without the change:** with the host's call disabled, the first five fail (45 of 56 passed, 5 failed); without the
    hold, the two-signal test fails. The dump-failure test checks the behavior, but it passes without the review's fix as well (a handler
    that raises ends the process at its first call either way).
  - **The C API:** `crash_host.c` (`engine_crash_across_boundary`, shared builds) installs the host's `SIGSEGV` handler in its crash child
    before `crash_install` and checks the handler's marker beside the envelope (`ENGINE_HOST_HANDLER_CHAINED`, Linux only); without the
    change it fails ("the host's handler ... did not run after the dump").
  - **the host's repro** from the request, built against `libengine.so.1` of this tree: `prev-ran.txt` and the engine's envelope are both
    there, and the child dies of signal 11.
- **Docs:**
  - `docs/guide/foundation-debugging.md`, "If your program has its own signal handler": the order, signal context, once, its own signal,
    sanitizers, not yet on Windows and macOS;
  - `docs/guide/engine-library.md`: the dump directory given at creation, and where the files land;
  - the `InstallCrashHandler` and `crash_handler_install` comments;
  - `docs/reference` regenerated.

## Runs

- `scripts/test-phase.sh --affected --flavor all --crashes`: `ran 11437 of 11437 tests (phases: all; flavors: all; display: included; crash
  tests: included) in 730 s`.
  - Failures, all known (`KNOWN-ISSUES.md`):
    - the `ENGINE_ASSERT`/`ENGINE_ENSURE` crash tests (three flavors) and the tsan stack overflow, from the Phase 27 close;
    - `renderer_demo_crash_after_reset` (Phase 26 close);
    - the Job-destroyed test (three flavors), `engine_c_surface`, the voice-cost snapshot, and `script_bench_check` (a timing bound under
      load).
- **After the review's fix**, on the final code:
  - the crash tests (`debug_tests "[crash]"`): plain 51 of 57 with 6 skipped, asan 46 of 51 with 5 skipped;
  - tsan: the known stack-overflow test failed in each of three runs. A second tsan crash test failed in the first of those runs only; its
    name was not kept;
  - `engine_c_runtime` in three flavors, `engine_crash_across_boundary` (shared tree), the gates and `ctest -L docs`: pass.
- GCC non-unity: clean.

## Post-implementation code review

`/code-review high` on the day: 1 finding, fixed.
- When Breakpad cannot write the dump it restores the handlers it replaced, the host's among them. The host's handler would then have been
  called by the recorder and again by the fault delivered once more.
- `CallHostHandler` now leaves it alone when it is the current disposition. A test of the dump-failure path was added; as noted above, it
  does not tell the fix apart.
- Checked, no change: the async-signal safety of the new path (`sigaction`, `sigprocmask` and atomics only), the once-only flag across
  install/uninstall, and the second-thread and double-fault paths (they never call the host's handler).

**Done when:** on Linux, a host handler installed before `crash_handler_install` runs once after the report and the dump with the original
`siginfo`, the process still dies of the original signal, nothing changes without one, and the guide says so.
