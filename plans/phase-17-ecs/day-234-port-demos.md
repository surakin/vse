# Phase 17 / Day 234 — Port the Demos & Remove the Hard-Coded Scene List

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Day 233.

The renderer demo and the scene tests build their worlds from entities; golden images stay bit-identical.

## Tasks

- [x] **`game/renderer_demo`** (`demo_scene.cpp`, `demo_frame.cpp`): the scene is created from entities (`Create<LocalTransform, MeshRenderer, ...>`); the per-frame animation (rotating objects, the orbiting light) becomes systems in a `Schedule`; the camera is an entity; the demo's frame loop runs the schedule and passes the synced `RenderScene` to the renderer exactly as before.
- [x] **Goldens:** the existing renderer demo goldens and `tests/rhi/renderer_demo_gpu_test.cpp` pass unchanged (same pixels, same tolerance); if the ECS changes the object order the test shows it, and the order is fixed in the sync (objects are added in entity creation order) rather than the golden re-minted.
- [x] **Other users of `RenderScene`:** `tests/renderer/*` and `tests/rhi/gbuffer_rig.h` keep using `RenderScene` directly (the renderer's own tests must not depend on `engine/scene`); `game/ui_demo`'s viewport scene is checked: if it builds a `RenderScene` it moves to entities, else it is untouched.
- [x] **Parallel culling decision (Phase 5 profile's first suggestion):** measure culling and sorting of the 100k-object case with the existing single-thread path (0.08 us per object in `renderer_bench`) against a parallel `VisibleList` system (a chunk-parallel frustum test over `WorldTransform` + bounds producing an index list, then the same sort); the numbers decide whether `GBufferPass::Prepare` gains an overload taking a precomputed visible list; if it does, the overload is added with its test, if not, the result is filed in `KNOWN-ISSUES.md`.
- [x] **The Phase 5 notes' promise checked:** `RenderScene`'s documentation says "the ECS replaces it"; this day rewrites that comment to describe the scene bridge truthfully, and `docs/guide/renderer.md` is updated.
- [x] **Tests:** the demo runs 1000 frames with `--validation` on Wayland and X11 with exit 0; frame counts and object counts per frame match the pre-port run; golden compare passes on Vulkan and (on the Windows PC at Day 236) DX12.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors asan and tsan on the demo's non-GPU parts; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md` (a complete "hello ECS" runnable sample), `docs/guide/renderer.md`.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the renderer demo builds and animates its scene from entities and systems with the goldens untouched, and the parallel-culling question is answered with numbers.

## Notes

- `DemoScene` keeps its public interface (the tests, `ui_demo` and the bench use it unchanged) and builds entities inside: static objects are entities with a `WorldTransform` (the exact placement matrix, so the pixels do not depend on a quaternion round trip) and a `MeshRenderer`; the lights carry `LocalTransform`, `WorldTransform` and `LightSource` (points also an `Orbit`); the camera is an entity. A four-system schedule runs per `Animate`: light orbits (update), transforms (post update), camera and render sync (render). The demo's mesh members became `SharedPtr<scene::MeshAsset>`; destruction drops the schedule and world first, then destroys the buffers.
- Goldens: untouched and passing (the renderer-demo, ui-demo and depth tests, on Vulkan). Objects are created in the order the scene used to add them (including the shuffled `add_order_seed` order) and lights in sun, ambient, points order, so slots are as before (`LightAt(2)` is the first point light).
- `tests/renderer` and `tests/rhi` still build `RenderScene` directly where they did; they now link `scene`/`ecs` only because they compile `demo_scene.cpp`. `game/ui_demo` shares the demo scene, nothing else changed there. `tests/rhi/text_golden_gpu_test.cpp` qualifies `renderer::settings::` (it relied on unity include order).
- 1000 frames with `--validation` on Wayland and on X11 (`--backend`): exit 0 both, 487 objects drawn in 16 draws, 297 point lights, resident memory flat (316276 to 316448 KiB, 326460 to 326628 KiB).
- **Parallel culling decision** (Release, `scene_tests "[cost]"`, 100k objects, 32-thread machine): `RenderScene::Cull` on one thread 0.573 ms (5.7 ns per object); chunk-parallel frustum test over an ECS bounds component 0.588 ms with 1 participant, 0.273 with 2, 0.134 with 4, 0.080 with 8, 0.060 with 16. Debug build: 3.1 ms single. Decision: no `GBufferPass::Prepare` overload now: the saving (about 0.5 ms of a frame whose CPU time is 7 ms at 50000 objects) is below the cost of a second path; the precomputed-visible-list overload belongs to Phase 18's visibility system (filed in KNOWN-ISSUES).
- Runs: plain `ran 2103 of 5454 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (the two known X11 window failures only); asan `ran 1586 of 5454 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`; tsan `ran 1268 of 5454 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)`, both clean. GCC and Clang non-unity clean (scene, demo, renderer, rhi, ui_demo targets). `ctest -L docs` 7 of 7.
- Windows run: open (DX12 goldens, Win32 demo; see KNOWN-ISSUES).
- The plan's "complete hello ECS sample" is the Systems and schedules sample in `docs/guide/ecs.md` (creates entities, runs a schedule, checks the output); no new sample was needed.

## Post-implementation code review

`/code-review high` over the day's diff: 3 findings, 2 fixed, 1 needing no change.

- **Fixed:** the light slot order depends on archetype creation order in the sync; now stated in a comment at the creation site (the goldens and `LightAt(2)` checks fail if it breaks).
- **Fixed:** `text_golden_gpu_test.cpp` used `settings::` unqualified, which the new includes made ambiguous in the unity batch; qualified as `renderer::settings::`.
- **No change:** the camera is built through `LookAt`, a quaternion and a matrix, so its last float bits can differ from `MakeLookAtCamera`; every golden passes at its tolerance.
