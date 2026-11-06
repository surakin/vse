# Phase 5 — Test Scoping

**Status: complete (2026-11-06).** Days 85-90 are done; the closing full run (1874/1874 in plain, ASan and TSan) is in [Day 90](phase-05-test-scoping/day-90-measure-wrapup.md). Phase 6 (Debugging facilities) needs its own day files before it starts.

**Depends on:** Phase 4 (Renderer core) complete. Inserted on 2026-11-05, ahead of the Windows port, because the
suite has grown to about 1800 tests (65 s on 16 cores in three sanitizer flavors) and every day was re-running all of
them.
**Goal:** day-to-day development re-runs only the tests of the phase being worked on, and the whole suite runs when a
phase completes. Nothing is lost: a change that reaches into an earlier phase's code also runs that code's tests.
**Definition of done:** every test belongs to exactly one phase; `scripts/test-phase.sh` (or the equivalent ctest
presets) runs the current phase's tests in the plain flavor by default, with `--all` for the full suite in all
flavors; the phase of the day's work is recorded in one place and read by the script; a "touched module" check adds
the tests of any earlier-phase module a diff modifies; `CLAUDE.md`, `STATUS.md` and the docs say when each run is
required; every phase-complete day in the plan ends with the full run.

Every day of this phase has its own file under `plans/phase-05-test-scoping/` (see `CLAUDE.md`, planning policy).

## Days

Each day has its own file of individually checkable sub-tasks, a docs task, an end-of-day `/code-review high` task where code changes, and a concrete Done condition. Day numbers continue from Phase 4 (which ended at Day 84).

85. [Test Inventory & Scoping Design](phase-05-test-scoping/day-85-test-inventory-design.md)
86. [Phase Labels for Every Test](phase-05-test-scoping/day-86-phase-labels.md)
87. [Phase Labels for the RHI and Renderer Tests](phase-05-test-scoping/day-87-rhi-renderer-labels.md)
88. [The Runner & Touched-Module Selection](phase-05-test-scoping/day-88-runner-and-affected.md)
89. [Workflow, Policy & Docs](phase-05-test-scoping/day-89-workflow-policy-docs.md)
90. [Measurement, Full Run & Phase Wrap-up](phase-05-test-scoping/day-90-measure-wrapup.md)

## Key deliverables

- **A phase for every test.** ctest labels `phase0` ... `phaseN` (and `phase2.5`) assigned by CMake from one table
  that maps a test source file or directory to its phase. A test with no phase fails a `test_phase_labels` ctest, so a
  new test cannot be forgotten. Tests that span phases (the renderer's real-device tests live in `tests/rhi`) are
  assigned to the phase that owns the behavior, not the directory.
- **A module-to-phase map and a touched-module check.** `scripts/test-affected.sh` reads `git diff` (uncommitted and
  the day's commits), finds which modules changed (`engine/foundation`, `engine/jobs`, `engine/rhi`, `engine/renderer`,
  `shaders`, `tools/...`), and adds those modules' tests to the current phase's. A change to `foundation` from Phase 6
  therefore re-runs the foundation tests, not just Phase 6's.
- **The runner.** `scripts/test-phase.sh [N | --all | --affected] [--flavor plain|asan|tsan|all]`, with the same
  parallelism and timeout defaults as today; it prints how many tests it selected out of the total, so a reduced run
  cannot be mistaken for a full one.
- **Where the current phase lives.** One line in `STATUS.md` (or a `PHASE` file the script reads), changed in the same
  commit that starts a phase; the script refuses to run a phase that is not the recorded one without `N`.
- **Full runs at the right times.** A phase's closing day, a change to build files or shared headers, and a release
  candidate always run everything in every flavor (`--all`); the day files of every phase end with that run. A weekly or
  on-demand `--all` is listed in `STATUS.md` build/test commands.
- **Measure it.** Record the full-suite time and each phase's scoped time before and after, so the saving is a number.
- **Docs.** `docs/guide/building-and-testing.md` gains the workflow; `CLAUDE.md`'s end-of-day rules say which run a day
  needs (scoped vs full).

## Notes / open questions

- **Risk: a scoped run misses a regression in an earlier phase.** The touched-module check covers direct edits; it
  does not cover behavior changes that arrive through a dependency that was not edited (a header-only change is
  covered, a changed build flag is not, so build-file changes force `--all`). Decide how much extra coverage a day gets
  (for example, always include the previous phase's golden-image tests).
- Do sanitizer flavors run scoped too? Proposed: yes, scoped by the same labels; the phase-complete run covers all
  three.
- Window/display tests share the user's input focus and are flaky when run in parallel (see `KNOWN-ISSUES.md`); the
  labels should let a phase skip them unless the phase touches platform code.
- How are the docs samples (`ctest -L docs`) and the shader checks scoped? Probably always on, they are fast.
