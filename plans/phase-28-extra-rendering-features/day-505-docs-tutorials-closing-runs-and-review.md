# Phase 28 / Day 505 — Docs, Tutorials, Closing Runs and the Phase Review

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 478-504.

Close the phase.

## Tasks

- [ ] Tutorials and the mental-model introduction (CLAUDE.md: they wait for the phase's last day): "Lighting a World with a Sky" (procedural to cube map to reflections), "Making a Landscape" (a height map to a walkable, textured terrain), "A Forest in One Draw" (instances and hierarchies, the cost model: chunks, runs, subtrees), compiling samples in `docs/samples`; glossary terms (cube map, height map, splat map, tile, level of detail, geomorph, chunk, instance run, subtree); `find-your-way.md`; the guides `docs/guide/renderer.md`, `terrain.md`, `instancing.md`, `physics.md`, `engine-library.md`, `scripting.md` complete and current; `docs/reference` regenerated; **no phase or day numbers in any published page** (`docs_phase_check`).
- [ ] `PLAN.md` (roadmap row to complete, progress log, new assumptions for the sky and cube texture, terrain, instancing and hierarchy), `CLAUDE.md` (bullets: "Terrain and the height-field collider are one data set", "Instances are chunked sets and packed trees, never one entity each"), `ARCHITECTURE.md`, `BUILD.md`, `STATUS.md`, `KNOWN-ISSUES.md`, `tests/module_map.txt` and `tools/module_deps_check/modules.txt` for any new target, the memory notes: all current.
- [ ] **The phase's last day runs the whole suite in every flavor:** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs` and the demos (`render_features_demo` in a window with validation on Wayland and X11, 1,000 frames) on Linux; the Windows and macOS runs are Days 502 and 503's; record every `ran X of Y` line here.
- [ ] **The end-of-phase review is uncapped (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 478 to the working tree), with extra scrutiny on the instance set's id and chunk bookkeeping, the hierarchy's array edits (`subtree_size` invariants), the terrain's untrusted blob parsing, the height-field collider's determinism and the lifetime of every new GPU owner across a reset; verify, fix with a regression test shown to fail without the fix, or decline with a reason; record the count and the outcome under "Post-implementation code review".
- [ ] `CURRENT_PHASE` stays 28 until the next phase's first day changes it (Phase 29, Android and iOS, once its day files exist).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** Phase 28 is documented, the whole suite passes in every flavor on Linux, the Windows and macOS days are recorded, and the uncapped review's findings are resolved.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
