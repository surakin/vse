# Phase 4 / Day 62 — Compute Pipelines & Dispatch

**Phase:** [Phase 4 — Vulkan RHI](../phase-04-vulkan-rhi.md)
**Depends on:** Day 60.

Compute is part of the RHI contract (later phases: culling, skinning, audio/physics offload) and gives a window-free way to test the binding model end to end.

## Tasks

- [x] `CreateComputePipeline(ComputePipelineDesc)`, `Dispatch(x, y, z)` implemented; compute queue use stays on the graphics+compute family for now (async compute is out of scope; noted for Phase 5).
- [x] Storage buffer / storage texture binding types exercised; `ResourceState::ShaderWrite` ↔ `ShaderRead` barriers for compute→graphics and compute→compute hazards using the Day 54 table.
- [x] Write two GLSL compute shaders: `fill.comp` (writes `gl_GlobalInvocationID.x * k` into a storage buffer) and `saxpy.comp` (storage buffer in/out with a push-constant scalar); compile/validate/embed via the Day 58 workflow.
- [x] Tests `[gpu]` (headless): dispatch `fill` over 1M elements, read back, verify every element; dispatch `saxpy` reading the previous result (needs the barrier) and verify; a missing barrier variant is checked only by the validation layer's sync-validation (the test asserts the *correct* version is clean); dispatch sizes beyond `maxComputeWorkGroupCount` are rejected by the RHI with a message.
- [x] Null device: dispatch legality (pipeline bound, bind groups complete) validated.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** compute APIs documented; guide section "Compute" with the barrier rules.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** compute dispatches produce verified results with correct barriers, headless, validation- and sync-validation-clean.

## Result

`VulkanDevice::CreateComputePipeline` (same layout sharing, pipeline cache and handle rules as graphics pipelines) and `VulkanCommandList::Dispatch` (outside a pass, compute pipeline bound, counts within `max_compute_work_group_count`, bind groups flushed with the compute bind point) are implemented. GLSL compute shaders `fill.comp` (`out[i] = i * k`), `saxpy.comp` (`y[i] = a * x[i] + y[i]` on unsigned integers, so results are exact) and `gradient.comp` (storage image) are compiled and embedded through the Day 58 workflow. Tests (`tests/rhi/vulkan_compute_test.cpp`, validation and sync validation on): fill and saxpy over 1M elements verified element by element, with the `ShaderWrite -> ShaderRead` barrier between them; a storage texture written by a dispatch and read back after `ShaderWrite -> CopySrc`; pipeline creation refused for no shader, a vertex-stage shader, a bad push size and a destroyed layout. The null device already validated dispatch legality (no pipeline, inside a pass, over the limit, resource states) in `null_device_test.cpp`. Guide section "Compute" with the barrier table. No `docs/reference/` change was needed beyond the `Dispatch` doc wording (regenerated); docs 1683/1683.

Deviations: the over-limit, no-pipeline and in-pass dispatch cases are `ENGINE_ASSERT` aborts, which cannot be exercised on the Vulkan backend without killing the test; they are covered on the null device, which shares the rules. The "missing barrier" variant is not asserted (sync validation reports it; the tests assert the correct version is clean).

## Post-implementation code review

`/code-review high` returned 8 findings.

Fixed: (1) a vertex-stage shader passed to `CreateComputePipeline` is now tested; (2) `Dispatch`'s doc now says Vulkan cannot check resource states (the null backend does); (3) the guide sample rounds the workgroup count up; (5) the storage-image test allows one level of unorm conversion difference; (6) the storage-texture test records a `ShaderWrite -> CopySrc` barrier and reads back in `CopySrc`; (7) log silencing uses an RAII guard; (8) day file and `STATUS.md` updated.

Declined: (1, rest) the Vulkan over-limit / no-pipeline / in-pass dispatch cases: asserts abort the process, covered on the null device (above). (4) Sharing code between the graphics and compute creation paths: each is about 25 lines with different log labels and failure texts; a shared helper would need the label passed through and was judged not worth it for two callers. (6, rest) A compute-to-graphics hazard test: it needs a draw that reads compute output, which is exactly Day 65's lit-cube pipeline; the barrier table itself is covered since Day 54.
