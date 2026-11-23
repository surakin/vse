# Phase 28 / Day 486 — The Height Map Asset and its Minter

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 485, Phase 22 (the asset pipeline).

A height map is a minted asset: samples in, a blob with the data the renderer and the physics world both need out.

## Tasks

- [ ] **Added by Day 485's spike:** `rhi::Format::R16_UNorm` (Vulkan, Direct3D 12 and Metal names, format table, size rules, null-device test, guide line). **Dropped by it:** the stored normal map (the normals come from four height taps in the shader); read the normal-map wording of the tasks below as removed.

- [ ] `asset::HeightMapData` (a shared unit: add it to `tools/shared_unit_check/units.txt` with `HeightMapDataPtr` and `HeightMapDataConstPtr` aliases in the same commit; CLAUDE.md "Shared representations"): width and height in samples (each at least 2, at most 8192), the world size and the height range, `u16` samples as a `SharedBytes` region by offset, the normal map region, and the min/max pyramid region (one `u16` pair per 32x32 block, then coarser).
- [ ] `asset_mint mint-heightmap` reads a source of 16-bit samples (a raw little-endian `.r16` plus a params file for the size, the world extents and the height range; the engine reads no PNG at run time and the minter reads none either, as the texture minter), checks the sizes, derives the normals with a fixed-order Sobel-like kernel in integer or fixed-order float arithmetic, and builds the pyramid; byte-identical on repeat runs and at any thread count (a golden hash).
- [ ] The blob layout, version and hostile-input checks follow the texture blob: a header with magic and version, sizes checked against the blob length with overflow-safe arithmetic, every offset inside the blob, a refusal of a height range that is not finite or has min above max.
- [ ] `asset/cpu_loaders` gets the `HeightMapData` loader (zero-copy: the data stays in the blob, `SharedBytes`, per CLAUDE.md "Asset loading performance"); the `GpuTexture` loader makes the height texture and the normal texture from it when asked.
- [ ] A CPU sampler `HeightMapData::HeightAt(x, z)` (bilinear, the same diagonal split the collider will use) and `NormalAt`, with tests against hand-computed values on a 4x4 map and a ramp.
- [ ] Tests: mint and load round trip, hostile blobs (a pyramid offset past the end, a 0-wide map, a size that overflows `u32`), the golden hash, the pyramid min/max against a brute-force scan on random maps.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end; quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** A height map is minted deterministically, loaded zero-copy, and sampled on the CPU; the shared-unit gate lists it.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
