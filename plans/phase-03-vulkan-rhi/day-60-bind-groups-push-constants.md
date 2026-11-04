# Phase 3 / Day 60 — Bind Groups, Descriptors & Push Constants

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 59.

Getting uniform buffers, textures and samplers to shaders.

## Tasks

- [ ] `CreateBindGroupLayout` → `VkDescriptorSetLayout` (uniform buffer, storage buffer, sampled texture, sampler, combined image sampler avoided in the RHI, storage texture; stage flags; array counts); layout cache.
- [ ] Descriptor allocation: per-frame linear descriptor pools (reset with the frame) for transient groups plus a persistent pool for long-lived groups; pool growth when exhausted (new pool, never a failed allocation); sizes configured by `DeviceCreateInfo`.
- [ ] `CreateBindGroup(layout, entries)` writes buffer ranges (offset/size, dynamic offsets reserved), texture + state (`ShaderRead`), samplers; destroying a group retires it with the frame. `SetBindGroup(slot, group)` binds with `vkCmdBindDescriptorSets`.
- [ ] `SetPushConstants(stage, offset, data, size)` with range checking against the pipeline layout and `DeviceCaps::max_push_constant_bytes`.
- [ ] Uniform buffer helper: `UniformRing` — per-frame bump allocator over a persistently mapped `CpuToGpu` buffer honoring the min uniform-offset alignment, returning (buffer, offset) for a bind group entry; reset each frame.
- [ ] Tests `[gpu]` (headless compute-free): bind groups built for a uniform + texture + sampler layout validate cleanly; misuse caught on the null device (group created against a different layout, binding a destroyed texture, texture in the wrong state); `UniformRing` offsets respect alignment for random sizes and never overlap; pool exhaustion grows rather than fails (allocate 10k transient groups in a frame).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** bind-group, push-constant and `UniformRing` APIs documented; guide section "Binding resources" with the per-frame vs persistent lifetime table.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** bind groups, push constants and the uniform ring work and misuse is caught early; heavy transient descriptor use grows pools instead of failing.
