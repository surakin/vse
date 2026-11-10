# Phase 26 / Day 290 — Metal Golden Images: the Phase 4 Suite

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 289.

The Phase 4 golden images (triangle, compute, cube, textured cube, lit cube) through Metal against the existing images and tolerances (decision 11).

## Tasks

- [ ] **Run the suite** (`tests/rhi/golden_test.cpp`, `cube_test`, `textured_cube_test`, `lit_cube_test`, `compute_test`) with a Metal factory and
      validation on; record each image's difference numbers (max, mean, pixels over tolerance).
- [ ] **Investigate every miss** before touching a tolerance: Y flip, winding after the flip, sRGB conversion on the render target, texture origin,
      sampler addressing, depth range, mip filter. A real difference (rasterization rules of this GPU) changes a tolerance only with the measured numbers
      recorded here and in the test's comment; images are not re-blessed.
- [ ] **Screenshots:** the readback path for a presented image (with `framebufferOnly` off) matches the offscreen render.
- [ ] **Per-object index:** the lit-cube and instanced tests prove `first_instance = i` reaches the shader through the stream.
- [ ] **Validation silent:** the whole suite with `MTL_DEBUG_LAYER_ERROR_MODE=assert` and shader validation; record the run time with and without
      validation.
- [ ] **Documentation:** `rhi.md` notes any tolerance change; `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with GPU tests (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: any tolerance change and the fix that made an image pass (no fix that only works on this
      GPU).
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the five Phase 4 golden suites pass through Metal on the Iris Plus 645 with validation silent in three flavors, and every difference and any
tolerance change is recorded with its numbers.
