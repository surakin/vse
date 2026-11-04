# Phase 3 / Day 62 — Compute Pipelines & Dispatch

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 60.

Compute is part of the RHI contract (later phases: culling, skinning, audio/physics offload) and gives a window-free way to test the binding model end to end.

## Tasks

- [ ] `CreateComputePipeline(ComputePipelineDesc)`, `Dispatch(x, y, z)` implemented; compute queue use stays on the graphics+compute family for now (async compute is out of scope; noted for Phase 4).
- [ ] Storage buffer / storage texture binding types exercised; `ResourceState::ShaderWrite` ↔ `ShaderRead` barriers for compute→graphics and compute→compute hazards using the Day 54 table.
- [ ] Hand-author two compute shaders: `fill.comp.spvasm` (writes `gl_GlobalInvocationID.x * k` into a storage buffer) and `saxpy.comp.spvasm` (storage buffer in/out with a push-constant scalar); assemble/validate/embed via the Day 58 workflow.
- [ ] Tests `[gpu]` (headless): dispatch `fill` over 1M elements, read back, verify every element; dispatch `saxpy` reading the previous result (needs the barrier) and verify; a missing barrier variant is checked only by the validation layer's sync-validation (the test asserts the *correct* version is clean); dispatch sizes beyond `maxComputeWorkGroupCount` are rejected by the RHI with a message.
- [ ] Null device: dispatch legality (pipeline bound, bind groups complete) validated.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** compute APIs documented; guide section "Compute" with the barrier rules.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** compute dispatches produce verified results with correct barriers, headless, validation- and sync-validation-clean.
