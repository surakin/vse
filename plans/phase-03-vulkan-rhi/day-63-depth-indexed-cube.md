# Phase 3 / Day 63 — Depth Buffer, Indexed Draws & the Spinning Cube

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Days 61, 62.

3D: depth testing, index buffers and a transformation uniform, using the foundation math types.

## Tasks

- [ ] Depth texture creation per `DeviceCaps::depth_format`, `ResourceState::DepthAttachment`, depth clear/load ops in `BeginRendering`, depth state in the pipeline (compare op, write enable); depth texture recreated with the swapchain on resize.
- [ ] Cube geometry in the demo: 24 vertices (position, normal, uv) + 36 indices uploaded through staging; vertex layout in the pipeline.
- [ ] Per-frame uniform data (model-view-projection from `Mat4`/`Transform` of Phase 0: perspective matrix with the Vulkan clip-space conventions — Y flip and 0..1 depth — implemented as a documented `Mat4::PerspectiveVulkan`-style helper in the demo/renderer-side code, **not** in the RHI) via `UniformRing` + a bind group.
- [ ] Shaders `cube.vert.spvasm` (MVP transform, pass normal/uv/color) and `cube.frag.spvasm` (flat vertex color for now).
- [ ] Demo draws the rotating cube with depth testing; resize keeps the aspect correct.
- [ ] Tests `[gpu]` offscreen: render the cube from a fixed angle at 128×128 and compare with a golden; analytic assertions — the near face covers the center pixel with its color, a second cube drawn behind is occluded (depth test) and appears when depth test is disabled; indexed vs non-indexed draws of the same geometry produce identical images; winding/cull: back faces culled.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** depth/indexed APIs documented; the conventions (clip space, handedness, winding) are written down in `docs/guide/rhi.md` with a pointer from `docs/guide/foundation-math.md`.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a depth-tested, indexed, transformed cube renders correctly in the window and offscreen with golden and analytic checks.
