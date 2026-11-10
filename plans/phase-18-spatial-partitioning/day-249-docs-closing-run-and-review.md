# Phase 18 / Day 249 — Docs, Windows PC, Closing Full Run & Phase Review

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 237-248.

Close the phase: complete guide, the Windows run, every flavor, and the uncapped review.

## Tasks

- [ ] **Guide:** `docs/guide/spatial.md` complete and readable by a junior developer (overview, which structure when, queries, the ECS tier, the physics and renderer contracts, rules, common mistakes, see also), every tagged sample compiling and running; `docs/guide/README.md` lists it; `docs/guide/ecs.md` and `renderer.md` point to it; a tutorial `docs/tutorials/spatial-queries.md` (add entities, ask for neighbors and a ray, see the result) with a sample that runs.
- [ ] **Windows PC:** build both compilers and both unity modes; run `tests/spatial`, `tests/scene` and the demo smoke test there; the two-machines entries in `KNOWN-ISSUES.md` for Days 237-248 are closed or filed with the facts.
- [ ] **Closing full run:** `scripts/test-phase.sh --all --flavor all` (on Linux; plain and asan on Windows), plus `ctest -L docs` and the phase's demo; the final `ran X of Y tests (...)` lines recorded here; `CURRENT_PHASE` stays 18 until Phase 19's first day.
- [ ] **End-of-phase review (uncapped, CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (the commit before Day 237 to the working tree); verify, fix or decline each finding with a reason; record the count and the outcome here and in `STATUS.md`.
- [ ] **Plan follow-through:** `plans/phase-22-physics.md` is re-read against what was built and amended where the broad phase is now Phase 18's; the open items (tile light lists, GPU culling, streaming cells) are filed in `KNOWN-ISSUES.md` or in the Phase 33 file.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (the whole suite, every flavor); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide, the tutorial and the module list.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Phase 18 is complete: the spatial module is documented, measured, running on both machines, and reviewed.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
