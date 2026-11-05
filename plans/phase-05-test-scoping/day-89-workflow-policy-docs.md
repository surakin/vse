# Phase 5 / Day 89 — Workflow, Policy & Docs

**Phase:** [Phase 5 — Test Scoping](../phase-05-test-scoping.md)
**Depends on:** Day 88.

Make the new workflow the written rule, so it is followed without being remembered.

## Tasks

- [ ] Update `CLAUDE.md`: the end-of-day rule says a day runs `scripts/test-phase.sh --affected` (plain) plus the sanitizer flavors of that selection before the review; a phase's closing day runs `--all` in every flavor and records the result in its day file; a change to build files or shared headers runs everything. State that a reduced run is reported with its `X of Y` line in the day file, never as "all tests pass".
- [ ] Update the phase-closing wording in the planning rules: the last day of every phase ends with the full run, and and add the closing full-run task as a bullet in the Phase 6 and Phase 7 plan files (earlier phases are history and are not rewritten).
- [ ] Update `STATUS.md`'s build and test commands: the scoped command, the full command, how to change `CURRENT_PHASE`, where the label map lives.
- [ ] Write the "Test phases and scoped runs" section of `docs/guide/building-and-testing.md`: the labels, the map, the runner with example output, what a scoped run can miss and how the touched-module check and the closing run cover it, how to add a test (one line in `tests/phases.txt`), and how to run a single flavor.
- [ ] Update `docs/DOC-CONVENTIONS.md` if its "when a day is documented" rule mentions running `ctest -L docs`: it stays a full, fast label run in every day (docs tests are not scoped).
- [ ] Update `PLAN.md` guiding principles with the rule and its rationale (suite size, iteration speed, the closing full run), and the Phase 5 roadmap row if wording changed.
- [ ] Decide and apply how `KNOWN-ISSUES.md` entries mention flaky tests under scoped runs (a flaky test is recorded with the phase it belongs to).
- [ ] Walk through the workflow as a new contributor: from `CURRENT_PHASE` to a scoped run to a closing run, with only the written instructions; fix any step that was unclear.
- [ ] **Documentation:** the guide section above is the deliverable. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] Update `STATUS.md`; commit (documentation and rules only; no code review is required).

**Done:** `CLAUDE.md`, `STATUS.md`, `PLAN.md` and the building-and-testing guide describe the scoped workflow and the closing full run consistently, and a contributor can follow them without asking.
