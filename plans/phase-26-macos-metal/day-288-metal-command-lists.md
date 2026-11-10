# Phase 26 / Day 288 — Metal V: Command Lists, Encoders, Barriers, Mips

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 287.

Command recording on Metal: one `MTLCommandBuffer` per command list, encoders opened and split around render passes, copies and dispatches, the
render graph's barriers mapped to what Metal needs, mip generation, and recording from several jobs. Modelled on DX12 Day 168.

## Tasks

- [ ] **Command lists** (`metal_command_list.mm`): `Begin` takes a command buffer from the queue (labelled), `End` commits it with a completion handler
      feeding the fence; recording from several jobs in parallel (one command buffer each, committed in submission order through `enqueue`).
- [ ] **Encoders:** `BeginRenderPass` opens an `MTLRenderCommandEncoder` from an `MTLRenderPassDescriptor` (load and store actions, clear values,
      resolve); copies open a blit encoder, dispatches a compute encoder; the open encoder is ended when the kind changes; state that does not survive an
      encoder (bound slots, pipeline, viewport, scissor) is re-applied (the binding cache of Day 286 resets).
- [ ] **Barriers:** with hazard tracking on (decision 9) a `Barrier` inside an encoder is a no-op, between encoders the order is the driver's; a
      read-after-write inside one compute encoder uses `memoryBarrierWithScope:`; the render graph's barrier tests check what was emitted per case.
- [ ] **Draws and dispatches:** indexed and non-indexed draws with `baseInstance` (the object index stream, `first_instance = i`), triangle strips with
      primitive restart (always on for strips on Metal: check the RHI contract and document), `dispatchThreadgroups:` with the reflected size.
- [ ] **Mips:** `GenerateMips` through a blit encoder's `generateMipmapsForTexture:` (no compute shader needed); filter matches the Vulkan path within the
      golden tolerance.
- [ ] **Debug markers:** `pushDebugGroup:`/`popDebugGroup` from the RHI's markers.
- [ ] **Tests:** `compute_test`, the command tests through Metal, `metal_commands_test.mm` (encoder splits, re-applied state after a split, parallel
      recording order, strips with restart, mips).
- [ ] **Documentation:** `rhi.md` Metal section (encoders, barriers, restart); `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: state re-application after every split, submission order across parallel recording,
      command-buffer lifetime until completion.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** command lists record render passes, copies, dispatches and mips on Metal from several jobs with validation silent, the compute tests and the
command tests pass in three flavors, and the barrier mapping is tested per case.
