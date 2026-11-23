# Phase 28 / Day 488 — Drawing the Terrain

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 481, 486-487.

Draw the selected tiles into the G-buffer.

## Tasks

- [x] One shared grid mesh (`tile_resolution` squared vertices plus skirt vertices) as a `GpuMesh`; one instance per selected tile in a storage buffer (`TileInstance`: tile origin, size, level, morph factor, height-texture rectangle); a single instanced indexed draw per material, so a frame is a few draws for any map size.
- [x] `shaders/terrain.vert.slang`: reads the height texture in the vertex stage (Day 485 spike result), displaces the grid vertex, morphs toward the coarser level near the level boundary (a geomorph so there is no pop), and writes the world position, the normal from the normal texture and the texture coordinates; `terrain.frag.slang` is the G-buffer fragment of Day 489 (here a flat grey so the geometry can be tested first).
- [x] Cracks: skirts (a ring of vertices pulled down by a fixed amount at the tile edge) if the spec chose them, or edge morphing to the neighbour's level; the spec's choice is implemented and a test draws two adjacent tiles of different levels and finds no background pixel along the seam.
- [x] The height and normal textures are `GpuTexture` owners with their CPU source kept (the asset), registered with `DeviceResources` at their tier, never cached by handle; the tile instance buffer is a per-frame transient.
- [x] The pass writes the same G-buffer attachments as meshes (`gbuffer.h`), depth included, so the lighting pass, the sky pass (Day 481) and every later pass work unchanged; the terrain's depth is correct against meshes (a box half buried in the terrain shows the intersection).
- [x] Tests: the CPU model of the displaced vertex (a function the shader mirrors) against a read-back of a drawn flat patch; the seam test; the buried-box test; a reset and a resize; the draw count of a 4096x4096 map from a high camera is under the recorded budget.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** A height map is drawn as a lit, levelled terrain without cracks or popping, and meshes intersect it correctly.

## Notes

- Deviations: the tile vertices are made in the vertex shader from the vertex id instead of a shared `GpuMesh` (no vertex or index buffer, no skirts: the geomorph closes the seams); the normals come from height differences in the vertex shader, so there is no normal texture (the height texture is the only owner); the DXIL and Metal blobs are placeholders until the PC and the Mac mint them.
- Pages touched: `docs/guide/renderer.md` ("Drawing the terrain"), `docs/reference/`.
- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

`/code-review high` over the day's diff: 6 findings.

- Fixed: cells of a clipped last tile were drawn past the map edge (the shader clamps positions to the map's far corner; regression test `A tile clipped by the map edge...` shown to fail without the clamp); `FrameRegion` called in the constructor before `BeginFrame` (aborted in the first GPU run); `sky.cpp` and `sky_gpu_test.cpp` did not compile with unity off or with GCC (missing include, test helper `Close` from another file).
- Recorded, not fixed: the seam test cannot detect cracks (`KNOWN-ISSUES.md`); the morph distance is to the unmorphed vertex; a frame over 4096 tiles drops the coarsest ones.

Runs: plain `ran 1272 of 11886 tests (phases: 3 4 5 6 22 28; flavors: plain; display: excluded; crash tests: excluded; soak: excluded) in 51 s` (`test_phase_script_long_warning_bwk_awk` failed, the known timing flake), asan `ran 1091 of 11886 tests`, tsan `ran 797 of 11886 tests`; reduced runs. GCC (`build-gcc`) and `-DENGINE_UNITY_BUILD=OFF` build clean. `ctest -L docs` 13 of 13.
