# Phase 25 / Day 401 — Docs, Closing Runs and the Phase Review

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** All days of Phase 25.

Close the phase on every machine.

## Tasks

- [ ] The guide `docs/guide/physics.md` is complete: concepts, components, the step and its order, shapes and assets, solver tuning, joints, sleeping, CCD, queries, the character controller, events, snapshots, determinism rules and the three-machine result, the C API section, limits; every tagged code sample compiles and runs.
- [ ] The tutorial `docs/tutorials/physics-in-your-window.md` is current; `docs/reference/` is regenerated (`cmake --build build --target docs`) and committed; `docgen lint` at 100 %.
- [ ] **Closing runs (CLAUDE.md):** `scripts/test-phase.sh --all --flavor all` on Linux, quoting the final `ran X of Y` line; the same on the Windows PC (plain and asan) and the Mac (plain), and `ctest -L docs`; the demos the phase ships run; the closing runs of the PC and Mac may trail by the rule 'no later than the phase's close' and are listed in `KNOWN-ISSUES.md` if they do.
- [ ] Builds clean (warnings are errors) with GCC and Clang, unity on and off, `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF`, on Linux; the closing pass runs the tests in the shared configuration too (the Phase 24 closing left that open).
- [ ] **Uncapped review (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 373 to the working tree), split by area if the diff is large; verify, fix (regression test that fails without the fix, shown to fail) or decline each finding with a reason; record the count and outcome under 'Post-implementation code review'.
- [ ] `STATUS.md`, `PLAN.md` (roadmap row 25 complete, progress log), `KNOWN-ISSUES.md` (every deferred item), `CURRENT_PHASE` stays 25 until the next phase's first day; memory notes updated.
- [ ] Commit and push.

**Done:** Phase 25 is complete on Linux with the full run quoted, the review recorded, and the PC and Mac runs done or listed.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
