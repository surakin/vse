# Phase 28 / Day 487 — Terrain Tile Selection on the CPU

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 486, Phase 18 (spatial).

Decide, on the CPU and deterministically, which tiles of which level of detail are drawn for a camera.

## Tasks

- [ ] `renderer::TerrainTiles`: the quadtree over a `HeightMapData` (tile size and level count from the spec), built once per map, holding for each node its bounds (from the pyramid, so the box is tight in height), a `FixedVector` of nodes in breadth-first order, no pointers.
- [ ] `SelectTiles(camera, settings, out)`: a recursive split by distance to the tile bounds (ranges doubling per level), culling against the frustum with `renderer::Frustum`, with the rule that adjacent tiles differ by at most one level (or the stitching of Day 488 handles any difference; the spec fixed which); output sorted in a fixed order (level, then z, then x) so equal inputs give equal lists on any thread count.
- [ ] `TerrainSettings` (`terrain.max_lod_distance`, `terrain.tile_resolution`, `terrain.morph_range`, `terrain.height_scale_override` is not a setting) registered in `settings::Registry` per CLAUDE.md "Anything a user can configure is a setting", classified in `render-settings.md` (changing the tile resolution rebuilds the grid mesh; the distance values are per frame).
- [ ] Culling against the spatial module: the terrain's tiles are inserted as static bounds into the world's `StaticBvh` when the terrain is added so a ray or an overlap query finds the terrain without a private grid (CLAUDE.md "Spatial queries go through one module"); the renderer's own frustum cull stays the per-frame selection.
- [ ] Tests: a flat map selects the expected counts at known distances; the selection is the same for 1 and 8 threads and on repeat; a camera inside the map, above it, and far away; no tile is selected twice and the selected tiles cover every visible cell exactly once (checked by area); a moving camera changes the list by whole tiles; timing of 1,000 selections on a 4096x4096 map recorded (a bench line, not a hard limit).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end; quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** The renderer picks terrain tiles for a camera, deterministically, with tests that prove coverage without holes or overlaps.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
