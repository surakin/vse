# Phase 7 / Day 109 — Recreatable Materials, Passes, Frame Data & Transient Pool

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Day 108.

The owners with dependencies: materials (bind groups, pipelines, default textures), the passes, the per-frame buffers and
the transient pool.

## Tasks

- [x] `MaterialSystem` registers at tier 3 and implements `Release`/`Recreate`: templates keep their descriptors and reflection (CPU), materials keep parameter bytes and texture/sampler bindings **by reference to `GpuTexture`/sampler owners** (not raw handles; a material given a raw handle is converted at the API edge or the API takes the owner), default textures and the default sampler are re-made, layouts, bind groups and the pipeline cache are rebuilt on demand; `MaterialStats` counters survive and a `rebuilds` counter is added.
- [x] Pipelines are rebuilt lazily after a reset (first use) or eagerly (a `Prewarm` option decided by the spec and the Day 113 measurement); both paths tested; the pipeline-cache file makes the second path fast.
- [x] `GBufferPass`, `GBufferDebugView`, `LightingPass`, `TonemapPass`: each registers at tier 4; shaders come from the embedded SPIR-V (CPU), layouts, pipelines and bind groups are rebuilt; `TonemapPass` and the pipeline target follow the swapchain format (`OnSwapchainChanged` from Day 107).
- [x] `FrameData` (frame, object and light buffers, the uniform ring) and `RenderScene` GPU mirrors: buffers re-created at their current capacities and marked fully dirty so the next frame re-uploads everything; the scene itself (objects, lights, transforms) is CPU data and untouched. Audit: no other cached `Buffer`/`BindGroup` handles in `engine/renderer` (grep list in the result).
- [x] `TransientPool`: `Release` destroys every pooled texture and buffer and clears the free lists; render-graph resources are per frame so nothing else holds pool handles; pool statistics reset, and a `resets` counter is added. `RenderGraph` per-frame state is rebuilt by the next `RenderFrame`.
- [x] `Renderer` keeps no device handle across frames other than through these owners (audit); `Renderer::Rebind(Device*, Swapchain*)` sets the new pointers after a reset.
- [x] Tests (null backend unless stated): material parameters, texture bindings and the default-slot rule are identical after a reset; a material using a `GpuTexture` follows the texture's new handle; pipelines rebuilt lazily and eagerly; each pass produces the same recorded command stream before and after a reset (compare command-list hashes or counts on the null device); `FrameData` re-uploads everything once; `TransientPool` has no live handle after `Release` and works after; ASan/TSan. GPU test: `renderer_demo`'s offscreen frame before and after one reset is pixel-equal (same adapter, golden tolerance at zero because it is the same GPU and driver).
- [x] **Documentation:** labels on the changed headers; `renderer.md` extended with what each owner does at a reset and the `Prewarm` choice; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` plus asan and tsan; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`, extra scrutiny on handles cached across frames and on the dirty-flag logic; record it.
- [x] Update `STATUS.md`; commit.

**Done:** every device-dependent object in `engine/renderer` and the demos is registered and rebuilds in tier order; a manual reset with the registry of Day 108 on the null and Vulkan backends leaves materials, passes, buffers and the pool working, and an offscreen frame is pixel-equal before and after.

## Result

`MaterialSystem` is a `Recreatable` (tier 3): `Release`/`Recreate` rebuild defaults, layouts, template shaders and variants (same indices), parameter buffers, bind groups and pipelines (lazily, or all with `MaterialSystemDesc::prewarm_pipelines`); materials bind textures and samplers by owner (`SetTexture`/`SetSampler` with `GpuTexture*`/`GpuSampler*`) and follow the owner's new handle at `FlushMaterials`; `MaterialStats::rebuilds` added. `GBufferDebugView`, `LightingPass`, `TonemapPass`, `FrameResources` and `TransientPool` (new `resets` counter) are `Recreatable` at tier 4 (`RegisterWith`); `GBufferPass` holds only per-frame lists and needs nothing. `Renderer::Rebind` and `RendererDesc::resources`; `DemoFrame`/`DemoScene` register everything and bind textures by owner. Audit of cached device handles in `engine/renderer`: the only ones were in these classes (grep for `rhi::.*Handle` members: material, gbuffer, lighting, frame_data, transient_pool, mesh, texture); `RenderObject` holds `const GpuMesh*`, `Renderer` holds only the device pointer, now rebound. Tests: `tests/renderer/recreatable_renderer_test.cpp` (6 cases: same draw/light stats after a reset, material parameters byte-identical and bindings following the owner, default-slot rule, lazy versus eager pipelines, pool and counts, five resets in a row, and a real-device offscreen frame pixel-identical before and after).
Deviation: "each pass produces the same recorded command stream" is checked through draw and light statistics and the unchanged object counts, not by hashing the null device's command lists (it has no such hook). The GPU pixel test uses the same adapter and compares bytes exactly.
Scoped runs: plain `ran 986 of 2610 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed), asan `ran 713 of 2610 ... flavors: asan`, tsan `ran 439 of 2610 ... flavors: tsan`; docs 4/4.

## Post-implementation code review

11 findings. Fixed: a failed variant rebuild no longer truncates the variant table; a prewarm failure is logged and left to lazy rebuild; `FlushMaterials` aborts on a released system; `Renderer::Rebind` requires a registry (documented and asserted; the false "frame running" label removed); garbled doc comments; misplaced `Rebind` doc in the demo scene; unchecked `RegisterWith` results now fail `DemoFrame::Create` or log; `DemoFrame::Rebind` null check; a demo-scene stale-pointer test found a real bug during writing (the scene kept a dangling device pointer; now `DemoScene::Rebind`). Declined: unbinding the scene's texture owners in `DemoScene::Destroy` (the frame is always declared before the scene in every caller; documented in the header).
