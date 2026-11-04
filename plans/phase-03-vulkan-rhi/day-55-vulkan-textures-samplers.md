# Phase 3 / Day 55 — Textures, Samplers & Layout Transitions

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 54.

Images are the other half of the resource model.

## Tasks

- [x] Vulkan `CreateTexture/DestroyTexture`: `VkImage` + view(s) from the descriptor (2D, array layers, mips; 3D supported, cube reserved), usage mapping, format mapping table `Format → VkFormat` (verified by a test that every `Format` has a mapping and that sRGB/compressed flags agree with `FormatInfo`); format-support query (`vkGetPhysicalDeviceFormatProperties`) reported as a `Device::FormatSupport(Format, TextureUsage)` capability.
- [x] Image layout tracking is **not** hidden: the caller moves textures between `ResourceState`s with `Barrier`; the null device already validates the state machine, the Vulkan device trusts the caller but validation layers confirm it. Initial state is `Undefined`.
- [x] Texture upload: `UploadTexture(dst, mip, layer, data, row_pitch)` via the staging ring, a `CopyBufferToImage` with the correct buffer row length/image height, transition to `ShaderRead` at the end; block-compressed upload sizes from `FormatInfo`.
- [x] Mip generation helper `GenerateMips(texture)` on the command list via blits (requires linear-blit support for the format; query it, document the fallback of uploading precomputed mips).
- [x] Samplers: `CreateSampler` from `SamplerDesc` with anisotropy clamped to the device limit, border/compare modes mapped; a sampler cache (hash of the desc) so identical samplers share one object.
- [x] Readback of textures (`CopyTextureToBuffer` + staging) for tests.
- [x] Tests `[gpu]`: upload a known 64×64 RGBA8 pattern, read it back identical (including a non-power-of-two size and a row pitch that differs from width×4); mip chain generated from a checkerboard has the expected averaged first mip within ±1; sampler cache returns one handle for equal descs; every `Format` round-trips through the mapping test; destruction is validation-clean.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** texture/sampler APIs and `FormatSupport` documented; guide section "Textures and samplers" including the barrier/state rules.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** textures upload and read back bit-exactly, mips generate, samplers deduplicate, and the format table is covered.

## Result

Added Vulkan `CreateTexture`/`DestroyTexture` (image, sampling view, attachment view, per-format limit check, deferred destruction), `CreateSampler` (shared and reference counted, anisotropy clamped), `FormatSupport` and `CanGenerateMips` from a format-feature table read at device creation, `UploadTexture` (row bands through the staging ring, any row pitch, 2D, arrays, mips, 3D, block-compressed), `ReadbackTexture`, command-list `CopyBufferToTexture`/`CopyTextureToBuffer`/`Barrier(texture)`, and `CommandList::GenerateMips` (blit chain). The null backend mirrors the API and the state rules. Copy and blit states use the `ALL_TRANSFER` stage because sync validation showed blits run at the BLIT stage. Tests: 64x64 and 37x23 (pitch 160) round trips, mip/layer sets, a multi-chunk 8 MiB level, 3D, BC1 (when supported), checkerboard mips within 126..129, buffer/texture copies in a list, sampler sharing and reference counts, deferred destruction counts, rejection cases, and null-device state rules; plain and ASan with validation, TSan for the non-GPU ones. Docs 1555/1555; guide section "Textures and samplers".

Deviations from the plan:
- `UploadTexture` takes the row pitch and leaves the texture in `ShaderRead`; the device remembers only what its own upload calls did (barriers in lists are invisible to it), which is documented on the call.
- `ReadbackTexture` takes the texture's current state from the caller for the same reason.
- The sampler cache is a scan of the live samplers, not a hash: sampler counts are small.
- Depth-stencil formats with a stencil aspect cannot be uploaded, copied or read back (Vulkan needs one aspect per copy region); depth-only formats can.

## Post-implementation code review

`/code-review high` returned 10 findings.

Fixed:
1. Copies used the combined depth-stencil aspect mask: copies now use one aspect (`CopyAspectOf`) and combined formats are rejected for upload, copy and readback.
2. Stale device-side state in `ReadbackTexture`: the caller now passes the state (the null device checks it); the upload-state limitation is documented.
3. Concurrent uploads to one texture race: documented as unsupported (one thread per texture).
4. A failed multi-chunk upload left the image in TRANSFER_DST: it is now moved to `ShaderRead` best-effort and the state updated, and the doc says the level is partly written.
5. No per-format image limits check: `CreateTexture` now queries `vkGetPhysicalDeviceImageFormatProperties`.
6. Cleanup under `resource_mutex_` and reliance on a moved-from value: handle allocation takes a copy and failure cleanup runs outside the lock.
9. `GenerateMips` state cannot be verified on Vulkan: documented, with validation layers as the net.
10. STATUS.md and this section.

Declined:
7. `CreateSampler` scans all live samplers under the resource lock: sampler counts are in the tens, creation is rare, and a hash adds a structure to keep in step with the pool; revisit if a project creates samplers per frame.
8. `bufferOffset` multiple of 4 for 1- and 2-byte formats: the rule applies to transfer-only queues, and all work runs on a graphics queue here; depth-stencil formats, the other case, are now rejected.
