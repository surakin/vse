# Phase 17 / Day 234 — Port the Demos & Remove the Hard-Coded Scene List

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Day 233.

The renderer demo and the scene tests build their worlds from entities; golden images stay bit-identical.

## Tasks

- [ ] **`game/renderer_demo`** (`demo_scene.cpp`, `demo_frame.cpp`): the scene is created from entities (`Create<LocalTransform, MeshRenderer, ...>`); the per-frame animation (rotating objects, the orbiting light) becomes systems in a `Schedule`; the camera is an entity; the demo's frame loop runs the schedule and passes the synced `RenderScene` to the renderer exactly as before.
- [ ] **Goldens:** the existing renderer demo goldens and `tests/rhi/renderer_demo_gpu_test.cpp` pass unchanged (same pixels, same tolerance); if the ECS changes the object order the test shows it, and the order is fixed in the sync (objects are added in entity creation order) rather than the golden re-minted.
- [ ] **Other users of `RenderScene`:** `tests/renderer/*` and `tests/rhi/gbuffer_rig.h` keep using `RenderScene` directly (the renderer's own tests must not depend on `engine/scene`); `game/ui_demo`'s viewport scene is checked: if it builds a `RenderScene` it moves to entities, else it is untouched.
- [ ] **Parallel culling decision (Phase 5 profile's first suggestion):** measure culling and sorting of the 100k-object case with the existing single-thread path (0.08 us per object in `renderer_bench`) against a parallel `VisibleList` system (a chunk-parallel frustum test over `WorldTransform` + bounds producing an index list, then the same sort); the numbers decide whether `GBufferPass::Prepare` gains an overload taking a precomputed visible list; if it does, the overload is added with its test, if not, the result is filed in `KNOWN-ISSUES.md`.
- [ ] **The Phase 5 notes' promise checked:** `RenderScene`'s documentation says "the ECS replaces it"; this day rewrites that comment to describe the scene bridge truthfully, and `docs/guide/renderer.md` is updated.
- [ ] **Tests:** the demo runs 1000 frames with `--validation` on Wayland and X11 with exit 0; frame counts and object counts per frame match the pre-port run; golden compare passes on Vulkan and (on the Windows PC at Day 236) DX12.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors asan and tsan on the demo's non-GPU parts; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md` (a complete "hello ECS" runnable sample), `docs/guide/renderer.md`.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the renderer demo builds and animates its scene from entities and systems with the goldens untouched, and the parallel-culling question is answered with numbers.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
