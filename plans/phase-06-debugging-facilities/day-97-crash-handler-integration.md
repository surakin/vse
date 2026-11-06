# Phase 6 / Day 97 — Crash Handling End to End

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 92-96.

The library catches the fault and writes the dump (Day 95); our report (Day 96) must appear on stderr and next to the
dump, on every thread, without making things worse. This day connects them and proves the whole path, including the
cases that go wrong.

## Tasks

- [ ] Connect the report to the crash, the way Day 91's Spike D decided: either through the library's on-crash hook or attachment, or through a small signal handler of ours installed so it runs first, prints the Day 96 report to the report sink and to `<dump dir>/crash-<pid>-<time>.txt` (opened ahead of time), and then lets the library's handler run. Whichever it is, the contract is: the text report is written first and never blocks the dump, a fault inside the report path prints `double fault` and goes on to the dump, and the original fault is still delivered (the process exit status shows the original signal and a debugger sees the original fault).
- [ ] Coexistence rules written into code and the guide: install order, what happens when an application or a sanitizer installed a handler first (chain, do not replace), and `UninstallCrashHandler` restoring the previous disposition. The sanitizer policy from Spike D: under ASan/TSan the library's handler is not installed unless asked, and a `SIGABRT` raised by `AssertFail` still produces our report because `AssertFail` printed it already.
- [ ] Threads: a fault on a job worker, on the I/O thread, on a plain `std::thread`, inside a driver thread and on the main thread each produces a dump holding every thread and a report naming the faulting thread; a stack overflow produces a dump and a report (the library or our handler must run on an alternate stack; add a registration function for threads that need one if Spike A showed it). Two threads faulting at once produce one report.
- [ ] Tests (death-test helper, child processes; written against the neutral API so Phases 8 and 9 run the same sources): `InjectFault` for each `FaultKind` gives the expected `CrashReason`, the report text on stderr (function name from Day 94, reason, build id, last log lines), one `.dmp` and one `.txt` in the dump directory, and the original signal in the exit status; stack overflow; fault on a worker thread and on a plain thread; a second fault inside a provider; an application handler installed before is called after our report; uninstall restores the default; `ENGINE_ASSERT` failing in a child ends with `SIGABRT`, our report, and a dump; run the set 20 times with no flaky result.
- [ ] Open each dump with `lldb` in an automated ctest (skips with a printed reason where `lldb` is missing): `lldb -b -c <dump> -o 'thread list' -o 'bt all' -o 'image list'` shows the faulting function on the faulting thread, the other threads, and the main module with the build id. Record the exact command lines in the guide.
- [ ] Debugger check (manual, recorded): a crashing program run under `gdb` stops at the original faulting instruction, not inside a handler; if no human has done it by the end of the day it goes to `KNOWN-ISSUES.md` as a check not yet done.
- [ ] Safety review of our own code in the crash path (the text report, if we have a handler of ours): it calls only the Day 94 and 96 functions and raw syscalls (`write`, `open`, `close`, `getpid`, `gettid`, `clock_gettime`); no `malloc`, `printf`, `std::`, `pthread_mutex_*`. A CMake-script gate in the style of `tools/raw_new_check` (`tools/signal_safety_check`, with fixtures planting each forbidden identifier) enforces it on those files.
- [ ] **Documentation:** `///` comments with `Thread-safety:` and `Fails:`; the guide gets "When the program crashes" (what you see, the files, how to read the exit status, the sanitizer table); reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [ ] Run the day's scoped tests (`--affected`, asan and tsan flavors) and quote the `ran X of Y tests` lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; the most signal-safety, lifetime and concurrency risk of the phase, so treat every finding seriously; record it here and in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** a crash on any thread, a stack overflow, an abort and a failed assert each print our report, leave a dump holding every thread that `lldb` opens, and end with the original signal; a fault in the report path cannot loop; the safety gate passes; the tests pass in plain, ASan and TSan and repeat 20 times without failure.
