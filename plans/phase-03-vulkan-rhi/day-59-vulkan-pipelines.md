# Phase 3 / Day 59 — Graphics Pipelines, Layouts & Pipeline Cache

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 58.

Pipeline state objects with dynamic rendering.

## Tasks

- [ ] `CreateGraphicsPipeline(GraphicsPipelineDesc)`: `VkPipelineLayout` from the bind group layouts + push constant range, vertex input state from the RHI vertex layout, input assembly, rasterization (cull/front face/polygon fill/depth bias), multisample (1x only now; field present), depth/stencil, color blend per attachment, viewport/scissor as dynamic state, `VkPipelineRenderingCreateInfo` from the attachment formats.
- [ ] Pipeline layout cache keyed on (layouts, push range) so equal layouts share one object.
- [ ] `VkPipelineCache`: created at device start, loaded from `~/.cache/<engine>/pipeline_cache_<vendor>_<device>_<uuid>.bin` if its header (vendor/device/UUID, version) matches, saved at shutdown atomically (write temp + rename); a corrupt or mismatched file is ignored with a log line, never an error. Path override via `ENGINE_RHI_CACHE_DIR`; the cache is disabled in tests unless a temp dir is given.
- [ ] Failure policy: pipeline creation failure (bad SPIR-V interface mismatch, unsupported format) returns the invalid handle with the driver/validation message logged; the null device mirrors the descriptor-level rejections.
- [ ] `SetPipeline`, `SetViewport`, `SetScissor`, `SetVertexBuffer`, `SetIndexBuffer`, `Draw`, `DrawIndexed`, `BeginRendering/EndRendering` (color + optional depth attachments, load/store ops, clear values) implemented on `VulkanCommandList`.
- [ ] Tests `[gpu]`: create the triangle pipeline for the swapchain format and an offscreen RGBA8 format; reject-path tests (missing shader, attachment format with no support, stage mismatch); pipeline cache file written and reused (second device creation reports cache hits via the creation-feedback counter or measurable pipeline-create count), and a corrupted cache file is ignored.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** pipeline APIs, `GraphicsPipelineDesc` fields and the cache env switches documented; guide section "Pipelines".
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a graphics pipeline compiles from the descriptor with dynamic rendering, layouts are shared, and the on-disk pipeline cache round-trips and tolerates corruption.
