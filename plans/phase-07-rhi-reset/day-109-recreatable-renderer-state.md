# Phase 7 / Day 109 — Recreatable Materials, Passes, Frame Data & Transient Pool

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Day 108.

The owners with dependencies: materials (bind groups, pipelines, default textures), the passes, the per-frame buffers and
the transient pool.

## Tasks

- [ ] `MaterialSystem` registers at tier 3 and implements `Release`/`Recreate`: templates keep their descriptors and reflection (CPU), materials keep parameter bytes and texture/sampler bindings **by reference to `GpuTexture`/sampler owners** (not raw handles; a material given a raw handle is converted at the API edge or the API takes the owner), default textures and the default sampler are re-made, layouts, bind groups and the pipeline cache are rebuilt on demand; `MaterialStats` counters survive and a `rebuilds` counter is added.
- [ ] Pipelines are rebuilt lazily after a reset (first use) or eagerly (a `Prewarm` option decided by the spec and the Day 113 measurement); both paths tested; the pipeline-cache file makes the second path fast.
- [ ] `GBufferPass`, `GBufferDebugView`, `LightingPass`, `TonemapPass`: each registers at tier 4; shaders come from the embedded SPIR-V (CPU), layouts, pipelines and bind groups are rebuilt; `TonemapPass` and the pipeline target follow the swapchain format (`OnSwapchainChanged` from Day 107).
- [ ] `FrameData` (frame, object and light buffers, the uniform ring) and `RenderScene` GPU mirrors: buffers re-created at their current capacities and marked fully dirty so the next frame re-uploads everything; the scene itself (objects, lights, transforms) is CPU data and untouched. Audit: no other cached `Buffer`/`BindGroup` handles in `engine/renderer` (grep list in the result).
- [ ] `TransientPool`: `Release` destroys every pooled texture and buffer and clears the free lists; render-graph resources are per frame so nothing else holds pool handles; pool statistics reset, and a `resets` counter is added. `RenderGraph` per-frame state is rebuilt by the next `RenderFrame`.
- [ ] `Renderer` keeps no device handle across frames other than through these owners (audit); `Renderer::Rebind(Device*, Swapchain*)` sets the new pointers after a reset.
- [ ] Tests (null backend unless stated): material parameters, texture bindings and the default-slot rule are identical after a reset; a material using a `GpuTexture` follows the texture's new handle; pipelines rebuilt lazily and eagerly; each pass produces the same recorded command stream before and after a reset (compare command-list hashes or counts on the null device); `FrameData` re-uploads everything once; `TransientPool` has no live handle after `Release` and works after; ASan/TSan. GPU test: `renderer_demo`'s offscreen frame before and after one reset is pixel-equal (same adapter, golden tolerance at zero because it is the same GPU and driver).
- [ ] **Documentation:** labels on the changed headers; `renderer.md` extended with what each owner does at a reset and the `Prewarm` choice; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` plus asan and tsan; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`, extra scrutiny on handles cached across frames and on the dirty-flag logic; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** every device-dependent object in `engine/renderer` and the demos is registered and rebuilds in tier order; a manual reset with the registry of Day 108 on the null and Vulkan backends leaves materials, passes, buffers and the pool working, and an offscreen frame is pixel-equal before and after.
