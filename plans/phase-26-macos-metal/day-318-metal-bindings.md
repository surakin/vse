# Phase 26 / Day 318 — Metal III: Bind Groups, Samplers and Slots from Reflection

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 317.

The RHI's bind groups (sets 0/1/2: frame, material, objects) on Metal. Planned default (decision 9): direct `set*Buffer`/`set*Texture`/
`set*SamplerState` calls at slots read from the minted reflection, not argument buffers; today measures and may amend. Modelled on DX12 Day 166.

## Tasks

- [ ] **Bind-group layouts** (`metal_bindings.mm`): a layout records, for each binding, the Metal slot per stage from the shader reflection Day 315
      emits (slangc flattens sets to buffer slots: frame `[[buffer(0)]]`, objects `[[buffer(1)]]` in `gbuffer.vert`); a pipeline's layout is checked against
      the shaders' slots at creation, with a clear error on a mismatch.
- [ ] **Bind groups:** hold their resources by RHI handle (not raw `id<MTLBuffer>` past a frame: the reset rule); `SetBindGroup` issues the set calls for
      the bound stages, skipping unchanged slots; push constants as `setVertexBytes:`/`setFragmentBytes:` up to 4 KiB.
- [ ] **Samplers:** `MTLSamplerDescriptor` from `SamplerDesc` (anisotropy, compare functions, `ClampToBorder` with border colors supported on macOS;
      the iOS refusal noted for Phase 27); cached by description.
- [ ] **Measure:** `SetBindGroup` cost with direct binding against the Vulkan and DX12 numbers in `rhi_bench` (different machines; recorded, no target),
      and the argument-buffer tier this device reports; amend decision 9 only if direct binding is clearly the bottleneck in the renderer benchmark (then
      a follow-up day is inserted, renumbering per CLAUDE.md).
- [ ] **Tests:** the shared binding tests through Metal; `metal_bindings_test.mm` (slot mapping from reflection, a deliberate mismatch rejected, unchanged
      slots skipped, sampler cache).
- [ ] **Documentation:** `rhi.md` Metal section (slots, push constants limit); `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: stale-slot skipping across encoder boundaries (a new encoder starts empty), handles
      resolved at record time.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** bind groups and samplers work on Metal at reflection-derived slots with mismatches rejected, the binding tests pass in three flavors, and the
`SetBindGroup` measurement and the device's argument-buffer tier are recorded with decision 9 confirmed or amended.
