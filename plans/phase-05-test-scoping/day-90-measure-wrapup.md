# Phase 5 / Day 90 — Measurement, Full Run & Phase Wrap-up

**Phase:** [Phase 5 — Test Scoping](../phase-05-test-scoping.md)
**Depends on:** Day 89.

Prove the saving, prove nothing was lost, and close the phase with the first full run under the new rules.

## Tasks

- [ ] Measure and record in this file: the full suite (all flavors, `-j16` and `-j1`) against the scoped runs for each phase (plain, and plain+ASan+TSan), with test counts; the saving as a ratio. Do the same for a typical day of Phase 6 work (a change in `engine/foundation/os` plus its tests, using `--affected`).
- [ ] Plant a regression to check the safety net: break something in an earlier phase's module (for example a foundation allocator off-by-one) from a Phase 6-style diff, confirm `--affected` catches it through the touched-module rule, and confirm the closing `--all` run would catch a break the scoped run could not (a break in a dependency nobody edited). Record both; revert the plants.
- [ ] Check that the scoped runs of Phase 6 and 7 will include what they need: list, for the Phase 6 and Phase 7 plans, the modules they will touch and the phases the runner selects for them; add any missing module to `tests/module_map.txt`.
- [ ] Final full run, as the closing run of the phase: `scripts/test-phase.sh --all --flavor all` (plain, ASan, TSan), docs, GPU tests with validation, `renderer_demo` and `rhi_demo` for 1000 frames with validation on Wayland and X11 (a locked session breaks this; record it in `KNOWN-ISSUES.md` if unavailable); results in this file.
- [ ] Mark Phase 5 complete in `plans/phase-05-test-scoping.md`, `PLAN.md` and `STATUS.md`; set `CURRENT_PHASE` to `6`; Phase 6 (Debugging facilities) needs its own day files before it starts.
- [ ] **Documentation:** the measured numbers go into the guide's workflow section. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the phase's diff (Days 86-90), fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** the saving is a measured number, both planted regressions behaved as predicted, the closing full run passes in every flavor, `CURRENT_PHASE` is 6, and Phase 5 is marked complete.
