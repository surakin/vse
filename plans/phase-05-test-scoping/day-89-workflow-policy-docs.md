# Phase 5 / Day 89 — Workflow, Policy & Docs

**Phase:** [Phase 5 — Test Scoping](../phase-05-test-scoping.md)
**Depends on:** Day 88.

Make the new workflow the written rule, so it is followed without being remembered.

## Tasks

- [x] Update `CLAUDE.md`: the end-of-day rule says a day runs `scripts/test-phase.sh --affected` (plain) plus the sanitizer flavors of that selection before the review; a phase's closing day runs `--all` in every flavor and records the result in its day file; a change to build files or shared headers runs everything. State that a reduced run is reported with its `X of Y` line in the day file, never as "all tests pass".
- [x] Update the phase-closing wording in the planning rules: the last day of every phase ends with the full run, and and add the closing full-run task as a bullet in the Phase 6 and Phase 7 plan files (earlier phases are history and are not rewritten).
- [x] Update `STATUS.md`'s build and test commands: the scoped command, the full command, how to change `CURRENT_PHASE`, where the label map lives.
- [x] Write the "Test phases and scoped runs" section of `docs/guide/building-and-testing.md`: the labels, the map, the runner with example output, what a scoped run can miss and how the touched-module check and the closing run cover it, how to add a test (one line in `tests/phases.txt`), and how to run a single flavor.
- [x] Update `docs/DOC-CONVENTIONS.md` if its "when a day is documented" rule mentions running `ctest -L docs`: it stays a full, fast label run in every day (docs tests are not scoped).
- [x] Update `PLAN.md` guiding principles with the rule and its rationale (suite size, iteration speed, the closing full run), and the Phase 5 roadmap row if wording changed.
- [x] Decide and apply how `KNOWN-ISSUES.md` entries mention flaky tests under scoped runs (a flaky test is recorded with the phase it belongs to).
- [x] Walk through the workflow as a new contributor: from `CURRENT_PHASE` to a scoped run to a closing run, with only the written instructions; fix any step that was unclear.
- [x] **Documentation:** the guide section above is the deliverable. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] Update `STATUS.md`; commit (documentation and rules only; no code review is required).

**Done:** `CLAUDE.md`, `STATUS.md`, `PLAN.md` and the building-and-testing guide describe the scoped workflow and the closing full run consistently, and a contributor can follow them without asking.

## Result

- `CLAUDE.md`: new section "Tests run by phase; the full suite closes a phase" (scoped run before the review, the `X of Y` line
  quoted in day files, the closing `--all --flavor all` run, the map lines for new tests, flaky tests named with their phase), and the
  end-of-day review's step 1 runs the scoped tests first.
- `STATUS.md`: a "Build and test commands" section (scoped, affected, one phase, full, how `CURRENT_PHASE` changes, where the map lives).
- `PLAN.md`: a guiding principle (test by phase; close a phase with the full suite). The Phase 5 roadmap row already said it.
- `docs/guide/building-and-testing.md`: "Test phases and scoped runs": labels, map, the runner with example output, what a scoped run
  can miss and how the module map and the closing run cover it, how to add a test, how to run one flavor.
- Phase 6 and 7 plans gained a "Closing run" deliverable (full suite in every flavor, `ctest -L docs`, the demos, the `X of Y` line in the last day file).
- `KNOWN-ISSUES.md` intro: a flaky or failing test names its phase.
- `docs/DOC-CONVENTIONS.md` does not mention how docs tests are run, so it needed no change; the `docs` label is not scoped (CLAUDE.md says so).
- Walk-through as a new contributor, using only the written text: `STATUS.md` commands -> `scripts/test-phase.sh` (35 of 1874 tests), `--affected`
  (escalated to everything because of the uncommitted CMake edits, as documented), `3 --flavor asan --dry-run` (211 of 1874), `ctest -R test_phase`
  (35/35) and `ctest -L docs` (4/4) all behaved as written. One step was unclear and is now spelled out: "plain" means neither
  sanitizer label. `docgen lint` is unchanged (no header changed); `cmake --build build --target docs` left `docs/reference/` unchanged.
