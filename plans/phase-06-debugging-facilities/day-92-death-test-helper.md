# Phase 6 / Day 92 — Death-Test Helper & the `tests/debug` Suite

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Day 91.

Every later day tests code that aborts or crashes. Build the helper that runs a function in a child process and reports
what happened, and the test target that holds this phase's tests, first. The helper **re-executes the test binary**
instead of calling `fork`: `fork` does not exist on Windows, and in a multithreaded parent it leaves the child with
locks it can never release, so a re-exec model is both portable and safer. Platforms that cannot start a process (iOS,
consoles) report `Unsupported` and the death tests skip there.

## Tasks

- [x] Add `engine/foundation/debug/death_test.h` with `RunInChild(name, argument, options) -> ChildResult`: starts a new process of the running executable with the environment variable `ENGINE_DEATH_TEST_CHILD=<name>` and `<argument>` (a short string the child function parses), runs the function registered under `name` there, and returns `exited`/`exit_code`, `signaled`/`signal`, `timed_out`, and the captured stderr and stdout text (fixed-capacity buffer, truncation flagged). `options`: timeout in milliseconds (default 10 s, the child is killed with `SIGKILL` and `timed_out` set), whether to capture stdout, extra environment entries, and whether to disable core dumps in the child (default yes, so tests do not fill the disk).
- [x] Registration: `ENGINE_DEATH_TEST_FN(name, argument_text)` defines a function and registers it by name in a static table; a hidden Catch2 test case (`[.death-child]`, defined once in the library `debug_test_support` that `tests/debug` and other test targets link) reads `ENGINE_DEATH_TEST_CHILD`, looks the function up, runs it, and leaves through `_exit(0)` so no reporter, atexit handler or static destructor runs in the child. The parent starts the child with the test binary's own path and the filter `[.death-child]`, so the child needs no change to `main` and works with `Catch2WithMain`.
- [x] Implement the spawn for Linux in `engine/foundation/os/linux/debug/death_test.cpp`: `pipe2` with `O_CLOEXEC`, `posix_spawn` with file actions that redirect the child's stdout/stderr to the pipes (the executable path from `/proc/self/exe`), `poll` with a deadline in the parent while draining the pipes (never a blocking read after the child dies without closing, never a deadlock when the child fills the pipe), `waitpid` retried on `EINTR`, and reaping on every path. The platform-neutral part (`ChildResult`, registration, the hidden test case, the options) is in `debug/`; only the spawn/wait/pipe code is per platform, so Windows adds `CreateProcess` + job objects and macOS reuses the POSIX code.
- [x] What the child inherits: a fresh process image, so nothing from the parent except the environment and the two pipes; the parent's threads, locks and allocator state do not matter (this is why re-exec replaces `fork`). Document that the child function gets only its argument string; larger inputs are written to a temp file the argument names.
- [x] `Unsupported` convention: `RunInChild` returns a result with `supported == false` on a platform with no process spawning, and a helper macro `ENGINE_REQUIRE_DEATH_TEST_SUPPORT()` makes the test skip with a printed reason; test that the skip path works by forcing the unsupported result in a unit test.
- [x] Add the `tests/debug/` directory with its CMake target `debug_tests` in the three flavors (plain, ASan, TSan, linked against `foundation`, `foundation_asan`, `foundation_tsan`) using `engine_discover_tests`, a `lsan_options.cpp` if the leak checker needs it, and Catch2 as in `tests/jobs`.
- [x] Wire the scoping: add `tests/debug/` lines to `tests/phases.txt` (phase 6) and a `flavors 6 plain,asan,tsan` line; add `tests/debug 6` and `engine/foundation/debug`/`engine/foundation/os/linux/debug` to the module map `tests/module_map.txt` (with the phases of the tests that cover them); run `test_phase_labels` and the `test_phase_script_*` tests to prove the map still validates.
- [x] Neutral results: `ChildResult` carries `reason` (a `CrashReason`: none, access violation, bus error, illegal instruction, arithmetic fault, stack overflow, abort, assert, ensure) decoded by the platform backend from the child's exit status (Linux: the terminating signal; the Windows backend will decode NTSTATUS exit codes such as `0xC0000005`), with the raw status kept beside it. Add `debug/fault_injection.h`: `InjectFault(FaultKind)` (null write, abort, illegal instruction, divide by zero, stack overflow, misaligned access where the platform has one), implemented per platform, so every crash test says `InjectFault(...)` and checks `reason`, never a signal number.
- [x] Platform selection: `engine/foundation/CMakeLists.txt` selects `FOUNDATION_DEBUG_PLATFORM_SOURCES` from `CMAKE_SYSTEM_NAME` next to the existing os selection (Linux only now; the Windows and macOS branches stop with a message naming Phase 10 or 11).
- [x] The neutral-header gate: a CMake-script ctest in the style of `tools/raw_new_check` (`tools/platform_neutral_check`, fixtures that plant each violation) that fails if a header under `engine/foundation/debug/` or a test source under `tests/debug/` (other than files named `*_linux_*`) names a platform type or constant (`ucontext_t`, `siginfo_t`, `SIG[A-Z]+`, `pid_t`, `HANDLE`, `/proc`, `fork(`, `<unistd.h>`, `<windows.h>`); it runs from this day on, so no later day can slip a platform detail into a neutral file. It also owns the library boundary: a file may include `backtrace.h` or the crash library's header only if it is the one implementation file named for that library (Days 94 and 95 add those names to the gate's table; until then no file may include either). Phase label 6.
- [x] Tests: a child that returns normally (exit code 0), calls `_exit(7)`, raises `SIGSEGV` by writing to null, calls `abort()`, writes 1 MB to stderr (the pipe fills; no deadlock), hangs (timed out and killed within the deadline plus a margin), closes stderr and then crashes, starts a grandchild that keeps the pipe open (the helper still returns at the child's exit), and receives its argument string and environment entries; the test binary being started from another working directory works (the path comes from `/proc/self/exe`, not `argv[0]`). The platform-neutral parts are also tested without spawning (registration, result decoding). Every test also asserts there is no zombie left (`waitpid(-1, WNOHANG)` finds nothing) and no leaked descriptor (`/proc/self/fd` count before and after).
- [x] Run the helper's tests in all three flavors, at `-j16` and 20 times in a loop (`ctest --repeat until-fail:20`) to catch flakiness; record the numbers. Anything flaky goes to `KNOWN-ISSUES.md` with its phase.
- [x] **Documentation:** `///` comments with the contract labels of the new module (add `module foundation/debug "Foundation: Debugging"` to `docs/reference/modules.txt` with `strict thread-safety`, `strict ownership` and `strict failure`); `docs/guide/foundation-debugging.md` created with a section "Testing code that crashes" (including the per-platform support table: re-exec on Linux/macOS/Windows, `Unsupported` on iOS and consoles) and a tagged, compiled sample; `docgen lint` at zero known findings; `cmake --build build --target docs` output committed; `ctest -L docs` passes.
- [x] Run the day's scoped tests: `scripts/test-phase.sh --affected` (the day touches an `engine/foundation` header, so it runs everything), then `--flavor asan` and `--flavor tsan` of the same selection; quote the `ran X of Y tests` lines.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix or decline each finding with a reason, record it under "Post-implementation code review" here, and mention it in `STATUS.md`. Extra scrutiny on the spawn/pipe lifetime and descriptor handling.
- [x] Update `STATUS.md`; commit.

**Done:** the neutral-header gate passes with its planted-violation tests; `RunInChild` (re-exec model, `Unsupported` where a platform cannot spawn) reports exit code, signal, timeout and captured output for the cases above without leaking a process or descriptor, in all three flavors, repeated 20 times without a failure; `tests/debug` is wired into the phase map and the scoped runner; the guide page and reference exist and `ctest -L docs` passes.

## Result

Code: `engine/foundation/debug/` (`crash_reason.h/.cpp`, `fault_injection.h`, `death_test.h/.cpp`, `death_test_platform.h`), the Linux
backend `engine/foundation/os/linux/debug/` (`death_test.cpp`, `fault_injection.cpp`), `cmake/EngineDeathTests.cmake`
(`engine_enable_death_tests(<target>)`), `tests/debug/` (plain, ASan, TSan), `tools/platform_neutral_check/`. Docs: module
`foundation/debug` (62/62 documented, baseline updated), `docs/guide/foundation-debugging.md`, `docs/reference/foundation-debug.md`.

Deviations from the plan above:

- `ENGINE_REQUIRE_DEATH_TEST_SUPPORT()` lives in `tests/debug/support/death_test_skip.h`, not in the engine header: it uses
  Catch2's `SKIP`, which an engine header cannot depend on. `ENGINE_DEATH_TEST_FN(name)` takes only the function name.
- Added `RunDeathTestChildIfRequested()` (public) so a program that is not built on Catch2 can be a child; the Catch2 test case
  `[.death-child]` (tests/debug/support/death_test_child.cpp) just calls it. The hidden test is not registered as a ctest.
- `ChildResult` has `terminated` + `native_code` + `reason` instead of `signaled`/`signal` (the neutral names); a stack
  overflow reads as `AccessViolation` from an exit status, documented. `CrashReason` has its own header, `debug/crash_reason.h`;
  Day 95's `crash_handler.h` includes it instead of declaring it.
- `FaultKind::BusError` replaces "misaligned access" (x86-64 Linux does not trap on it); implemented by touching a page of an
  empty `memfd` mapping.
- The gate exempts files whose name has `_linux` followed by `_` or `.` (so `resource_guard_linux.cpp` counts); it scans
  `engine/foundation/debug/` and `tests/debug/`. The library-boundary table `tools/platform_neutral_check/library_boundary.txt`
  is empty until Days 94 and 95.
- The child's sanitizer fault handling is turned off through `ASAN_OPTIONS`/`TSAN_OPTIONS`/`UBSAN_OPTIONS`/`LSAN_OPTIONS`
  (`sanitizers_pass_faults`, on by default), otherwise ASan and TSan turn a SIGSEGV into an exit code.
- `module_map.txt`: `engine/foundation` now selects phase 6 too, so `test_phase_script_affected_foundation_source` expects
  `0 1 2 2.5 3 4 6`.

Verification (final): `scripts/test-phase.sh --affected` plain `ran 894 of 1939 tests (phases: all; flavors: plain; display: included)`,
asan `ran 655 of 1939 tests (... flavors: asan ...)`, tsan `ran 390 of 1939 tests (... flavors: tsan ...)`, all pass (the
selection is everything because the day changes CMake and a foundation header). Phase 6 has 67 tests: 21 plain, 21 ASan, 21 TSan and the 4 `platform_neutral_check` cases. `ctest -L phase6 -j16 --repeat until-fail:20`, eight rounds:
no failure (160 runs of 67 tests). Also built and passing with `-DENGINE_UNITY_BUILD=OFF` and with clang 23.1.4 (67 of 67).
`ctest -L docs` 4/4; `docgen lint` at zero known findings.

A bug the repeat loop found: the helper's deadline was computed from a start time truncated to milliseconds, so the timeout
could fire 1 ms early (a 300 ms timeout killed at 299 ms; the test caught it in 3 of the first 25 repeats). Fixed by tracking
the deadline in nanoseconds.

## Post-implementation code review

`/code-review high` on the day's diff, 10 findings:

1. `waitpid` failing with `ECHILD` (SIGCHLD ignored, or another thread reaped the child) reported a clean exit. **Fixed**: the
   result keeps `started` but leaves `exited` and `terminated` false when the status was not observed (also on the timeout
   path). Test: `A child whose status is reaped elsewhere is reported as unobserved` (fails without the fix).
2. A sanitizer option passed in `extra_env` was added twice and the child saw only the first. **Fixed**: merged into one entry
   with the fault flags. Test: `A sanitizer option the caller passes is merged...` (fails without the fix).
3. `__builtin_trap()` raises SIGTRAP on AArch64. **Fixed**: `ud2` on x86, `.inst 0x00000000` (udf) on AArch64. The AArch64 branch
   is not compiled or run here (KNOWN-ISSUES).
4. `DrainPipe` had no bound, so a fast writer could keep the poll loop from checking the deadline. **Fixed** with a 256 KB budget
   per call. The guard test (`A child that writes without pause is still killed at the timeout`, three writer threads) passes
   with and without the bound: this machine's reader keeps up with the writers, so the starvation could not be reproduced. Kept as
   a guard, not a proof; recorded in KNOWN-ISSUES.
5. A child in its own process group outlived a parent killed by Ctrl-C or a ctest timeout. **Fixed**: the child arms
   `PR_SET_PDEATHSIG` (SIGKILL) and exits if its parent already died. Test: `A child's own child dies with it when its parent
   is killed` (fails without the fix, found and the grandchild killed by the test).
6. `readlink("/proc/self/exe")` gives `... (deleted)` after a rebuild during a run. **Fixed**: the literal `/proc/self/exe` is
   executed, which works for a deleted binary; the readlink buffer is gone.
7. Polling `waitpid` every 10 ms instead of a pidfd. **Declined**: it adds at most 10 ms to a call, the whole suite runs in
   under 7 s, and `pidfd_open` needs Linux 5.3 plus a fallback path.
8. `RunInChild` documented as thread-safe while reading an unsynchronized flag and the environment. **Fixed**: the flag is
   atomic and the contract states that the environment must not be changed concurrently.
9. Phase 6 rules sat under the Phase 5 heading and the label range said `phase5`. **Fixed**.
10. Day file, `STATUS.md` and the verification record were missing. **Fixed** (this section).

Also declined: nothing else. Noted for the reader: `docgen lint --update-baseline` raised the documented counts of other modules
that had grown since the baseline was last written; no known finding was added.
