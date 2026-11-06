# Phase 4 / Day 59 — Graphics Pipelines, Layouts & Pipeline Cache

**Phase:** [Phase 4 — Vulkan RHI](../phase-04-vulkan-rhi.md)
**Depends on:** Day 58.

Pipeline state objects with dynamic rendering.

## Tasks

- [x] `CreateGraphicsPipeline(GraphicsPipelineDesc)`: `VkPipelineLayout` from the bind group layouts + push constant range, vertex input state from the RHI vertex layout, input assembly, rasterization (cull/front face/polygon fill/depth bias), multisample (1x only now; field present), depth/stencil, color blend per attachment, viewport/scissor as dynamic state, `VkPipelineRenderingCreateInfo` from the attachment formats.
- [x] Pipeline layout cache keyed on (layouts, push range) so equal layouts share one object.
- [x] `VkPipelineCache`: created at device start, loaded from `~/.cache/<engine>/pipeline_cache_<vendor>_<device>_<uuid>.bin` if its header (vendor/device/UUID, version) matches, saved at shutdown atomically (write temp + rename); a corrupt or mismatched file is ignored with a log line, never an error. Path override via `ENGINE_RHI_CACHE_DIR`; the cache is disabled in tests unless a temp dir is given.
- [x] Failure policy: pipeline creation failure (bad SPIR-V interface mismatch, unsupported format) returns the invalid handle with the driver/validation message logged; the null device mirrors the descriptor-level rejections.
- [x] `SetPipeline`, `SetViewport`, `SetScissor`, `SetVertexBuffer`, `SetIndexBuffer`, `Draw`, `DrawIndexed`, `BeginRendering/EndRendering` (color + optional depth attachments, load/store ops, clear values) implemented on `VulkanCommandList`.
- [x] Tests `[gpu]`: create the triangle pipeline for the swapchain format and an offscreen RGBA8 format; reject-path tests (missing shader, attachment format with no support, stage mismatch); pipeline cache file written and reused (second device creation reports cache hits via the creation-feedback counter or measurable pipeline-create count), and a corrupted cache file is ignored.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** pipeline APIs, `GraphicsPipelineDesc` fields and the cache env switches documented; guide section "Pipelines".
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a graphics pipeline compiles from the descriptor with dynamic rendering, layouts are shared, and the on-disk pipeline cache round-trips and tolerates corruption.

## Result

`VulkanDevice::CreateGraphicsPipeline` builds a `VkPipeline` from a `GraphicsPipelineDesc` with dynamic rendering (`VkPipelineRenderingCreateInfo` from the attachment formats), dynamic viewport and scissor, vertex input, rasterization, 1x multisample, depth/stencil and per-attachment blend. Creation is refused, with a logged reason and an invalid handle, for a missing, destroyed or wrong-stage shader, a stale bind group layout, a device limit, a color or depth format the device cannot render to, and wireframe without `fillModeNonSolid`. `CreateBindGroupLayout` builds `VkDescriptorSetLayout` objects shared by content (entries sorted by binding index first); pipeline layouts are shared by (set layout ids, push range) and reference counted by the pipelines using them. The on-disk pipeline cache (`vk_pipeline_policy.{h,cpp}` for the pure rules, `vulkan_device_pipelines.cpp` for the I/O): `DeviceCreateInfo::pipeline_cache` / `pipeline_cache_dir`, `ENGINE_RHI_CACHE_DIR`, a header check against vendor/device/UUID, atomic save (per-process temp file then rename) at shutdown or through `SavePipelineCache()`, and any unusable file ignored with a log line. `VulkanCommandList` records `SetPipeline` (checks the attachment formats against the pass), `SetViewport`, `SetScissor`, `SetVertexBuffer`, `SetIndexBuffer`, `Draw` and `DrawIndexed`. Tests (`tests/rhi/vulkan_pipeline_test.cpp`): the enum conversions and cache policy (no GPU); the triangle pipeline for BGRA8_sRGB, RGBA8 and RGBA16F; every reject path; layout sharing and release (including 640 distinct layouts over time); a real triangle drawn into an offscreen target and read back; the cache written, reused, truncated, wrong-vendor, garbage, empty, a directory, disabled, and saved mid-session. Docs 1652/1652; guide section "Pipelines".

Deviations from the plan: `CreateBindGroupLayout` is implemented here rather than on Day 60, because a pipeline layout is made from the descriptor set layouts; Day 60 adds bind groups, descriptor pools and push constants. `BeginRendering/EndRendering` were already done on Day 57. The cache is measured by the bytes handed to the driver at start (`loaded_bytes`), not by a creation-feedback counter. The test runner sets `ENGINE_RHI_CACHE_DIR` into the build tree, so tests that pass no directory do not write to `~/.cache`; running a test binary by hand does.

## Post-implementation code review

`/code-review high` returned 9 findings.

Fixed:
1. Pipeline layouts were never released and the cache was a fixed 512 entries: they are reference counted by pipelines and freed when the last one is collected; a 640-layout churn test covers it.
2. The cache file used a fixed `.tmp` name, so two processes saving together could interleave: the temp name carries the process id.
3. The cache was saved only in the destructor: `SavePipelineCache()` saves on demand (the destructor uses it).
6. Layouts with the same bindings in a different order were distinct: entries are sorted by binding before comparing; test added.
7. `ToVkCompare` was a forwarder: removed.
8. `ReadFile` could stall on a FIFO or allocate for a bogus size: it requires a regular file and uses the `stat` size; a directory test covers it.
9. STATUS.md and this section: done in the commit.

Declined:
4. "`SetViewport` rejects a negative height (the Vulkan Y-flip)": the RHI contract (documented on `CommandList::SetViewport` and enforced by the null backend) is a positive size on every backend; flipping Y is a renderer decision for Phase 5, which would change the contract for all backends at once.
5. "Lookups take the resource mutex per bind, and some command-list state is unused": the unused fields (layout ids, push range) are what `SetBindGroup` and `SetPushConstants` read on Day 60, in the next commit; the lock cost is noted for the Phase 5 renderer, which will batch binds. Body validation of the cache file is left to the driver, which checks its own data against the header UUID.
(The duplicated `PipelineRes`/`PipelineInfo` structs stay: one is internal, one is the public snapshot.)
