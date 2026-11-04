# Phase 3 / Day 55 — Textures, Samplers & Layout Transitions

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 54.

Images are the other half of the resource model.

## Tasks

- [ ] Vulkan `CreateTexture/DestroyTexture`: `VkImage` + view(s) from the descriptor (2D, array layers, mips; 3D supported, cube reserved), usage mapping, format mapping table `Format → VkFormat` (verified by a test that every `Format` has a mapping and that sRGB/compressed flags agree with `FormatInfo`); format-support query (`vkGetPhysicalDeviceFormatProperties`) reported as a `Device::FormatSupport(Format, TextureUsage)` capability.
- [ ] Image layout tracking is **not** hidden: the caller moves textures between `ResourceState`s with `Barrier`; the null device already validates the state machine, the Vulkan device trusts the caller but validation layers confirm it. Initial state is `Undefined`.
- [ ] Texture upload: `UploadTexture(dst, mip, layer, data, row_pitch)` via the staging ring, a `CopyBufferToImage` with the correct buffer row length/image height, transition to `ShaderRead` at the end; block-compressed upload sizes from `FormatInfo`.
- [ ] Mip generation helper `GenerateMips(texture)` on the command list via blits (requires linear-blit support for the format; query it, document the fallback of uploading precomputed mips).
- [ ] Samplers: `CreateSampler` from `SamplerDesc` with anisotropy clamped to the device limit, border/compare modes mapped; a sampler cache (hash of the desc) so identical samplers share one object.
- [ ] Readback of textures (`CopyTextureToBuffer` + staging) for tests.
- [ ] Tests `[gpu]`: upload a known 64×64 RGBA8 pattern, read it back identical (including a non-power-of-two size and a row pitch that differs from width×4); mip chain generated from a checkerboard has the expected averaged first mip within ±1; sampler cache returns one handle for equal descs; every `Format` round-trips through the mapping test; destruction is validation-clean.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** texture/sampler APIs and `FormatSupport` documented; guide section "Textures and samplers" including the barrier/state rules.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** textures upload and read back bit-exactly, mips generate, samplers deduplicate, and the format table is covered.
