# Phase 3 / Day 61 — Triangle Milestone & Offscreen Golden-Image Tests

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Days 57-60.

The first drawn geometry — and the test infrastructure that makes every later rendering day verifiable without a human looking at a window.

## Tasks

- [x] Offscreen rendering helper `RenderTarget` (RGBA8 color texture + readback) and `ReadbackTexture(texture) → pixels`; the null device cannot render, so these are `[gpu]` tests that need no window or swapchain.
- [x] `rhi_demo` draws the triangle (GLSL shaders from Day 58) (vertex buffer uploaded through the staging path, pipeline from Day 59) in the window; a `--frames N` and `--screenshot out.ppm` option renders N frames, saves the last frame (PPM writer in the demo, offscreen path) and exits for scripted runs.
- [x] Golden-image comparison utility `tests/rhi/image_compare.{h,cpp}`: per-channel tolerance (default ±2), maximum differing-pixel fraction, and a diff-image dump to the temp dir on failure; goldens live in `tests/rhi/golden/*.ppm` (small, e.g. 64×64) and are regenerated only by an explicit `ENGINE_RHI_UPDATE_GOLDEN=1` run that the reviewer must inspect.
- [x] Test: render the triangle offscreen at 64×64 and compare with the golden; also assert specific pixels analytically (a vertex-color corner pixel is near the vertex color, the background pixel is the clear color) so a wrong golden cannot hide a broken pipeline.
- [x] Rasterization differences between vendors: document the tolerance rationale; the golden tests compare center-sampled pixels away from triangle edges with a wider edge tolerance, avoiding vendor-specific edge rules.
- [x] Run the whole suite under validation-fatal; zero messages.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** the golden-test workflow (env switches, tolerance, regeneration rule) is documented in `docs/guide/building-and-testing.md`; the hello-rhi tutorial gains the triangle step with the real code.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** a colored triangle renders in the window and offscreen; the offscreen golden test passes with tolerance and with independent analytic pixel assertions.

## Result

`tests/rhi/render_target.h` is the offscreen RGBA8 target with `Readback`; `tests/rhi/image_compare.{h,cpp}` has the tolerant comparison (interior tolerance 2, edge pixels - where the golden steps by more than 16 to a neighbour - get tolerance 64 with 25% allowed to miss), PPM read/write, the diff-image dump (`$TMPDIR/rhi_diff_<name>.ppm`) and the `ENGINE_RHI_UPDATE_GOLDEN=1` switch. `tests/rhi/vulkan_golden_test.cpp` renders the red-green-blue triangle at 64x64 (vertices through `UploadBuffer` into a `GpuOnly` buffer) and checks it three ways: the background pixel against the clear color, thirteen pixels against the barycentric blend computed independently of the GPU (interior, near each edge, outside each edge), and the whole image against `tests/rhi/golden/triangle.ppm` (inspected by eye when generated). Unit tests cover the comparison rules, a smooth ramp being held to the interior tolerance, a size mismatch, PPM round trip, a truncated file, a huge header over a tiny file, and header comments. `game/rhi_demo` now draws the triangle in the window (Wayland and X11 runs with validation exit cleanly) and takes `--frames N` and `--screenshot out.ppm` (offscreen, no window, 256x256). Guide: "Golden-image rendering tests" in `building-and-testing.md`; tutorial `hello-rhi.md` gained "Draw a triangle" with a compiled and run sample on the null device. Docs 1683/1683, 7/7 samples.

Validation-fatal run (`ENGINE_RHI_VALIDATION_FATAL=1`, whole suite): zero validation messages from any test except the six that provoke errors on purpose (validation-capture tests in `vulkan_instance_test.cpp`, plain and asan), which abort by design.

Deviations from the plan: the demo is not covered by a ctest (a machine without a GPU would fail it); it was run by hand on Wayland and X11. The sample in the tutorial runs on the null device because tagged samples link only `rhi`, `jobs` and `foundation`.

## Post-implementation code review

`/code-review high` returned 10 findings.

Fixed:
1. Edge pixels were found with the interior tolerance (2), so every pixel on the color gradient counted as an edge and a 50-level shading error would pass: edges now need a golden step above `edge_step` (16); a unit test checks that an error of 10 in a ramp fails as an interior miss.
2. The unit test read the diff image from a hardcoded `/tmp`: `DiffPath` is exposed and the test uses it.
3. `RenderTriangle` leaked the device on a failed REQUIRE and destroyed resources before idling: a `TriangleScene` guard idles first, then destroys, then destroys the device.
4. `ReadPpm` allocated from the header before checking the file's size and refused comments: it checks the remaining bytes first and skips `#` comments; tests added.
5. `Triangle::Create` in the demo could leave half-created objects: it cleans up on failure and `Destroy` tolerates missing handles.
6. The demo marked the vertex buffer ready when the barrier was recorded, not when the list was submitted: `Submitted()` is called only after a successful `Submit`.
8. The "just outside the left edge" probe was 9 pixels away: probes are now 2-4 pixels either side of each edge (four outside, three inside), checked against the analytic color.
9. STATUS.md and this section: done in the commit. 10. The tolerance claim is now true (finding 1) and the guide names the validation-capture tests instead of counting them.

Declined:
7. "The triangle setup and PPM writer are repeated in the demo, the golden test and the tutorial": each copy has a different job. The test builds its own pipeline on purpose so it checks the shaders and RHI independently of the demo; the demo cannot depend on test code, nor the tests on a game; the tutorial shows the steps in full. The vertex data is eight lines. A shared helper would belong to the Phase 4 renderer.
