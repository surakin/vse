# Phase 17 / Day 233 — The Scene Bridge: Entities to the Renderer

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Days 229, 231 and 232; Phase 5 `RenderScene`, `GpuMesh`, materials.

New module `engine/scene`: renderable components and the system that mirrors them into the renderer's `RenderScene`, which stays unchanged.

## Tasks

- [x] **Module** `engine/scene` (CMake library with flavors, `modules.txt`, `ARCHITECTURE.md`, `docs/reference/modules.txt`); depends on `ecs` and `renderer`; nothing in `ecs` or `renderer` includes it.
- [x] **Components:** `MeshRenderer { SharedPtr<GpuMeshHandle> mesh; u32 material; u32 flags; }` (the mesh component owns or shares the `GpuMesh`, as the Phase 5 notes require: a ref-counted holder `scene::MeshAsset` wrapping a `GpuMesh` so the scene entry cannot outlive it), `Light { kind, color, intensity, range, ... }` mirroring `renderer::Light`, `Camera { fov, near, far, ... }` plus the `ActiveCamera` resource, `Hidden` tag (maps to `ObjectFlags::kHidden`).
- [x] **`RenderSync` system (stage `kRender`):** keeps a `RenderHandle { ObjectId | LightId }` component on each mirrored entity; for entities with `MeshRenderer` and `WorldTransform` but no `RenderHandle` it calls `RenderScene::AddObject`; for `Changed<WorldTransform>` it calls `SetTransform`; for `Changed<MeshRenderer>` it updates mesh/material/flags (re-adds when the mesh pointer changed); removal: entities that lost their `MeshRenderer` or were destroyed are detected through a `RenderHandle` that has no `MeshRenderer` / a cleanup hook (a `RemovedComponents<RenderHandle>` list kept by the world for components that opt in with `kTrackRemoval`, populated at destroy/remove and cleared at the end of the frame) and call `RemoveObject`/`RemoveLight`; capacity exhaustion of the `RenderScene` is a counted refusal, not a crash.
- [x] **`ecs::World` support for removal tracking** (`kTrackRemoval` opt-in per component type; tested in `tests/ecs/removal_test.cpp`; the list holds the entity id and the removed component's value for non-tag types).
- [x] **Camera:** a system that builds the `renderer::Camera` from the `ActiveCamera` entity's `WorldTransform` and `Camera` component (the demos read it).
- [x] **Tests** (`tests/scene/scene_sync_test.cpp`, CPU with a null device `GpuMesh` where possible): creating N entities adds N objects with the entities' matrices; moving a parent moves its subtree's objects (through the hierarchy system) and calls `SetTransform` only for changed entities (counted); destroying an entity removes its object and light; swapping the mesh re-adds; hiding sets the flag; `RenderScene` full capacity is counted; a 100k-entity static scene costs a `Changed` query and no `RenderScene` calls per frame; the mesh asset outlives the entity that dropped it until the last user is gone (counting destructor); GPU test (`tests/scene/scene_render_test.cpp`, per backend): an entity-built scene renders the same pixels as the `RenderScene` built by hand (compared with the Phase 5 tolerance).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors tsan (the sync is a parallel-capable system) and asan; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md` "Rendering"; `docs/guide/renderer.md` cross-reference.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** entities with a mesh, a transform and a material appear in the renderer through an incremental sync that touches the `RenderScene` only for what changed, with removal and capacity handled, and the rendered pixels equal the hand-built scene.

## Notes

- Phase 18 (spatial partitioning) later adds the visibility system and a precomputed-visible overload of `RenderScene::Cull` on top of this bridge; nothing here needs to change for it, but the `RenderSync` should not assume it is the only thing that decides what is drawn.
- Layering: `engine/scene` is layer 13; the programs (demos, benchmarks, tools) moved to layer 14 in `modules.txt` and `ARCHITECTURE.md` so Day 234's demos may use it. `docs/reference/modules.txt` has the `scene` module and the lint baseline a `scene` line (the `ecs` and `foundation/lang` lines were refreshed to their real counts).
- Removal tracking is retained per *frame*, not per world tick: `Query::Finish()` advances the tick, so a tick-based window pruned entries before a once-per-frame reader ran (first version, caught by the first scene tests). `World::EndFrame()` (called by `Schedule::Run`) ends a frame; entries live for the frame they were logged in and the next. Tracked types must be trivially copyable, default constructible and not tags.
- Deviations: (1) the `RenderSync` is single-threaded (the `RenderScene` is not safe for concurrent writes), so the plan's "parallel-capable" is only that it can share a wave with systems that do not touch the scene; (2) `MeshRenderer.flags` holds extra `ObjectFlags` bits and the `Hidden` tag adds `kHidden`; the tag change is found through the arrival stamp of `RenderHandle` (a tag has no column), which fires on any migration and costs one comparison per row of such a chunk; (3) there is no `SetMaterial` in the `RenderScene`, so a mesh or material change is a remove plus add (the slot and so the draw order may change); (4) an entity mirrors a mesh or a light, not both (the light is ignored); (5) `Camera` output goes to a `CameraOutput` resource.
- The GPU test (`tests/scene/scene_render_test.cpp`) renders a hand-built and an entity-built scene with the demo frame on each available backend and requires identical bytes (Vulkan here; DX12 on the Windows PC).
- 100k-entity static scene: after the first frames, 100 frames add, remove, move and flag nothing (counted by `SceneSyncReport`).
- Runs: plain `ran 2103 of 5454 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (the two known X11 window failures only); asan `ran 1586 of 5454 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`; tsan `ran 1268 of 5454 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)`, both clean. GCC and Clang non-unity clean. `ctest -L docs` 7 of 7. Guide: `docs/guide/ecs.md` "Removal tracking" and "Rendering", cross-reference in `docs/guide/renderer.md`.
- Windows run: open (Two machines item stays unchecked; see KNOWN-ISSUES).

## Post-implementation code review

`/code-review high` over the day's diff: 5 findings, 4 fixed, 1 declined.

- **Fixed:** the sync kept `last_removal_seq_` across worlds, so a new world's removals (numbered from 1) were skipped; reset on rebuild, regression test "a world with fresh removal numbers still removes" (fails without the reset).
- **Fixed:** the removal log was pruned by world tick, which `Query::Finish` advances many times a frame; now per frame (`EndFrame`). Caught by the first scene tests; covered by `tests/ecs/removal_test.cpp` and the scene destroy tests.
- **Fixed:** read-only queries took `RenderHandle` as `Write`, stamping its chunks; now `Read`.
- **Fixed:** a tracked component that is not default constructible now fails at registration with a `static_assert`.
- **Declined:** a material change re-adds the object and changes its draw order (no `SetMaterial` in the `RenderScene`; add one if a profile shows it matters), and an entity with both a mesh and a light mirrors only the mesh; both are documented in the guide.
