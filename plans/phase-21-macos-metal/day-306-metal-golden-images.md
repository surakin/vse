# Phase 21 / Day 306 — Metal Golden Images: the Phase 4 Suite

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 305.

The Phase 4 golden images (triangle, compute, cube, textured cube, lit cube) through Metal against the existing images and tolerances (decision 11).

## Tasks

- [x] **Run the suite** (Day 309: every golden passes through Metal with validation; the numbers are in the Day 309 file) ([the Day 309 run](day-309-metal-lifecycle.md#the-run-of-days-315-325)). Originally: (deferred to Day 309, with the numbers: the suites already run once per backend, and Metal is one of them on macOS) (`tests/rhi/golden_test.cpp`, `cube_test`, `textured_cube_test`, `lit_cube_test`, `compute_test`) with a Metal factory and
      validation on; record each image's difference numbers (max, mean, pixels over tolerance).
- [x] **Investigate every miss** (Day 309: no miss; no tolerance changed) ([the Day 309 run](day-309-metal-lifecycle.md#the-run-of-days-315-325)). Originally: (with the Day 309 run) before touching a tolerance: Y flip, winding after the flip, sRGB conversion on the render target, texture origin,
      sampler addressing, depth range, mip filter. A real difference (rasterization rules of this GPU) changes a tolerance only with the measured numbers
      recorded here and in the test's comment; images are not re-blessed.
- [x] **Screenshots:** the readback path for a presented image (with `framebufferOnly` off) matches the offscreen render.
- [x] **Per-object index:** (Day 309: the lit-cube and material tests pass through Metal) ([the Day 309 run](day-309-metal-lifecycle.md#the-run-of-days-315-325)). Originally: (proved by the lit-cube and instanced tests in the Day 309 run) the lit-cube and instanced tests prove `first_instance = i` reaches the shader through the stream.
- [x] **Validation silent:** (Day 309: every Metal ctest ran with API and shader validation in assert mode; the rhi GPU suite 16 s with, 15 s without) ([the Day 309 run](day-309-metal-lifecycle.md#the-run-of-days-315-325)). Originally: (the Day 309 run: every RHI ctest has the validation environment; the times with and without it are measured then) the whole suite with `MTL_DEBUG_LAYER_ERROR_MODE=assert` and shader validation; record the run time with and without
      validation.
- [x] **Documentation:** `rhi.md` notes any tolerance change; `docgen lint` zero.
- [x] **Runs:** `ran 6110 of 6490 tests (phases: all; flavors: all; display: included; crash tests: excluded) in 1479 s` ([the Day 309 run](day-309-metal-lifecycle.md#the-run-of-days-315-325)). Originally: `scripts/test-phase.sh --affected --flavor all` with GPU tests (quote). **Deferred to Day 309 (the user's choice; the user is asked first).**
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: any tolerance change and the fix that made an image pass (no fix that only works on this
      GPU).
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the five Phase 4 golden suites pass through Metal on the Iris Plus 645 with validation silent in three flavors, and every difference and any
tolerance change is recorded with its numbers.

## Implementation notes

- **The suites need no change to run on Metal.** `golden_test`, `cube_test`, `textured_cube_test`, `lit_cube_test`, `compute_test` (and the renderer's
  GPU tests) run once per GPU backend the program registered and can use (`backend_fixture.h`, `FOR_EACH_GPU_BACKEND`), and on macOS that is Metal,
  through the RHI only and the minted blob sets. The Vulkan-only tests (`gpu_fixture.h`) skip on macOS even under `ENGINE_RHI_REQUIRE_GPU=1`.
  `golden_test`'s census checks Metal is there on macOS when a GPU is required. No tolerance was touched; the differences are measured on Day 309.
- **Screenshots:** a swapchain image reads back byte for byte what the same drawing gives offscreen (`metal_swapchain_test`). On Metal it can be
  read only until `Present`, when the system takes the drawable back: holding the drawable's texture for later would keep the drawable and starve
  the layer, so the contract says so (`rhi/swapchain.h`, `Device::ReadbackTexture`), and a later read fails with a log line.
- **Docs:** the two contracts and the reference regenerated.
- **Built:** both unity modes on macOS, every flavor, no warning. **Runs: deferred to Day 309.**

## Post-implementation code review

`/code-review high` on the day's diff: 8 findings, 7 fixed, 1 declined.

1. **The census failed a Mac without Metal 3 instead of skipping.** Fixed: checked only when a GPU is required.
2. **Under `ENGINE_RHI_REQUIRE_GPU=1` the Vulkan-only tests failed on macOS** (with a wrong message). Fixed: they skip.
3. **`ReadbackTexture`'s new failure was not in its contract.** Fixed (`Fails:`).
4. **The swapchain contract said images stay valid until the next `Acquire`.** Fixed: on Metal until `Present` (the drawable cannot be kept).
5. **The screenshot test compared one clear color.** Fixed: an oriented drawing, byte-compared with the same drawing offscreen.
6. **The test skipped the contract's barriers.** Fixed.
7. **The test accepted `Lost` from `Acquire`.** Fixed: `Ok` or `Recreated`.
8. **A readback could race `Present` from another thread.** Declined: a swapchain belongs to one thread (`rhi::Swapchain`'s contract), and its
   image is read on that thread before it is presented.
