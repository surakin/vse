# Phase 6 / Day 97 — Crash Handling End to End

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 92-96.

The library catches the fault and writes the dump (Day 95); our report (Day 96) must appear on stderr and next to the
dump, on every thread, without making things worse. Day 91 showed the library's `on_crash` hook runs in the crashing
process, so the report is written there and the engine installs no signal handler of its own. This day connects them and proves the whole path, including the
cases that go wrong.

## Tasks

- [x] Connect the report to the crash through the library's `on_crash` hook (Day 95 installs it): the hook writes the Day 96 report to the report sink and to the attached `crash-report.txt` before the library writes the dump; a fault inside the report path is caught by an atomic re-entry flag that prints `double fault` and returns so the dump still gets written; the original fault is still delivered (the process exit status shows the original signal and a debugger sees the original fault). The hook never blocks: it has a time budget (a `clock_gettime` check between sections) and stops writing sections when it is over.
- [x] Coexistence rules written into the guide and tested: Day 91 Spike D found that a handler installed **before** `sentry_init` is replaced by Breakpad's and never runs, and one installed after runs and chains; the engine installs none, and the guide says an application that wants its own handler must install it after `InstallCrashHandler` and chain to the previous one (a test does exactly that). `UninstallCrashHandler` restores the previous disposition. Sanitizer policy: the handler is off in the sanitizer flavors unless asked (their own reports are the artifact), a smoke test installs and crashes under ASan (Day 91: it worked), and the TSan result from Day 95 is applied; a `SIGABRT` raised by `AssertFail` still prints our report because `AssertFail` printed it before aborting.
- [x] Threads: a fault on a job worker, on the I/O thread, on a plain `std::thread`, inside a driver thread and on the main thread each produces a dump holding every thread and a report naming the faulting thread; a stack overflow produces a dump and a report (Day 91: it did, with the Breakpad backend; if a worker thread needs an alternate stack, add `RegisterThreadForCrashHandling`). Two threads faulting at once produce one report.
- [x] Tests (death-test helper, child processes; written against the neutral API so Phases 8 and 9 run the same sources): `InjectFault` for each `FaultKind` gives the expected `CrashReason`, the report text on stderr (function name from Day 94, reason, build id, last log lines), one envelope (from which `crash_extract` yields the `.dmp`) and one attached `.txt` in the dump directory, and the original signal in the exit status; stack overflow; fault on a worker thread and on a plain thread; a second fault inside a provider; an application handler installed before is called after our report; uninstall restores the default; `ENGINE_ASSERT` failing in a child ends with `SIGABRT`, our report, and a dump; run the set 20 times with no flaky result.
- [x] Open each extracted dump with `lldb` in an automated ctest (skips with a printed reason where `lldb` is missing): `lldb -b -c <dump> -o 'thread list' -o 'bt all' -o 'image list'` shows the faulting function on the faulting thread, the other threads, and the main module with the build id. Record the exact command lines in the guide.
- [ ] Debugger check (manual, recorded): a crashing program run under `gdb` stops at the original faulting instruction, not inside a handler; if no human has done it by the end of the day it goes to `KNOWN-ISSUES.md` as a check not yet done.
- [x] Safety review of our own code in the crash path (the `on_crash` hook and the report writer): it calls only the Day 94 and 96 functions and raw syscalls (`write`, `open`, `close`, `getpid`, `gettid`, `clock_gettime`); no `malloc`, `printf`, `std::`, `pthread_mutex_*`. A CMake-script gate in the style of `tools/raw_new_check` (`tools/signal_safety_check`, with fixtures planting each forbidden identifier) enforces it on those files.
- [x] **Documentation:** `///` comments with `Thread-safety:` and `Fails:`; the guide gets "When the program crashes" (what you see, the files, how to read the exit status, the sanitizer table); reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [x] Run the day's scoped tests (`--affected`, asan and tsan flavors) and quote the `ran X of Y tests` lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`; the most signal-safety, lifetime and concurrency risk of the phase, so treat every finding seriously; record it here and in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** a crash on any thread, a stack overflow, an abort and a failed assert each print our report, leave an envelope whose extracted dump holds every thread and opens in `lldb`, and end with the original signal; a fault in the report path cannot loop; the safety gate passes; the tests pass in plain, ASan and TSan and repeat 20 times without failure.


## Result

Phase 6 has 395 tests (49 new this day). Scoped runs: `ran 1016 of 2267 tests (phases: all; flavors: plain; display: included)`
(one known display failure, the X11 window test), `ran 758 of 2267 ... asan`, `ran 493 of 2267 ... tsan`. Also green in the non-unity,
clang and Release builds, Release with forced checks, 20 repeats of the phase under `-j16` with no failure, and `ctest -L docs`;
`docgen lint` at 2659/2659. `tools/signal_safety_check` passes on 6 marked regions and reports every planted violation.

**Design change from the task list (recorded here as the plan requires).** Day 91 Spike D concluded the library's `on_crash` hook
could write the report with no signal handler of ours. Reading the Breakpad backend showed two facts the spike did not hit:
on Linux the hook receives a null `uctx` (no signal, no fault address) and it runs in a helper thread that Breakpad clones, not on
the thread that faulted (so the thread name, the thread id and the stack are the wrong ones). So a small recorder
(`os/linux/debug/crash_signal.cpp`) is installed after `sentry_init`, in front of Breakpad's handlers: on the faulting thread it
records the signal, fault address, stack-overflow test and the stack trace, calls the neutral report writer, and then always calls
Breakpad's handler, which writes the dump. Nothing is replaced. The report is written to standard error and to
`<dump directory>/crash-report.txt` (the library attaches that file to the envelope).

Other facts found: a handler installed before `InstallCrashHandler` is replaced by the library's and never runs (test documents it);
the dump opens in `lldb` with the faulting function, every thread and the module build id (automated test; the commands are in the
guide). Two threads faulting at once exposed three races in the recorder, all fixed (a second thread chained to the library and ended
the process in the middle of the first thread's report; the same through a stale read of the owner), and the log-ring reader
let a torn line through about once in a hundred loaded runs (fixed: acquire loads).

Pages touched: `docs/guide/foundation-debugging.md` ("When the program crashes"), `docs/reference/foundation-debug.md`,
`docs/reference/platform-os.md`.

## Post-implementation code review

`/code-review high` found 10 issues.

1. The recorder captured a stack trace before `InitDebugFacilities` had necessarily run (its setup locks and allocates). **Fixed**: `InstallCrashHandler` runs it. The new test checks the visible behavior (frames in the first report) but cannot reproduce the deadlock, so it does not fail without the fix.
2. `SIG_IGN` as the previous disposition would loop on a hardware fault. **Fixed** (treated as the default; a report is also written at most once per process).
3. Stale hook state after something was installed over the recorder; the install result was ignored. **Fixed**: install reuses a recorder still in the chain, returns false if nothing could be installed, and the caller logs it.
4. `RegisterThreadForCrashHandling` replaced any alternate stack and disabled it unconditionally. **Fixed**: keeps an existing stack that is big enough, and disables only its own.
5. A stale `crash-report.txt` could be attached to a later envelope. **Fixed**: emptied at install (test, fails without the fix); an over-long path is logged.
6. The report to standard error was suppressed after a failed assert, which printed to standard output. **Fixed by removing the suppression**: the crash report now always follows the assert's output (a duplicate of the context is better than nothing on the console).
7. A thread waiting for the reporting thread waits up to 20 s with no check that the reporter is alive. **Declined as designed**: bounded, and the unbounded case (a provider that never returns) is recorded in `KNOWN-ISSUES.md`.
8. Short or interrupted writes dropped part of the report. **Fixed** (`WriteAll` retries).
9. "Acquire loads plus a release read-modify-write cannot keep the ring reader's loads before the re-check." **Declined**: an acquire load keeps every later operation, including the re-check, after it, which is the property needed; the comment is reworded.
10. `RegisterThreadForCrashHandling` has no stub for platforms without the file. **Deferred to Day 101**: the Windows and macOS `BACKEND.md` checklists list it.

Found by the day's own stress runs rather than the review: the three recorder races above (a deterministic test now covers the second-thread case and fails without the fix), and the TSan limitation in `KNOWN-ISSUES.md`.
