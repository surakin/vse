# Crash handler chaining / Day 2: Windows: the previous unhandled-exception filter (the Windows PC)

**Series:** [the crash handler should chain to the handler installed before it](../host-crash-handler-chaining.md)
**Depends on:** Day 1 (the shape and the tests); Phase 13 Day 155 (`os/windows/debug/crash_hook.cpp`, the crash gate).

Started on the PC before Day 1 had landed (the user's call, 2026-11-19): the Windows route touches only `crash_hook.cpp` (no neutral
seam). Day 1 landed while this day ran; the rebase took its `crash_host.c` form (below) and updated its "Windows: not yet" notes.

## Tasks

- [x] **Read first:** what `sentry_init`'s Breakpad backend does with the filter it replaces (`SetUnhandledExceptionFilter`'s previous value),
  whether it calls it after the dump, and where our gate (`g_filter_before_gate`) sits; record the chain in the notes.
- [x] **Note the host's filter** before `sentry_init` (the value `SetUnhandledExceptionFilter` returns, or a read of it), never the gate or the
  library's own.
- [x] **Call it after the dump:** from `on_crash` (`OnLibraryCrash`, after the report) or from the gate once the library has written the dump,
  with the original `EXCEPTION_POINTERS`, once; its return value is ignored for the decision (the process ends with the original exception
  code as now).
- [x] **Tests:** the Day 1 cases with a filter set before (`SetUnhandledExceptionFilter`) instead of a signal handler: it runs once after the
  report and dump, an access violation and an `abort()` each reach it, install/uninstall/install does not call it twice, and `crash_host.c`'s
  Windows host sees its marker; plain and asan, MSVC and clang-cl. (An `abort()` reaches the host's `SIGABRT` handler, not its filter: see
  the notes.)
- [x] **The guide:** the Windows order and caveats (the filter runs on the faulting thread or on Breakpad's handler thread, as found).
- [x] **Standing rows:** as Day 1, on the PC. Runs by ctest directly: `scripts/test-phase.sh --affected` selects everything this phase,
  including the phase 8 reset-loop test, which is not to be run on this PC. Crash tests were included.
  - Before the rebase onto Day 1: `build-msvc` plain 4179 of 4183 passed (the known voice-cost snapshot and `Throughput`; the mouse
    test, which compares with the real cursor, failed 1 in 5 alone; this day's "answer ... does not loop" failed once and passed 5 of 5
    alone, both in `KNOWN-ISSUES.md`), asan 3596 of 3596; `build-clang-cl` plain 4212 of 4213 (voice-cost).
  - After the rebase: `build-msvc` plain 4192 of 4195 (voice-cost, `Throughput`, an asset async-load test once, 5 of 5 alone), asan
    3594 of 3596 (the abort-flags crash test once, 5 of 5 alone; and the phase 8 reset loop, which these asan runs did not leave out by
    mistake: it passed before and failed after; the PC did not freeze), `ctest -L docs` 8 of 8; `build-clang-cl` 243 of 243 of
    phases 7 and 23 and the crash tests; `engine_library` 89 of 89 in `build-msvc-shared` and `build-clang-cl-shared`.
  - After the second rebase (onto Day 3, the Mac, whose `crash_host.c` also asks `host_crash_child_died_of_segv`: on Windows,
    the access violation or exit 3, `host_os_windows.h`): all six trees clean; `-L "crash|phase7|phase23|docs"` 411 of 411 in
    `build-msvc` and 260 of 260 in `build-clang-cl`; `engine_library` 89 of 89 in both shared trees (`crash_host: the host's handler
    ran after the dump`).
  - Two loaded crash-only runs (`-L crash -j 16 --repeat until-fail`) each saw one child hang before any output (`KNOWN-ISSUES.md`).
  - Clean builds: MSVC and clang-cl, unity and not, static and shared. The rebase changed comments in `crash_handler.h` and
    `engine_c.h` (Windows is now chained), so `docs/reference` was regenerated (`docgen` lint passed).

## Notes

**The chain, read (2026-11-19).** At a hardware fault the top-level filter is our gate (`InstallCrashEventHook` put it over Breakpad's).
The gate passes the first crash to Breakpad's `HandleException`, which sets the top-level filter to its own predecessor (the gate again,
from `PrepareCrashEventHook`) while it works, writes the dump on its handler thread, where the library's callback calls `on_crash` (our
report), and then: **if the dump succeeded it returns `EXCEPTION_EXECUTE_HANDLER`** and calls nobody else
(`exception_handler.cc`, `HandleException`); only a failed dump calls its `previous_filter_`, the gate, which passed it on to the host's
filter. So the host's filter was lost for every crash that was dumped. The host's filter is already what the gate keeps as
`g_filter_before_gate` (recorded by `PrepareCrashEventHook`, never the gate itself; the library's own filter goes to `g_library_filter`).

**What changed (`crash_hook.cpp`).** When the library's filter returns `EXCEPTION_EXECUTE_HANDLER`, the gate calls the host's filter, on
the faulting thread (the gate runs there; the report runs on Breakpad's thread before it), with the original `EXCEPTION_POINTERS`, then
returns `EXCEPTION_EXECUTE_HANDLER` whatever the host answered: the process ends with the original code. Once per crash: an atomic flag
(`g_host_filter_called`, reset by each install) also covers the library-gave-it-back path, so the host's filter never runs twice; there, a
second exception on the crashing thread after the host's filter ran ends the process instead of passing it on again. A fault inside the
host's filter ends its call (`__try`, one line on standard error), not a loop. Order: report, dump, the host's filter, the end of the
process with the original exception. Nothing changes without a host filter.

**`abort()`.** It raises no exception, so no unhandled-exception filter sees it, with or without the engine (the C runtime's `abort` calls
the `SIGABRT` handler, then ends the process; the engine clears `_CALL_REPORTFAULT`). The library's `SIGABRT` handler (`sentry_os.c`) already
calls the one it replaced after the report and the dump, then `TerminateProcess(3)`: a host's `SIGABRT` handler set before the install
runs, once. Calling the host's filter for an abort would be more than the host gets without the engine, so it is not done; the plan's
"an `abort()` reaches it" is met by the `SIGABRT` handler, and the guide says to set one. The test pins both (the handler once, the filter
not at all).

**Tests (`tests/debug/crash_end_to_end_windows_test.cpp`, death tests).**
- a filter set before: runs once, after `=== END OF REPORT ===`, on the faulting thread, with code `0xc0000005` and the fault address the
  report names; the envelope with a minidump exists; the process ends with the access violation;
- its answer `EXCEPTION_CONTINUE_EXECUTION` does not keep the process alive; a filter that itself faults: one report, the line, no loop;
- another thread faulting while the filter runs: one report, one envelope (the review's finding 1);
- a fault on a registered worker: the filter runs on that thread;
- install, uninstall, install; and a refused second install (`AlreadyInstalled`): each calls the filter once;
- `abort()` with a `SIGABRT` handler and a filter set before: the handler once after the report, the filter never, exit code 3.

Without the change in `crash_hook.cpp` the four filter tests fail (the abort one passes: the library already chained `SIGABRT`).
`tests/engine_library/crash_host.c` (Day 1's form): the crash child sets the marker (`host_set_crash_marker`) and installs the host's
handlers before `crash_install`; on Windows `host_install_own_crash_handler` installs the filter and a `SIGABRT` handler, either of
which writes the marker, and `host_os_windows.h` sets `ENGINE_HOST_HANDLER_CHAINED` to 1. Seen before the rebase: the filter's
marker in `build-msvc-shared`.

**Docs.** `docs/guide/foundation-debugging.md` ("If your program has its own unhandled-exception filter (Windows)", and the install bullet),
`docs/guide/engine-library.md` (the crash handler: the host's earlier handler, and `crash.dump_directory` given at creation, where the
envelope and `crash-report.txt` land), `engine/foundation/os/windows/debug/BACKEND.md` (the gate's new step). No public header changed.

## Post-implementation code review

`/code-review high` over the day's diff, with care for the fault path (reentrancy, once-only, what runs on which thread): 2 findings.

1. **Fixed: a second thread faulting while the host's filter runs was dumped and reported again.** Breakpad, returning from its filter,
   makes its own filter top-level again (`AutoExceptionHandler`'s destructor), not the gate; the host's filter now runs in that window, so
   another thread's fault went straight to Breakpad. The gate is put back on top before the host's filter is called. Regression test: the
   host's filter starts a thread that faults and waits a second; without the fix the child printed two reports (`2 == 1`), with it one
   report and one envelope.
2. **Fixed: a comment in `host_os_windows.h`** said the `SIGABRT` handler is the one that runs for a crash inside a job; the run showed the
   filter's marker. The comment now says either may run.

**Done when:** on Windows, a host filter set before `crash_handler_install` runs once after the report and dump with the original exception,
and the process still ends with the original code.
