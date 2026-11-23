# Phase 28 / Day 486 — The Height Map Asset and its Minter

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 485, Phase 22 (the asset pipeline).

A height map is a minted asset: samples in, a blob with the data the renderer and the physics world both need out.

## Tasks

- [x] **Added by Day 485's spike:** `rhi::Format::R16_UNorm` (Vulkan, Direct3D 12 and Metal names, format table, size rules, null-device test, guide line). **Dropped by it:** the stored normal map (the normals come from four height taps in the shader); read the normal-map wording of the tasks below as removed.

- [x] `asset::HeightMapData` (a shared unit: add it to `tools/shared_unit_check/units.txt` with `HeightMapDataPtr` and `HeightMapDataConstPtr` aliases in the same commit; CLAUDE.md "Shared representations"): width and height in samples (each at least 2, at most 8192), the world size and the height range, `u16` samples as a `SharedBytes` region by offset, the normal map region, and the min/max pyramid region (one `u16` pair per 32x32 block, then coarser).
- [x] `asset_mint mint-heightmap` reads a source of 16-bit samples (a raw little-endian `.r16` plus a params file for the size, the world extents and the height range; the engine reads no PNG at run time and the minter reads none either, as the texture minter), checks the sizes, derives the normals with a fixed-order Sobel-like kernel in integer or fixed-order float arithmetic, and builds the pyramid; byte-identical on repeat runs and at any thread count (a golden hash).
- [x] The blob layout, version and hostile-input checks follow the texture blob: a header with magic and version, sizes checked against the blob length with overflow-safe arithmetic, every offset inside the blob, a refusal of a height range that is not finite or has min above max.
- [x] `asset/cpu_loaders` gets the `HeightMapData` loader (zero-copy: the data stays in the blob, `SharedBytes`, per CLAUDE.md "Asset loading performance"); the `GpuTexture` loader makes the height texture and the normal texture from it when asked.
- [x] A CPU sampler `HeightMapData::HeightAt(x, z)` (bilinear, the same diagonal split the collider will use) and `NormalAt`, with tests against hand-computed values on a 4x4 map and a ramp.
- [x] Tests: mint and load round trip, hostile blobs (a pyramid offset past the end, a 0-wide map, a size that overflows `u32`), the golden hash, the pyramid min/max against a brute-force scan on random maps.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end; quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** A height map is minted deterministically, loaded zero-copy, and sampled on the CPU; the shared-unit gate lists it.

## Notes

- Done 2026-11-23 on Linux. Built: `rhi::Format::R16_UNorm` (appended after `BC4_UNorm` so no stored format code moves; Vulkan, Direct3D 12 and Metal mappings; format-table test), `asset::HeightMapView` and `BuildHeightMapBlob` (`engine/asset/height_map.{h,cpp}`, type `HGHT`: header, packed mip levels, block pyramid), `HeightAt` (the collider's triangle split) and `NormalAt`, and `asset_mint mint-heightmap` with `.hgt.src` (`tools/asset_mint/height_source.*`).
- **Differences from the task list:** (1) no stored normal map (Day 485); (2) the view is a plain value over a `SharedBytesPtr` by offsets, like `AudioClipView`, so it is not added to `shared_unit_check/units.txt`; (3) the pyramid is one level (blocks of 8 by 8 cells), the coarser levels are made by the quadtree; (4) the GPU texture loader is left to Day 488, which makes the texture from the levels; (5) the project-level `asset_mint mint` does not yet know `.hgt.src` (only the `mint-heightmap` command does); (6) the new test file is phase 22 like the rest of `tests/asset/` (a directory line cannot be overridden per file).
- Fixed on the way: the Day 485 spec page was not linked from `docs/README.md` (`docs_links`).
- Runs: plain `--affected` `ran 1187 of 11840 tests (phases: 3 4 5 22 28; flavors: plain; ...)`; asan `ran 1074 of 11840`; tsan `ran 785 of 11840`; all passed, reduced runs.

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

`/code-review high`: 2 findings, both nits, none fixed. (1) `R16_UNorm` is not created on a device until Day 488: declined, mandatory format on all three APIs, proven by the terrain GPU test. (2) The block pyramid is not checked against the samples at open: declined, a forged pyramid cannot cause a memory error; recomputing costs a pass over level 0.
