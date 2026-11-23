# Phase 28 / Day 488 — Drawing the Terrain

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 481, 486-487.

Draw the selected tiles into the G-buffer.

## Tasks

- [ ] One shared grid mesh (`tile_resolution` squared vertices plus skirt vertices) as a `GpuMesh`; one instance per selected tile in a storage buffer (`TileInstance`: tile origin, size, level, morph factor, height-texture rectangle); a single instanced indexed draw per material, so a frame is a few draws for any map size.
- [ ] `shaders/terrain.vert.slang`: reads the height texture in the vertex stage (Day 485 spike result), displaces the grid vertex, morphs toward the coarser level near the level boundary (a geomorph so there is no pop), and writes the world position, the normal from the normal texture and the texture coordinates; `terrain.frag.slang` is the G-buffer fragment of Day 489 (here a flat grey so the geometry can be tested first).
- [ ] Cracks: skirts (a ring of vertices pulled down by a fixed amount at the tile edge) if the spec chose them, or edge morphing to the neighbour's level; the spec's choice is implemented and a test draws two adjacent tiles of different levels and finds no background pixel along the seam.
- [ ] The height and normal textures are `GpuTexture` owners with their CPU source kept (the asset), registered with `DeviceResources` at their tier, never cached by handle; the tile instance buffer is a per-frame transient.
- [ ] The pass writes the same G-buffer attachments as meshes (`gbuffer.h`), depth included, so the lighting pass, the sky pass (Day 481) and every later pass work unchanged; the terrain's depth is correct against meshes (a box half buried in the terrain shows the intersection).
- [ ] Tests: the CPU model of the displaced vertex (a function the shader mirrors) against a read-back of a drawn flat patch; the seam test; the buried-box test; a reset and a resize; the draw count of a 4096x4096 map from a high camera is under the recorded budget.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** A height map is drawn as a lit, levelled terrain without cracks or popping, and meshes intersect it correctly.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
