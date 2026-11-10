# Phase 17 / Day 233 — The Scene Bridge: Entities to the Renderer

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Days 229, 231 and 232; Phase 5 `RenderScene`, `GpuMesh`, materials.

New module `engine/scene`: renderable components and the system that mirrors them into the renderer's `RenderScene`, which stays unchanged.

## Tasks

- [ ] **Module** `engine/scene` (CMake library with flavors, `modules.txt`, `ARCHITECTURE.md`, `docs/reference/modules.txt`); depends on `ecs` and `renderer`; nothing in `ecs` or `renderer` includes it.
- [ ] **Components:** `MeshRenderer { SharedPtr<GpuMeshHandle> mesh; u32 material; u32 flags; }` (the mesh component owns or shares the `GpuMesh`, as the Phase 5 notes require: a ref-counted holder `scene::MeshAsset` wrapping a `GpuMesh` so the scene entry cannot outlive it), `Light { kind, color, intensity, range, ... }` mirroring `renderer::Light`, `Camera { fov, near, far, ... }` plus the `ActiveCamera` resource, `Hidden` tag (maps to `ObjectFlags::kHidden`).
- [ ] **`RenderSync` system (stage `kRender`):** keeps a `RenderHandle { ObjectId | LightId }` component on each mirrored entity; for entities with `MeshRenderer` and `WorldTransform` but no `RenderHandle` it calls `RenderScene::AddObject`; for `Changed<WorldTransform>` it calls `SetTransform`; for `Changed<MeshRenderer>` it updates mesh/material/flags (re-adds when the mesh pointer changed); removal: entities that lost their `MeshRenderer` or were destroyed are detected through a `RenderHandle` that has no `MeshRenderer` / a cleanup hook (a `RemovedComponents<RenderHandle>` list kept by the world for components that opt in with `kTrackRemoval`, populated at destroy/remove and cleared at the end of the frame) and call `RemoveObject`/`RemoveLight`; capacity exhaustion of the `RenderScene` is a counted refusal, not a crash.
- [ ] **`ecs::World` support for removal tracking** (`kTrackRemoval` opt-in per component type; tested in `tests/ecs/removal_test.cpp`; the list holds the entity id and the removed component's value for non-tag types).
- [ ] **Camera:** a system that builds the `renderer::Camera` from the `ActiveCamera` entity's `WorldTransform` and `Camera` component (the demos read it).
- [ ] **Tests** (`tests/scene/scene_sync_test.cpp`, CPU with a null device `GpuMesh` where possible): creating N entities adds N objects with the entities' matrices; moving a parent moves its subtree's objects (through the hierarchy system) and calls `SetTransform` only for changed entities (counted); destroying an entity removes its object and light; swapping the mesh re-adds; hiding sets the flag; `RenderScene` full capacity is counted; a 100k-entity static scene costs a `Changed` query and no `RenderScene` calls per frame; the mesh asset outlives the entity that dropped it until the last user is gone (counting destructor); GPU test (`tests/scene/scene_render_test.cpp`, per backend): an entity-built scene renders the same pixels as the `RenderScene` built by hand (compared with the Phase 5 tolerance).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors tsan (the sync is a parallel-capable system) and asan; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md` "Rendering"; `docs/guide/renderer.md` cross-reference.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** entities with a mesh, a transform and a material appear in the renderer through an incremental sync that touches the `RenderScene` only for what changed, with removal and capacity handled, and the rendered pixels equal the hand-built scene.

## Notes

- Phase 18 (spatial partitioning) later adds the visibility system and a precomputed-visible overload of `RenderScene::Cull` on top of this bridge; nothing here needs to change for it, but the `RenderSync` should not assume it is the only thing that decides what is drawn.
- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
