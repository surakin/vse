# Phase 3 / Day 61 — Triangle Milestone & Offscreen Golden-Image Tests

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Days 57-60.

The first drawn geometry — and the test infrastructure that makes every later rendering day verifiable without a human looking at a window.

## Tasks

- [ ] Offscreen rendering helper `RenderTarget` (RGBA8 color texture + readback) and `ReadbackTexture(texture) → pixels`; the null device cannot render, so these are `[gpu]` tests that need no window or swapchain.
- [ ] `rhi_demo` draws the triangle (GLSL shaders from Day 58) (vertex buffer uploaded through the staging path, pipeline from Day 59) in the window; a `--frames N` and `--screenshot out.ppm` option renders N frames, saves the last frame (PPM writer in the demo, offscreen path) and exits for scripted runs.
- [ ] Golden-image comparison utility `tests/rhi/image_compare.{h,cpp}`: per-channel tolerance (default ±2), maximum differing-pixel fraction, and a diff-image dump to the temp dir on failure; goldens live in `tests/rhi/golden/*.ppm` (small, e.g. 64×64) and are regenerated only by an explicit `ENGINE_RHI_UPDATE_GOLDEN=1` run that the reviewer must inspect.
- [ ] Test: render the triangle offscreen at 64×64 and compare with the golden; also assert specific pixels analytically (a vertex-color corner pixel is near the vertex color, the background pixel is the clear color) so a wrong golden cannot hide a broken pipeline.
- [ ] Rasterization differences between vendors: document the tolerance rationale; the golden tests compare center-sampled pixels away from triangle edges with a wider edge tolerance, avoiding vendor-specific edge rules.
- [ ] Run the whole suite under validation-fatal; zero messages.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the golden-test workflow (env switches, tolerance, regeneration rule) is documented in `docs/guide/building-and-testing.md`; the hello-rhi tutorial gains the triangle step with the real code.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a colored triangle renders in the window and offscreen; the offscreen golden test passes with tolerance and with independent analytic pixel assertions.
