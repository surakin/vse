# Phase 28 / Day 492 — Height-Field Collision and the Character Controller

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 491.

Make bodies rest on, roll over and walk on the terrain.

## Tasks

- [ ] Narrow phase: sphere, capsule and box against the height field in `physics/collide_*.cpp`: gather the cells under the shape's bound (pyramid-culled), generate contacts against their triangles with the existing triangle-against-convex routines used for `ShapeKind::Mesh` (reuse, do not copy), with the fixed cell order so the contact order is deterministic; hull and compound members the same way.
- [ ] Contact quality: a box lying flat gets 4 contacts, a sphere 1, a capsule 1 or 2; edge and vertex contacts across cell diagonals are merged so a sphere rolling over a diagonal does not jolt (a test rolls a sphere over a tilted plane made of cells and checks the speed stays within a tolerance of the analytic one).
- [ ] Continuous collision: the existing CCD sweep treats the height field as a static shape (a fast sphere dropped from high above does not tunnel through a thin ridge; a test).
- [ ] The character controller (`character.cpp`) walks on a height field: ground probe, slope limit, step handling; a capsule walks up a ramp, stops at the slope limit, and walks across a cell diagonal without a hop.
- [ ] Determinism: the reference scenes (`tests/physics/determinism_test.cpp`) gain a terrain scene (a hundred bodies dropped onto a bumpy field); the state hash is recorded and equal at 1, 2 and 8 threads and on repeat; the expectation of equality across Linux, Windows and macOS is checked on Days 502 and 503.
- [ ] Tests: a stack of boxes rests on a slope; a sphere comes to rest in a bowl; no body falls through a grid of 64x64 random bumps in 10 s (the invariant: the lowest body is above the surface minus a tolerance); the debug drawer shows the field's cells near the bodies.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** Bodies and characters collide with a height field deterministically, and the terrain scene's hash is committed.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
