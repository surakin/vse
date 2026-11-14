# Phase 22 / Day 329 — GPU-Backed Resources, the Level Spawner & Surviving a Device Reset

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Day 328; Phase 4 (`renderer::MaterialSystem`), Phase 8 (`Recreatable`, `DeviceResources`), Phase 17 (`scene`), Day 322 (GPU skinning).

The resources that live on the GPU, loaded through the manager of Day 328 so that a device reset rebuilds them from the manager's own bytes, and the spawner that turns a level into entities that draw. Split from Day 328 on 2026-11-14 (the user's decision).

## Tasks

- [ ] **GPU-backed loaders (registered over the CPU ones):** `MESH` builds a `renderer::GpuMesh` through the trusted zero-copy entry Day 319 left open (a `TrustedMeshView` with one or two vertex streams, the skin stream of Day 322), made recreatable; `TEXR` builds a `GpuTexture` from the blob's levels (`CreateFromLevels`) with a loader that re-reads through the manager, so a device reset recreates it (the `Recreatable` rule of CLAUDE.md); `MATL` creates the `MaterialId` and binds textures by owner, never by handle. A skinned mesh on a material that is not skinnable is refused naming both.
- [ ] **Per-type fallbacks (behind `assets.allow_fallbacks`, default off):** a checker texture, a magenta material and a unit cube, each use logged and counted in `Stats::fallbacks_used`; the demo and every test that loads minted data assert the count is zero.
- [ ] **The level spawner (`scene`):** `scene::SpawnLevel` loads a level through the manager (its closure first), creates the entities with `asset::LoadLevelBlob`, and gives each `MeshReference` a `scene::MeshRenderer` whose `SharedPtr<MeshAsset>` holds the manager's mesh; `include` levels are spawned with it. The edge `scene -> asset` is new (the table allows it: `scene -> animation -> asset` exists already): `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets say so.
- [ ] **The animation adapter:** an `animation::AnimationResolver` over the manager (skeletons and clips by id), in the module that may see both, with the one-way edge documented.
- [ ] **Device reset (`tests/renderer/reset_rig.h`, the Phase 8 pattern):** textures, meshes (with and without the skin stream) and materials loaded through the manager survive a reset with the counts from `Device::Resources()` equal before and after.
- [ ] **Backend independence for GPU resources:** every type loads from a memory backend and from a pak of the same blobs with identical results; `Stats::load_conversions` is zero after loading every type.
- [ ] **A level renders like its hand-built twin:** a level that loads its meshes through the manager renders the same pixels as the hand-built scene (a golden image through the scene bridge).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 339; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a level loaded through the manager renders like its hand-built twin, a device reset recreates every GPU resource it loaded with the counts equal, a missing resource falls back visibly only when the development setting says so, and every type loads identically from a memory backend and from a pak.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
