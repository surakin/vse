# Phase 28 / Day 490 — Terrain in the Scene: ECS, Render Sync and Spatial Cells

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 487-489, Phase 17 (ECS), Phase 18.

Make terrain an ordinary part of a world.

## Tasks

- [ ] `scene::Terrain` component (the height-map asset id, the material id, the world transform (translation and yaw only, the spec says why scale and tilt are out), the layer mask) with validation; `scene::MakeRenderSyncSystem` creates or updates the renderer's terrain object, tears it down on removal, and survives a changed asset (hot reload) by rebuilding the quadtree.
- [ ] More than one terrain per world (a world made of neighbouring maps) is supported as separate components; the tile selection merges their lists; the seam between two maps is the user's to hide (documented), the engine only guarantees no cracks inside one map.
- [ ] `ObjectId`-style generation-checked `TerrainId` in `render_scene.h`; the scene capacity rules stay (fixed capacity, stable ids).
- [ ] Spatial: the terrain registers its static bounds once (Day 487) and a `spatial::RaycastSet`-style query hits the terrain surface exactly through the collider (Day 491) when one exists, and by the tile bounds otherwise; the spatial debug drawer shows the tiles.
- [ ] Level loading: `asset::Level` can reference a terrain entity (a level format version bump with the loader for the current version only, per "no load-time conversions"; the old minted levels are re-minted by the build) and `level_spawner` spawns it.
- [ ] Tests: an ECS world with a terrain and a camera renders the terrain; remove and re-add; hot reload of the height map changes the heights; two terrains side by side; a level with a terrain spawns it; the scene capacity test counts a terrain as one object.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** A world holds terrain as a component, saves and loads it with levels, and renders it through the render sync system.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
