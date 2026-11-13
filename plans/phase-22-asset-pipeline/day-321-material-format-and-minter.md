# Phase 22 / Day 321 — The Material Format & the Material Minter

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 314-320; Phase 4 (`renderer::MaterialSystem`, templates).

Materials as data: a template reference, parameters, and texture references by id.

## Tasks

- [ ] **Material blob v1:** a template name (`gbuffer_standard` and the like, resolved by name against the renderer's registered `MaterialTemplateDesc`s), parameters (scalars, vectors, colors with a type tag), texture bindings (slot name, `ResourceId`, sampler description), render-state overrides (the `MaterialRenderState` fields), and a dependency list (every `ResourceId` the material names).
- [ ] **Source form `.mat.src`:** a text file (`template`, `param`, `texture`, `state`) with line-numbered errors; the minter resolves texture paths to ids and records the dependency list; a texture path that does not exist in the project is a mint error, not a runtime surprise.
- [ ] **Parameter validation at mint time** against the template's reflected parameter block (names, types, sizes from `shader_reflection`): an unknown or mistyped parameter is an error with the template's expected list.
- [ ] **Loader `LoadMaterialBlob(bytes) -> MaterialAssetData`** (CPU description; the manager creates the `MaterialId` on Day 323).
- [ ] **Tests:** valid and invalid sources, parameter type errors, the dependency list of a material with three textures, a hostile blob, a golden v0 blob upgrading, determinism.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a `.mat.src` file mints to a validated blob with its dependency list, loads into a material description, and every error is caught at mint time.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
