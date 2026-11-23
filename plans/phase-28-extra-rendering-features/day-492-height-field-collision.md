# Phase 28 / Day 492 — Height-Field Collision and the Character Controller

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 491.

Make bodies rest on, roll over and walk on the terrain.

## Tasks

- [x] Narrow phase: sphere, capsule and box against the height field in `physics/collide_*.cpp`: gather the cells under the shape's bound (pyramid-culled), generate contacts against their triangles with the existing triangle-against-convex routines used for `ShapeKind::Mesh` (reuse, do not copy), with the fixed cell order so the contact order is deterministic; hull and compound members the same way. (Done on Day 491: the mesh code became a template over a triangle source.)
- [x] Contact quality: a box lying flat gets 4 contacts, a sphere 1, a capsule 1 or 2; edge and vertex contacts across cell diagonals are merged so a sphere rolling over a diagonal does not jolt (a test rolls a sphere over a tilted plane made of cells and checks it against the same plane made of one box, within 5 %).
- [x] Continuous collision: the existing CCD sweep treats the height field as a static shape (a fast sphere dropped from high above does not tunnel through a thin ridge; a test).
- [x] The character controller (`character.cpp`) walks on a height field: ground probe, slope limit, step handling; a capsule walks up a ramp, stops at the slope limit, and walks across a cell diagonal without a hop.
- [x] Determinism: the reference scenes (`tests/physics/determinism_test.cpp`) gain a terrain scene (a hundred bodies dropped onto a bumpy field); the state hash is recorded and equal at 1, 2 and 8 threads and on repeat; the expectation of equality across Linux, Windows and macOS is checked on Days 502 and 503.
- [x] Tests: a stack of boxes rests on a slope; a sphere comes to rest in a bowl; no body falls through a grid of 64x64 random bumps in 10 s (the invariant: the lowest body is above the surface minus a tolerance); the debug drawer shows the field's cells near the bodies.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** Bodies and characters collide with a height field deterministically, and the terrain scene's hash is committed.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

`/code-review high` over the day's diff: 4 findings, 0 fixed, 4 recorded.
- The solid-below rule lifts a body found under the surface (a height field has no overhangs; documented).
- The cell debug drawing is quadratic in fields times colliders and capped at 64 triangles a collider (debug only).
- The terrain scene's hashes are committed from the Linux machine; Days 502 and 503 check the other machines.
- The character tests use wide tolerances chosen from one run.
A real defect found by the tests and fixed: a capsule dropped on a steep random field sank into a cell and was pushed down through it (the mesh code picks the shortest way out of a thin surface); a height field now always brings a body out upward (`kSolidBelow`). The 64 by 64 random-bump test with per-sample random heights (slopes up to 71 degrees) fails without it (one body of 60 at -1.4 m under a surface at 1.96 m) and passes with it.
Deviations: continuous collision is not new code: it works through `Collide`, and a body needs `kBodyContinuous` or a non-zero `ccd_threshold` like every body (the default threshold is 0); the character controller needed no change (its capsule sweeps are shape casts against the field's triangles). The debug drawer draws the cells under other colliders' boxes (`DebugDrawOptions::field_cells`).
Terrain golden scene: 100 bodies on a 33 by 33 field, hashes at steps 100 to 600 equal at 0, 1, 2, 8 and 32 threads.
Runs: see STATUS.md.
