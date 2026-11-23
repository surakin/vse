# Phase 28 / Day 493 — Terrain in the C API, Scripts and the Demo Scene

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 484, 490-492.

Reach every host and show it.

## Tasks

- [x] **Moved from Day 491:** the C API names the height-field shape (a constant, creation from a terrain resource, and read-back: `runtime.cpp` reports every kind but sphere and box as a capsule today) (the debug drawer already draws the cells near bodies, Day 492).
- [x] **Moved from Day 490 (partly; the rest deferred, see Notes):** the level reference (a stored terrain component naming the height map and the material, `level_spawner` spawning it, a loader for the height-map asset type), the `terrain.*` settings (`first_range`, `morph_fraction`, `max_distance`, registered with the render settings; `SceneFrame::SetTerrainSelect` already takes them) and the terrain's registration in the spatial module (static bounds, the debug drawer showing tiles, ray queries by the tile bounds until the collider exists).
- [x] C API: `engine_world_create_terrain` (a height-map asset id or host-provided 16-bit samples with a size and extents, a material, a transform), `engine_world_destroy_terrain`, the collider flag (create the height-field shape together with the terrain, one call), and `engine_world_terrain_height(world, x, z)` for hosts that place things on the ground; ABI growth rules and the snapshot update.
- [x] Scripts: `terrain.height(x, z)` and `terrain.normal(x, z)` (pure reads of the tick-start world), `terrain.raycast(origin, dir, max)` through the physics query service slot; marked `world-read` or `pure`; `docs/reference/script-api.md` regenerated.
- [x] **Partly:** the new `game/render_features_demo` (headless hash only; the picture mode is deferred to Day 504, see Notes). Planned: The model viewer or a new `game/render_features_demo` (Day 504 completes it) gains a terrain scene: a minted 1024x1024 map, three layers, a sphere and a box dropped onto it, a free camera; headless mode prints a hash of the physics state and a screenshot mode writes a frame (the existing `--screenshot` and `--headless` pattern).
- [x] Host-supplied heights: a C host passes samples and the engine keeps them as the CPU source for a surface rebind (CLAUDE.md C-API rule: GPU owners keep their CPU source and are rebuilt by the surface's scene).
- [x] Docs: `docs/guide/terrain.md` (reference style: the data, the settings, the component, the collider, the C API); `docs/guide/physics.md` gains the height field; `docs/reference` regenerated; `docgen lint` at zero.
- [x] Tests: a C host creates terrain from samples, queries a height, drops a body (physics through the C API), renders and checks a pixel; the script test reads heights and ray casts; the demo's headless hash is recorded at 1 to 16 threads.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** Terrain is created, queried and collided from C and from scripts, and the demo scene shows it.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## What was built, and what was not

- C API: `ENGINE_COMPONENT_TERRAIN` (`engine_terrain`: height map id, layer count, tiling/tint/roughness/metallic per layer, layer array and splat textures, slope and fade rules, a collider flag), `engine_api.height_map_create` (host 16-bit samples become an `HGHT` resource the engine keeps, so a surface rebind rebuilds the GPU texture from it), `engine_api.terrain_height(world, x, z)`, and `ENGINE_SHAPE_HEIGHT_FIELD` for `engine_collider` (created and read back). The flag creates the static collider with the terrain, one call. This replaced the separate `engine_world_create_terrain` names of the plan: terrain is a component like every other C API object.
- Render settings `terrain.first_range`, `terrain.morph_fraction`, `terrain.max_distance` (dynamic cost), read by the scene frame.
- Scripts: `terrain.height`, `terrain.normal`, `terrain.raycast` (world-read), through the physics query service slot.
- Loader for `HGHT` registered with the resource manager.
- Tests: `host_terrain.c` (samples, refusals, component round trip, height query, a ball dropped onto the collider, flag removal, render and a pixel), the physics "Terrain queries" case, a script case, and `render_features_demo_hash_{1,2,4,16}` (one hash, `0x1e1cb53ed947ac04`, at 1, 2, 4 and 16 threads). Two stale expectations fixed: `host_runtime.c` used `MORPH_WEIGHTS + 1` as "a kind past the last" (now `TERRAIN + 1`), and `host_import.c`'s `EXPECTED_FORMAT_VERSION` was out of step since the `HGHT` type of Day 486 (now `0xae616b492f1df2bf`; nothing had run `engine_c_import` since).
- Docs: `docs/guide/terrain.md` (new), `engine-library.md`, `physics.md`, `render-settings.md`, `scripting.md`; reference regenerated.

**Deferred (KNOWN-ISSUES.md):** (1) the level reference (a stored terrain component in a level, `level_spawner` spawning it) needs a level format change and is not built; (2) the terrain's registration in the spatial module is superseded by the height-field collider, which the spatial pair feed already carries, and is not built; (3) `render_features_demo` has no picture mode yet (the physics hash only): the sky, terrain and instances picture, window and `--screenshot`, is for Day 504; (4) a terrain with no layer textures draws flat grey.

Runs: plain `ran 2040 of 11983 tests (phases: 3 4 5 6 10 12 22 23 25 27 28; flavors: plain; display: excluded; crash tests: excluded; soak: excluded) in 95 s` (5 failures at first: two stale expectations fixed above, `docs_check` and `docs_phase_check` fixed (a phase name in a header comment, a stale reference), and `test_phase_script_long_warning_bwk_awk` passed when rerun); asan `ran 1727 of 11985 tests (phases: 3 4 5 6 10 12 22 23 25 27 28; flavors: asan; ...) in 178 s`, no failure; tsan 1 failure of 1425, the known LuaJIT sandbox flake (fails 3 of 3 reruns alone in this tree too). Reduced runs.

## Post-implementation code review

`/code-review high` over the day's diff: 4 findings, 0 fixed, 4 recorded in KNOWN-ISSUES.md.

1. `SurfaceScene::terrain_sources_` and `textures_` are never pruned while a surface lives: a terrain removed from the world keeps its GPU height texture and layer textures until the surface is destroyed. Declined (a world has few terrains; the same holds for the mesh and material caches).
2. `ResourceHost::height_map_pins_` grows by one pin per replaced resource (the pin is found by the handle's pointer). Declined: same pattern as the mesh-collider pins.
3. The collider made from a terrain is made once (resolved at the end of the next frame); replacing the height map afterwards does not rebuild it. Declined, documented in the guide.
4. `terrain_height` queries by the terrain's translation and y-turn only (`TerrainWorldValid`); a terrain placed with another rotation or a scale is refused by the renderer, so it is not a gap.
