# Phase 22 / Day 326 — The Material Format & the Material Minter

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 314-325; Phase 4 (`renderer::MaterialSystem`, templates).

Materials as data: a template reference, parameters, and texture references by id.

## Tasks

- [x] **Skinning flag (added 2026-11-13):** a material records whether its geometry shader has the `SKINNED` permutation (Day 322), as a bit in the material header; the minter sets it from the shader's permutation table; the material system's mismatch report (a non-skinnable material on a skinned mesh) names both, and a test mints a skinnable and a non-skinnable material and checks the flag.
- [x] **Material blob v1:** a template name (`gbuffer_standard` and the like, resolved by name against the renderer's registered `MaterialTemplateDesc`s), parameters (scalars, vectors, colors with a type tag), texture bindings (slot name, `ResourceId`, sampler description), render-state overrides (the `MaterialRenderState` fields), and a dependency list (every `ResourceId` the material names).
- [x] **Source form `.mat.src`:** a text file (`template`, `param`, `texture`, `state`) with line-numbered errors; the minter resolves texture paths to ids and records the dependency list; a texture path that does not exist in the project is a mint error, not a runtime surprise; **references are typed:** a slot declares the texture shape it accepts (2D, cube, array; color space), and the minter checks the referenced texture's blob metadata against it, so binding a cube map to a 2D slot is a mint error; the dependency list is built by the shared reference collector (Day 314).
- [x] **Parameter validation at mint time** against the template's reflected parameter block (names, types, sizes from `shader_reflection`): an unknown or mistyped parameter is an error with the template's expected list.
- [x] **Loader `LoadMaterialBlob(bytes) -> MaterialAssetData`** (CPU description; the manager creates the `MaterialId` on Day 328).
- [x] **The source carries its id (2026-11-13, no sidecar):** the material source has an `id` field in its header (a first line `id <hex>` in a text form), written only by `asset_mint adopt` (Day 333); the parser reads and round-trips it, a missing id is the error that names `adopt`, and the minter writes the id into the blob header (Day 314); a test per format.
- [x] **Tests:** valid and invalid sources, parameter type errors, the dependency list of a material with three textures, a hostile blob, a golden v0 blob refused by the loader and upgraded by `remint`, determinism; parameters are stored in the template's uniform block layout (std140 offsets resolved at mint time) so loading a material copies one block.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [x] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 339; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a `.mat.src` file mints to a validated blob with its dependency list, loads into a material description, and every error is caught at mint time.

## Outcome

Built as planned, with these deviations:

- **Template names** are the renderer's built-in `gbuffer`, `gbuffer_color`, `gbuffer_skinned` and `gbuffer_skinned_color`. The renderer gained `FindGBufferTemplateInfo` (`engine/renderer/gbuffer.h`): per template the skinned flag, the fragment permutation table, the texture slots with their shape (2D, cube, array) and color space (any, sRGB, linear), and the parameter defaults. The minter reads the fragment module's reflection (`MakePermutationKey`, `FindPermutation`, `ParseShaderReflection`) for the set 1 uniform block, so parameter names, types and std140 offsets come from the shader, not a copy.
- **The format** (`engine/asset/material.*`, sections 16 to 21): a 96-byte header (flags, counts, state overrides, template name), the parameter block, a parameter table, texture bindings, samplers and permutation options. The skinnable flag is bit 0, set from the template. One dependency (`TEXR`) per distinct texture, the role being the first slot that names it; the loader checks the list against the bindings and every reserved byte.
- **The minter** (`tools/asset_mint/material_source.*`, `asset_mint mint-material`): defaults first, then the source's values written at the reflected offsets; an unknown or mistyped parameter lists the template's members; a texture is resolved through a `MaterialReferenceResolver` (the CLI reads `.texr` files beside the source and takes the id from the blob), its `TextureView` kind and `Srgb()` are checked against the slot (a cube in a 2D slot, linear data in the albedo slot, a normal map without `option NORMAL_MAP` are mint errors with the line).
- **`remint`** does not exist until Day 333: the golden "v0" is a blob whose type-version field is 0, and the loader refuses it with `UnsupportedVersion` naming `asset_mint remint`.
- The material is a description only (`MaterialAssetData`); the `MaterialId` is made on Day 328.

Runs: plain `ran 3054 of 8177 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2469 of 8177`, tsan `ran 2142 of 8177` (the DXIL module test waits for the PC to re-mint `gbuffer.vert`, and the known tsan Vulkan SegFaults); Clang non-unity, GCC unity and non-unity clean; `ctest -L docs` passes; the module, platform, std, config, file and phase-map gates pass.

## Post-implementation code review

`/code-review high` over the day's diff: 4 findings, all fixed.

- A repeated `param`, `texture` or `sampler` line reached the blob builder and failed with line 0; the minter now refuses it with the line of the second record (tests added: each of the three fails without the check).
- A resolver failure other than "not found" returned a status with no message and line 0; it now reports the line and the resolver's message.
- `tests/asset/material_test.cpp` lacked `cstr/format.h` (found by the non-unity builds); added.
- The hostile-blob test first assumed every flipped byte is refused; the container's hash is only checked by `Verify`, so it now requires refusal for the container header, the counts and the reserved bytes, and absence of a crash for the rest.
