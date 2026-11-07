# Phase 12 / Day 142 — Assert Reports and Thread Registration as Hooks

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Day 141; the hook APIs in the spec (Day 140, Spike A).

Remove the two upward edges into the debugging module (`ARCHITECTURE.md` section 2, items 1 and 3) without changing what an assert or a thread does once the
program is set up.

## Tasks

- [ ] **Assert report hook:** `log` keeps the assert macros and a complete minimal failure path (the message, file, line and kind to standard error through
      `os::Write`, then the handler chain, then abort); the full report (stack trace, crash context, crash report, debugger break) moves to
      `foundation_debug` and is installed into the hook by `InitDebugFacilities`/program setup. `log/log.cpp` no longer includes `debug/`; `log/assert.h` keeps only
      `debug/compiler.h` (base layer). The hook's installation is safe against an assert on another thread during installation (the spec's rule).
- [ ] **Thread-start hook:** a base-layer hook that thread-starting code calls with the thread's name; `io/file.cpp` (the I/O thread) and `jobs` (workers) call it
      instead of `debug::RegisterThreadForCrashHandling`; `foundation_debug` installs `RegisterThreadForCrashHandling` into it. `io/file.cpp` no longer includes
      `debug/`. Threads started before installation are registered when the hook is installed if the spec requires it (record the rule).
- [ ] **Tests:** an assert before the report hook is installed prints the message and aborts (death test, new); after installation every existing assert and
      crash-report test is unchanged (`tests/debug`, `tests/foundation`); the I/O thread and the job workers are registered for crash handling after program
      setup (the existing thread tests, plus one that starts the I/O thread before setup if the rule above applies).
- [ ] **Allow list:** entries for items 1 and 3 deleted; `module_deps_check` passes.
- [ ] **Documentation:** `log/assert.h`, the hook header and `debug/program_setup.h` document the two states (before and after installation) with `Aborts:` labels
      where an assert is involved; `docs/guide/foundation-logging-and-assert.md` and `foundation-debugging.md` explain who installs what; reference regenerated;
      `docgen lint` zero; `ctest -L docs`.
- [ ] **Runs (Linux):** `scripts/test-phase.sh --affected` plus `--flavor asan` and `--flavor tsan` (assert paths, thread start and crash handling are lifetime and
      concurrency code), and `--crashes`; quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the minimal failure path allocating or locking (it runs when the heap may be broken), an assert
      racing hook installation, a crash on a thread that started before the hook.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** `log` and `io` no longer include the debugging module, asserts and thread registration behave exactly as before once the program is set up, the
pre-installation behavior is tested, and two allow-list entries are gone.
