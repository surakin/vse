# Phase 12 / Day 142 — Assert Reports and Thread Registration as Hooks

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Day 141; the hook APIs in the spec (Day 140, Spike A).

Remove the two upward edges into the debugging module (`ARCHITECTURE.md` section 2, items 1 and 3) without changing what an assert or a thread does once the
program is set up.

## Tasks

- [x] **Assert report hook:** `log` keeps the assert macros and a complete minimal failure path (the message, file, line and kind to standard error through
      `os::Write`, then the handler chain, then abort); the full report (stack trace, crash context, crash report, debugger break) moves to
      `foundation_debug` and is installed into the hook by `InitDebugFacilities`/program setup. `log/log.cpp` no longer includes `debug/`; `log/assert.h` keeps only
      `debug/compiler.h` (base layer). The hook's installation is safe against an assert on another thread during installation (the spec's rule).
- [x] **Thread-start hook:** a base-layer hook that thread-starting code calls with the thread's name; `io/file.cpp` (the I/O thread) and `jobs` (workers) call it
      instead of `debug::RegisterThreadForCrashHandling`; `foundation_debug` installs `RegisterThreadForCrashHandling` into it. `io/file.cpp` no longer includes
      `debug/`. Threads started before installation are registered when the hook is installed if the spec requires it (record the rule).
- [x] **Tests:** an assert before the report hook is installed prints the message and aborts (death test, new); after installation every existing assert and
      crash-report test is unchanged (`tests/debug`, `tests/foundation`); the I/O thread and the job workers are registered for crash handling after program
      setup (the existing thread tests, plus one that starts the I/O thread before setup if the rule above applies).
- [x] **Allow list:** entries for items 1 and 3 deleted; `module_deps_check` passes.
- [x] **Documentation:** `log/assert.h`, the hook header and `debug/program_setup.h` document the two states (before and after installation) with `Aborts:` labels
      where an assert is involved; `docs/guide/foundation-logging-and-assert.md` and `foundation-debugging.md` explain who installs what; reference regenerated;
      `docgen lint` zero; `ctest -L docs`.
- [x] **Runs (Linux):** `scripts/test-phase.sh --affected` plus `--flavor asan` and `--flavor tsan` (assert paths, thread start and crash handling are lifetime and
      concurrency code), and `--crashes`; quote the lines.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the minimal failure path allocating or locking (it runs when the heap may be broken), an assert
      racing hook installation, a crash on a thread that started before the hook.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** `log` and `io` no longer include the debugging module, asserts and thread registration behave exactly as before once the program is set up, the
pre-installation behavior is tested, and two allow-list entries are gone.

## Result (2026-11-07)

- New `log/diagnostics_hooks.h` (`FailureReportHooks`: stack trace, crash context, debugger check; `SetThreadStartHook`/`NotifyThreadStart`), implemented in
  `log/log.cpp`; `debug/failure_hooks.{h,cpp}` provides `debug::InstallDiagnosticsHooks()`, called by `InitDebugFacilities`. `log.cpp`, `io/file.cpp` and the
  two `jobs` threads no longer include `debug/`. Frame counts: the adapter is `ENGINE_NOINLINE` and passes `skip + 1`; the existing trace tests prove it.
- Rule recorded: a thread started before installation is never registered later (registration needs the crash handler anyway).
- Behavior change: a program that never calls `InitDebugFacilities` / `StartProgramDebugging` now gets the bare message from an assert. The demos and
  benches already call `StartProgramDebugging`; `docgen` and `shader_mint` now call `InstallDiagnosticsHooks()` first thing, and the `tests/debug` executables
  install through `tests/debug/install_hooks.cpp`.
- Tests: `tests/foundation/diagnostics_hooks_test.cpp` (no hooks: message + abort / ensure continues; fake hooks: trace, context, debugger, skip counts; thread
  hook), `tests/debug/failure_hooks_test.cpp` (real install). Allow-list entries `log -> debug` and `io -> debug` deleted; gate passes.
- Runs: plain `--affected --crashes` = `ran 1528 of 3489 tests (phases: all; flavors: plain; display: included; crash tests: included)`, 2 failures, both the
  known Wayland/NVIDIA demo crash; `--flavor asan`: `ran 965 of 3489 tests (... flavors: asan ...)` and `--flavor tsan`: `ran 682 of 3489 tests (... flavors: tsan ...)`,
  no failures (run before the last two small edits; the crash-label sweep `ctest -L crash` was rerun after them: 481 of 482 pass, the one failure the demo).
  Docs: `cmake --build build --target docs`, `ctest -L docs` pass.

## Post-implementation code review

Inline high-effort review of the diff. Found and fixed: tools that never start the debugging facilities (`docgen`, `shader_mint`) silently lost assert traces
(they now install the hooks). Declined: a program with no `StartProgramDebugging` gets a bare report by design (documented in both guides); the frame skip depends on
the adapter not being tail-called, which `ENGINE_NOINLINE` and the trace-position tests cover.
