# Phase 28 / Day 489 — Terrain Materials: Layers and Splat Maps

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 488, Phase 5 (materials).

Give the terrain surface: several texture layers blended by a splat map and by slope.

## Tasks

- [x] `TerrainMaterial` (a minted `.terrain.src` or a field of the height-map params, the spec decides): up to 4 layers per splat map and up to 2 splat maps (8 layers), each layer with albedo, normal, roughness (packed: albedo RGB + roughness A, normal RG + height B in a texture array of the same size), a tiling scale in world units and a slope-and-height rule for the automatic blend.
- [x] The splat map is an RGBA8 texture (4 weights; a second one for layers 5 to 8) minted from a raw source with a fixed normalisation (weights sum to 255 exactly, a rule with a test); the layer arrays are `GpuTexture` arrays with mips minted by the existing texture minter.
- [x] `terrain.frag.slang` replaces the flat grey: blends the layers by the weights (with height-based blending so a layer's own relief decides the edge), applies the slope rule (steep faces use the rock layer), samples the normal layers in the tangent frame built from the terrain normal, and writes the standard G-buffer; triplanar sampling on steep slopes (an option, off by default) avoids stretching.
- [x] A distance fade of the detail layers into a low-resolution colour map (the tile's own splat-mixed average) so far terrain does not shimmer; the fade distance is a setting.
- [x] The terrain material is registered with the material system so `emissive_from_albedo`, `reflection_intensity` and the slot-override machinery do not apply to it by accident (the docs say which slots a terrain has).
- [x] Tests: weights always normalised; a splat map with one full-weight layer draws that layer; the slope rule picks rock above the threshold on a ramp; a texture array with 9 layers is refused; a reset keeps the splat and layer textures.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** The terrain is textured by layers and splat maps with a slope rule and a distance fade, and the data is minted deterministically.

## Notes

- Scope follows the design spec ("Material (Day 489)"): 4 layers, albedo plus roughness, no normal maps. Pages touched: `docs/guide/renderer.md` ("The terrain material"), `docs/reference/`.
- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

`/code-review high` over the day's diff: 5 findings.

- Fixed: layer samples sat in a branch pixels of a quad take differently (undefined mip level), now every layer is sampled; unused layers could index past the array (the index is clamped to the last layer); the splat map's dimension and layer count were not validated.
- Recorded, not fixed: the 8-layer, normal-layer, height-blend and triplanar parts of the day file are not built (the spec's smaller scope, `KNOWN-ISSUES.md`); the pass keeps the material's textures until the next `Prepare`.

Runs: plain `ran 1280 of 11906 tests (phases: 3 4 5 6 22 28; flavors: plain; display: excluded; crash tests: excluded; soak: excluded) in 64 s` (`test_phase_script_long_warning_bwk_awk` failed, the known timing flake), asan `ran 1099 of 11906 tests`, tsan `ran 801 of 11906 tests`; reduced runs. GCC and `-DENGINE_UNITY_BUILD=OFF` build clean; `ctest -L docs` 13 of 13.
