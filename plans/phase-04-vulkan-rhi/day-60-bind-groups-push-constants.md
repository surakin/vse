# Phase 4 / Day 60 — Bind Groups, Descriptors & Push Constants

**Phase:** [Phase 4 — Vulkan RHI](../phase-04-vulkan-rhi.md)
**Depends on:** Day 59.

Getting uniform buffers, textures and samplers to shaders.

## Tasks

- [x] `CreateBindGroupLayout` → `VkDescriptorSetLayout` (uniform buffer, storage buffer, sampled texture, sampler, combined image sampler avoided in the RHI, storage texture; stage flags; array counts); layout cache.
- [x] Descriptor allocation: per-frame linear descriptor pools (reset with the frame) for transient groups plus a persistent pool for long-lived groups; pool growth when exhausted (new pool, never a failed allocation); sizes configured by `DeviceCreateInfo`.
- [x] `CreateBindGroup(layout, entries)` writes buffer ranges (offset/size, dynamic offsets reserved), texture + state (`ShaderRead`), samplers; destroying a group retires it with the frame. `SetBindGroup(slot, group)` binds with `vkCmdBindDescriptorSets`.
- [x] `SetPushConstants(stage, offset, data, size)` with range checking against the pipeline layout and `DeviceCaps::max_push_constant_bytes`.
- [x] Uniform buffer helper: `UniformRing` — per-frame bump allocator over a persistently mapped `CpuToGpu` buffer honoring the min uniform-offset alignment, returning (buffer, offset) for a bind group entry; reset each frame.
- [x] Tests `[gpu]` (headless compute-free): bind groups built for a uniform + texture + sampler layout validate cleanly; misuse caught on the null device (group created against a different layout, binding a destroyed texture, texture in the wrong state); `UniformRing` offsets respect alignment for random sizes and never overlap; pool exhaustion grows rather than fails (allocate 10k transient groups in a frame).
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** bind-group, push-constant and `UniformRing` APIs documented; guide section "Binding resources" with the per-frame vs persistent lifetime table.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** bind groups, push constants and the uniform ring work and misuse is caught early; heavy transient descriptor use grows pools instead of failing.

## Result

Bind groups work on the Vulkan device (`vulkan_device_bindings.cpp`) and match the null backend's checks: usage, offset alignment, range, one entry per binding element, live resources, and (new) the device's uniform/storage buffer range limit. Descriptor sets come from two kinds of pool: persistent pools (sets freed one by one when a destroyed group is collected) and per-frame pools (reset in `BeginFrame` once the frame that used them has finished). Pools grow instead of failing (`DeviceCreateInfo::descriptor_sets_per_pool`, 1024 by default, clamped 16..65536); a layout needing more descriptors of a type than the default sizing gets an exact-size pool, which later groups that fit reuse. `BindGroupDesc::transient` marks a group valid only in the frame it was created in and released by the device; `DestroyBindGroup` on one is an error. Groups remember the buffers, textures and samplers they name, so `SetBindGroup` reports a destroyed resource. `VulkanCommandList` binds groups lazily at draw time against the pipeline's layout (`FlushBindGroups`), checks each slot's layout id against the pipeline's, and `SetPushConstants` checks the pipeline, 4-byte alignment and the declared range. `rhi::UniformRing` is a backend-agnostic, lock-free bump allocator over a persistently mapped `CpuToGpu` buffer with three per-frame regions; `Flush()` makes the frame's writes visible on devices without coherent host memory (new `Device::FlushMappedBuffer`, a no-op by default). Tests (`tests/rhi/vulkan_bindings_test.cpp`): ring alignment, no overlap, fullness, rotation and multithreaded disjointness (null device); transient groups on the null device; a real draw reading a uniform buffer, a texture and a sampler plus a transient group and a push constant, checked by readback; 10k transient groups with 64-set pools; persistent recycling; a 200-sampler array and its pool reuse; every misuse refusal. Docs 1683/1683 (guide section "Binding resources" in `docs/guide/rhi.md`); `ctest` 1197/1197 with `ENGINE_RHI_REQUIRE_GPU=1` including asan and tsan.

Deviations from the plan: `CreateBindGroupLayout` was done on Day 59 (pipeline layouts need it). `transient` is a field of `BindGroupDesc` rather than a separate call. "Texture + state `ShaderRead`" is not tracked per bind group on Vulkan: the device cannot see states inside command lists, so the validation layers and the null backend check it (the guide says so). Dynamic offsets stay reserved.

## Post-implementation code review

`/code-review high` returned 9 findings.

Fixed:
1. Every group with a layout beyond the default sizing created a fresh one-set pool that was never reused (a leak per frame for transient groups): exact pools are kept with their per-type capacities and reused when they cover the layout, and reset or freed like the others; test added (20 persistent and 10 transient large groups leave the pool count unchanged / at most one per frame slot).
2. `UniformRing` never flushed non-coherent memory: `UniformRing::Flush()` plus `Device::FlushMappedBuffer` (flushes the used range on Vulkan, no-op elsewhere); the guide shows the call before `Submit`. No automated test (the dev GPU's memory is coherent).
4. No check against `maxUniformBufferRange` / `maxStorageBufferRange`, including for size 0 (whole buffer): `CreateBindGroup` refuses an over-limit range; the guide says to bind each ring allocation by offset and size. No automated test, since the limit is device-dependent and not exposed in `DeviceCaps`.
5. Any allocation error other than "pool full" was retried across every pool and a fresh one: it now stops at the first hard failure.
7. The null backend scanned all live groups at every `EndFrame`: it keeps a per-frame list like the Vulkan backend.
8. `FlushBindGroups` re-asserted layout ids on every draw: only slots changed since the last draw (`SetPipeline` marks all) are checked.

Declined:
3. "`ResetFrameDescriptorsLocked` resets pools under `resource_mutex_`": a transient group created by another thread right after `BeginFrame` opens the frame would otherwise allocate from a pool before it is reset; the reset must happen before the frame is visible as open. The cost is bounded now that exact pools are reused.
6. "Transient groups are not retired after a device loss": once the device is lost, the pools cannot be reset safely (the GPU may still read them) and every later submission is ignored; the device is unusable and is destroyed next. Not worth extra state.
9. "Sampled textures are always described as `SHADER_READ_ONLY_OPTIMAL`": that is the layout the RHI's `ShaderRead` state maps to for every format (including depth), and `SampledTexture` bindings require the texture to be in `ShaderRead` when the draw executes; storage textures use `GENERAL` as the `ShaderWrite` state does.
