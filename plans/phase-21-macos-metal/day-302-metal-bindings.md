# Phase 21 / Day 302 — Metal III: Bind Groups, Samplers and Slots from Reflection

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 301.

The RHI's bind groups (sets 0/1/2: frame, material, objects) on Metal. Planned default (decision 9): direct `set*Buffer`/`set*Texture`/
`set*SamplerState` calls at slots read from the minted reflection, not argument buffers; today measures and may amend. Modelled on DX12 Day 166.

## Tasks

- [x] **Bind-group layouts** (`metal_bindings.mm`): a layout records, for each binding, the Metal slot per stage from the shader reflection Day 299
      emits (slangc flattens sets to buffer slots: frame `[[buffer(0)]]`, objects `[[buffer(1)]]` in `gbuffer.vert`); a pipeline's layout is checked against
      the shaders' slots at creation, with a clear error on a mismatch.
- [x] **Bind groups:** hold their resources by RHI handle (not raw `id<MTLBuffer>` past a frame: the reset rule); `SetBindGroup` issues the set calls for
      the bound stages, skipping unchanged slots; push constants as `setVertexBytes:`/`setFragmentBytes:` up to 4 KiB.
- [x] **Samplers:** `MTLSamplerDescriptor` from `SamplerDesc` (anisotropy, compare functions, `ClampToBorder` with border colors supported on macOS;
      the iOS refusal noted for Phase 27); cached by description.
- [x] **Measure:** (Day 309: argument buffers tier 1, read-write textures tier 1; `rhi_bench`, which binds groups per draw, 0.64-0.89 us per draw on Metal against 0.73-0.87 on the Radeon 860M (D3D12) and 0.5-0.6 on the RTX 3060 (Vulkan): direct binding stays, decision 9 confirmed) ([the Day 309 run](day-309-metal-lifecycle.md#the-run-of-days-315-325)). Originally: (deferred to Day 309: `SetBindGroup` is timed through command lists, which come on Day 304; the argument-buffer tier is printed by `metal_device_test`) `SetBindGroup` cost with direct binding against the Vulkan and DX12 numbers in `rhi_bench` (different machines; recorded, no target),
      and the argument-buffer tier this device reports; amend decision 9 only if direct binding is clearly the bottleneck in the renderer benchmark (then
      a follow-up day is inserted, renumbering per CLAUDE.md).
- [x] **Tests:** (the Metal file; the shared binding tests join through the GPU fixture on Day 306) the shared binding tests through Metal; `metal_bindings_test.mm` (slot mapping from reflection, a deliberate mismatch rejected, unchanged
      slots skipped, sampler cache).
- [x] **Documentation:** `rhi.md` Metal section (slots, push constants limit); `docgen lint` zero.
- [x] **Runs:** `ran 6110 of 6490 tests (phases: all; flavors: all; display: included; crash tests: excluded) in 1479 s` ([the Day 309 run](day-309-metal-lifecycle.md#the-run-of-days-315-325)). Originally: `scripts/test-phase.sh --affected --flavor all` (quote). **Deferred to Day 309 (the user's choice).**
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: stale-slot skipping across encoder boundaries (a new encoder starts empty), handles
      resolved at record time.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** bind groups and samplers work on Metal at reflection-derived slots with mismatches rejected, the binding tests pass in three flavors, and the
`SetBindGroup` measurement and the device's argument-buffer tier are recorded with decision 9 confirmed or amended.

## Implementation notes

- **Plan amended: slot tables.** Slang chooses each Metal library's slots itself, and nothing carried them to the RHI. Each minted
  `.metallib.inc` now holds `<Base>MetallibSlots`, an `rhi::ShaderSlotTable` (new in `rhi/desc.h`). It lists every resource's set,
  binding, Metal slot, declared type and array size, plus the push-constant block's buffer slot, all from the reflection.
  `rhi::MetalLibBlob` takes it, `ShaderDesc::SetCode` copies it into `ShaderDesc::slots`, and `renderer::PermutationModule` carries one
  per module. `tools/shader_mint` writes it into the `.metallib.inc`, the placeholder (an empty table), the `.dxil.inc` blob set and the
  `.perm.inc` rows. A committed `.metallib.inc` without the tables counts as needing a placeholder off a Mac.
- **Re-minted on the Mac:** all 29 `.metallib.inc` and the 4 `.perm.inc`. The library bytes are unchanged, and a second mint was
  byte-identical (twice: after the first format and after the review's change). The 29 `.dxil.inc` files were edited mechanically to pass
  `&<Base>MetallibSlots`, the text the updated template writes; the PC's re-mint must reproduce it (`KNOWN-ISSUES.md`).
- **`metal_device_bindings.mm`:**
  - **Samplers:** `MTLSamplerDescriptor` from `SamplerDesc`, shared by equal descriptions under one lock, border colors on macOS.
  - **Layouts:** sorted by binding.
  - **Groups:** validated like D3D12's, hold handles, resolved when bound (`ResolveGroup`). Transient groups end when their frame's slot
    comes round (4 slots).
  - **Slot maps:** `MapStageSlots` matches a stage's table against the pipeline's layouts; Day 303 calls it at pipeline creation. It
    checks a missing binding, stage visibility, the type's table, the array size, the table limits, shared slots, the push-constant slot,
    and a missing table.
  - **Binder:** `MetalBinder` sets each resource at its stage's slot by its class, skips unchanged slots, starts empty on a new encoder,
    and writes push constants with `set*Bytes:`.
- **Decision 9 stands for now:** direct binding, no argument buffers. The measurement waits for command lists (Day 304) and the runs (Day 309).
- **Tests added** (run on Day 309): `tests/rhi/macos/metal_bindings_test.mm` covers:
  - the minted table reaching the descriptor;
  - the mapping, and every rejected mismatch;
  - the binder's skipping, a new encoder, push constants and a group of another layout refused;
  - the sampler cache;
  - group validation, handles resolved late, and transient groups.
- **Docs:** `docs/guide/rhi.md` (Metal bindings), `docs/guide/rhi-shaders.md` (slot tables) and `shaders/README.md`; the reference is
  regenerated and `docgen lint` is at zero.
- **Built:** both unity modes on macOS, every flavor, no warning. **Runs: deferred to Day 309.**

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings, 9 fixed, 1 fixed in part.

1. **The binder chose the argument table by which object was non-nil**, so a nil object or a group of another layout could write past
   the 16-entry sampler array. Fixed: the table follows the binding's type, and a nil object makes the group invalid; tested.
2. **A group resource was matched to a slot by set and binding only.** Fixed: a resource of another class or array size is refused, and
   the bind fails before setting anything; tested with a sampler group bound where the pipeline has a buffer.
3. **The slot tables recorded neither type nor count**, so those mismatches could not be found. Fixed: `ShaderSlot` has both, re-minted;
   tested.
4. **A missing table counted as "binds nothing".** Fixed: refused when the stage's layouts give it a binding; tested.
5. **Encoder calls were made under the device lock.** Fixed: `ResolveGroup` copies the objects, retained, into the binder's reused scratch
   vector, and the calls follow without the lock.
6. **The push-constant slot was not range-checked.** Fixed; tested.
7. **A shared sampler handle hides a double destroy, and the lookup and the creation ran in separate lock scopes.** The race is fixed: one
   lock covers both. The sharing is kept, as on D3D12, so the two backends behave alike.
8. **`LayoutBindings` was read once per table entry.** Fixed: once per set.
9. **A hand-written insertion sort.** Fixed: `Sort`. The build then found that `rhi::Span` is a read-only alias, so the sort uses the
   foundation's `Span`.
10. **Slot limits were written twice, and `kNoShaderSlot` was declared after its first use.** Fixed: `kMetalBufferSlots`,
    `kMetalTextureSlots` and `kMetalSamplerSlots` in the header; `kNoShaderSlot` before `ShaderSlotTable`.

Outside the cap: push constants had no code yet although the guide described them. Done (`PushRender`, `PushCompute`).
