# Phase 28 / Day 493 — Terrain in the C API, Scripts and the Demo Scene

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 484, 490-492.

Reach every host and show it.

## Tasks

- [ ] C API: `engine_world_create_terrain` (a height-map asset id or host-provided 16-bit samples with a size and extents, a material, a transform), `engine_world_destroy_terrain`, the collider flag (create the height-field shape together with the terrain, one call), and `engine_world_terrain_height(world, x, z)` for hosts that place things on the ground; ABI growth rules and the snapshot update.
- [ ] Scripts: `terrain.height(x, z)` and `terrain.normal(x, z)` (pure reads of the tick-start world), `terrain.raycast(origin, dir, max)` through the physics query service slot; marked `world-read` or `pure`; `docs/reference/script-api.md` regenerated.
- [ ] The model viewer or a new `game/render_features_demo` (Day 504 completes it) gains a terrain scene: a minted 1024x1024 map, three layers, a sphere and a box dropped onto it, a free camera; headless mode prints a hash of the physics state and a screenshot mode writes a frame (the existing `--screenshot` and `--headless` pattern).
- [ ] Host-supplied heights: a C host passes samples and the engine keeps them as the CPU source for a surface rebind (CLAUDE.md C-API rule: GPU owners keep their CPU source and are rebuilt by the surface's scene).
- [ ] Docs: `docs/guide/terrain.md` (reference style: the data, the settings, the component, the collider, the C API); `docs/guide/physics.md` gains the height field; `docs/reference` regenerated; `docgen lint` at zero.
- [ ] Tests: a C host creates terrain from samples, queries a height, drops a body (physics through the C API), renders and checks a pixel; the script test reads heights and ray casts; the demo's headless hash is recorded at 1 to 16 threads.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** Terrain is created, queried and collided from C and from scripts, and the demo scene shows it.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
