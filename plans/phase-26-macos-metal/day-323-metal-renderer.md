# Phase 26 / Day 323 — Metal Renderer: the Phase 5 Suite

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 322.

The Phase 5 scene tests through Metal: the render graph, materials and permutations, the deferred G-buffer and lighting, and `renderer_demo.ppm`.
Modelled on DX12 Day 171.

## Tasks

- [ ] **Run the suites** (deferred to Day 325: they run once per GPU backend, Metal on macOS, now with Metal validation) with a Metal factory: `render_graph_gpu_test`, `gbuffer_gpu_test`, `lighting_gpu_test`, `renderer_demo_gpu_test`, the material
      and permutation tests, `reset_golden_gpu_test`; record differences as on Day 322.
- [x] **Graph on Metal:** the graph's barriers become what Day 320 decided; passes that write and then sample a G-buffer target land in separate render
      encoders (checked by a test counting encoders per frame); the HDR target `RGBA16Float`.
- [ ] **Depth sampling:** (nothing to add: Metal samples `Depth32Float` as itself, no typed view; checked by the lighting suite in the Day 325 run) the depth shim the renderer uses for D3D12 (typed view of a depth texture) has its Metal counterpart; the lighting pass samples
      `Depth32Float` correctly.
- [ ] **`renderer_demo` on Metal:** (the ctests are set up; they run, with the F1-F10 keys by hand, in the Day 325 run) runs its script, writes `renderer_demo.ppm`, matches the golden within tolerance; F1-F10 live keys checked by hand.
- [ ] **Baselines:** (the Day 325 run) `rhi_bench` and `renderer_bench` on Metal recorded beside the other machines' (no target; the integrated GPU and the Retina size are
      named).
- [x] **Documentation:** `renderer.md` names Metal where it lists backends; `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with GPU tests (quote). **Deferred to Day 325 (the user's choice; the user is asked first).**
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: handle lifetime across frames in parallel recording, the encoder-count test's strength.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the Phase 5 scene suites and `renderer_demo.ppm` pass through Metal with validation silent in three flavors, and the benchmark baselines are
recorded.

## Implementation notes

- **The renderer needs nothing of its own for Metal:**
  - it names no backend;
  - the minted libraries carry their slots;
  - each pass is a render encoder, so a pass sampling an earlier one's target is ordered by the encoder boundary;
  - a depth texture is sampled as `depth32_float` itself.
  Its GPU suites run once per GPU backend (`backend_fixture.h`), so on macOS through Metal.
- **Validation on for every Metal run:**
  - `renderer_tests` on macOS now sets `MTL_DEBUG_LAYER=1`, `MTL_DEBUG_LAYER_ERROR_MODE=assert` and `MTL_SHADER_VALIDATION=1`, as `rhi_tests`
    does;
  - so does every demo ctest (`DEMO_PROFILE_ENV` on macOS);
  - the renderer's `[gpu]` tests also run under TSan on macOS.
  Metal stops the process at the first error, so `BackendValidation` needs no Metal count.
- **`renderer_demo` on Metal:**
  - the macOS scripted run and the overlay run now use `--rhi metal` (they were on null from Day 314);
  - three named Metal ctests mirror D3D12's: the screenshot with validation, the loss trace ("the Metal device was lost") and the staging
    ring's out-of-memory (`testopt` lines in `tests/phases.txt`, phase 26).
- **Tests added** (run on Day 325): `tests/rhi/macos/metal_graph_test.mm` runs a graph with a clear pass into an `rgba16_float` target and a
  pass that samples it: two render encoders, no memory barrier, the right pixels.
- **Docs:** `docs/guide/renderer.md` (platform notes name Metal); `backend_fixture.h`'s note on Metal validation.
- **Built:** both unity modes on macOS, every flavor, no warning. **Runs: deferred to Day 325.**

## Post-implementation code review

`/code-review high` on the day's diff: 8 findings, 5 fixed, 1 in part, 2 declined.

1. **The new demo tests ran Metal without validation** (`--validation` is advisory on Metal). Fixed: the validation variables are in every demo
   ctest's environment on macOS.
2. **The renderer suites ran through Metal without validation.** Fixed: `renderer_tests` sets the variables on macOS; `BackendValidation`
   documents that Metal stops the process instead of counting.
3. **The encoder-count checks follow from the test's own passes.** Fixed in part: the target is the HDR format, and the checks stay as a
   statement of the property. A count over the real deferred frame waits for the Day 325 run, where the renderer's own frame runs on Metal.
4. **`skipped_barriers >= 1` is loose.** Declined for now: the exact count is measured in the Day 325 run, and tightened then.
5. **No null check on `CreateCommandList`.** Fixed.
6. **`Create` did not check the shaders and the layout.** Fixed.
7. **The scene repeats `renderer_test.cpp`'s.** Declined: about 80 lines in a macOS-only file; to share when a third copy appears.
8. **A stale comment in the demo's CMake file.** Fixed.
