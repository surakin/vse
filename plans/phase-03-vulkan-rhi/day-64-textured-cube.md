# Phase 3 / Day 64 — Textured Cube

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 63.

Textures and samplers in a real draw.

## Tasks

- [ ] Procedural test textures generated in code (checkerboard, UV gradient) — no image file format yet (assumption #4: custom formats come with the asset pipeline); a tiny internal raw texture container `*.rtex` (header: magic, width, height, format, mip count; payload: tightly packed mips) read by the demo from `game/rhi_demo/assets/` to exercise a file → GPU path; writer is a throwaway tool function in the demo's test code.
- [ ] Texture uploaded with a full mip chain (`GenerateMips`), sampler with trilinear filtering + anisotropy; bind group (UBO + texture + sampler) from Day 60.
- [ ] Shaders `textured.vert/frag.spvasm`: sample the texture with the interpolated uv and multiply by vertex color; sRGB texture format used so the hardware converts on sample; swapchain sRGB handling documented (linear math in shaders).
- [ ] Demo shows the textured, spinning cube; `--screenshot` works.
- [ ] Tests `[gpu]` offscreen: golden image of the textured cube; analytic checks — with a 2×2 texture of four distinct colors and nearest filtering a face's four quadrants have those colors; sRGB vs UNORM texture of the same data produce the expected brightness difference at a mid-gray; mip selection: a minified checkerboard (small render target) averages to ≈ mid-gray with mips and does not without (proves mips are bound).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** texture usage in draws documented; hello-rhi tutorial gains the texturing step (tagged samples where compilable).
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a textured cube renders correctly with mips, anisotropy and sRGB handling, verified by golden and analytic tests.
