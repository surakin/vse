# Phase 26 / Day 301 — Metal II: Buffers, Textures, Storage Modes, Upload and Readback

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 300.

Resources on Metal: buffers and textures with the storage mode chosen by usage, the staging and uniform rings, uploads and readbacks through blit
encoders, and `FlushMappedBuffer` on `Managed` memory. Modelled on DX12 Day 165.

## Tasks

- [ ] **Formats** (`metal_format.mm`): every RHI `Format` to `MTLPixelFormat` (sRGB variants, depth formats: `Depth32Float`, `Depth24Unorm_Stencil8` only
      where the device supports it, else refused), with a table test against `DeviceCaps`.
- [ ] **Storage modes:** GPU-only resources `Private`; upload, readback, uniform rings `Shared`; a CPU-written, GPU-read resource on a non-unified device
      `Managed`, with `FlushMappedBuffer` as `didModifyRange:` and readback through `synchronizeResource:` in a blit encoder. **This Mac is unified**, so
      the `Managed` path is forced by a debug setting in a test (allowed on macOS) and checked to produce the same bytes; recorded as the only coverage.
- [ ] **Buffers:** `CreateBuffer`, mapping (`contents`), the staging ring and uniform ring (`staging_ring.h`, `uniform_ring.h`) on `Shared` buffers;
      alignment rules (constant-buffer offsets 256 on macOS) in `DeviceCaps`.
- [ ] **Textures:** 2D, arrays, cube, mips, render-target and depth usage flags to `MTLTextureUsage`; `UploadTexture` through the staging ring and a blit
      `copyFromBuffer:...toTexture:` (row pitch alignment honored), `ReadbackTexture` the reverse; sampled depth with the right view format.
- [ ] **Destruction:** resources released when the GPU is done (deferred by the frame fence, as the other backends); `Resources()` counts go back to zero.
- [ ] **Tests:** the shared resource tests through a Metal factory (`tests/rhi/*` that are backend-neutral), a `metal_resource_test.mm` (storage-mode
      choice per usage, the forced-`Managed` round trip, pitch alignment, every format's upload and readback round trip).
- [ ] **Documentation:** `rhi.md` Metal section (storage modes, the forced-Managed debug setting); `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: resource lifetime against in-flight command buffers, the ring's wraparound with
      completion on another thread, `didModifyRange` ranges.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md` (the Managed coverage limit); commit and push.

**Done:** buffers and textures of every RHI format round-trip through upload and readback on Metal with validation silent, the forced-`Managed` path gives
identical bytes, resource counts return to zero, and the tests pass in three flavors.
