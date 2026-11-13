# Phase 22 / Day 323 — The Resource Manager: Handles, Cache, Dependencies & Synchronous Loading

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 314-322; Phase 8 (`Recreatable`, `DeviceResources`).

The runtime face of the system: ask for a resource by id, get a handle, never know which backend served it.

## Tasks

- [ ] **`asset::ResourceManager`:** owns the mount table and a cache; `Load<T>(id) -> Handle<T>` synchronous; `Get(handle)`; reference counts (a handle copy adds one); a resource is freed when its count reaches zero and it is not pinned; a generation in every handle so a stale handle reads as invalid, never as another resource.
- [ ] **Per-type loaders registered with the manager** (`RegisterLoader<T>`): mesh (builds a `renderer::GpuMesh`), texture (a `GpuTexture` made with a loader that re-reads through the manager, so a device reset recreates it: the `Recreatable` rule of CLAUDE.md), material (creates the `MaterialId`, binds textures by owner), level (spawns entities through the world; `MeshRenderer` gets a `SharedPtr<MeshAsset>` from the manager).
- [ ] **Dependency graph:** loading a material loads its textures first; a cycle is an error (levels may name levels only through an explicit "include" that is checked acyclic at mint time); unloading a dependent releases its dependencies.
- [ ] **Errors:** a missing resource returns a handle to a per-type *fallback* (a checker texture, a magenta material, a unit cube) and logs once, counted in `Stats`; the call also returns the `Status` so tools can be strict (`assets.strict`).
- [ ] **`Stats`:** loads, cache hits, bytes resident, per-backend reads, fallbacks used.
- [ ] **Settings** `assets.*` (`mounts`, `strict`, `cache_bytes`) through `settings::Registry`; `RegisterAssetSettings`, `ManagerConfigFromSettings`.
- [ ] **Tests:** load every type from a memory backend and from a pak of the same blobs with identical results (the "never know which backend" test); override by a loose mount; a device reset (`tests/renderer/reset_rig.h`) leaves the textures and meshes intact and the counts equal (the Phase 8 pattern); reference counting and generations under a stale handle; a dependency cycle; fallbacks; a level that loads its meshes and renders the same pixels as the hand-built scene (a golden image through the scene bridge).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** all four types load through the manager from any backend with identical results, a device reset recreates them, missing resources fall back visibly, and a level renders like its hand-built twin.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
