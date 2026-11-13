# Phase 21 / Day 301 — Metal II: Buffers, Textures, Storage Modes, Upload and Readback

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 300.

Resources on Metal: buffers and textures with the storage mode chosen by usage, the staging and uniform rings, uploads and readbacks through blit
encoders, and `FlushMappedBuffer` on `Managed` memory. Modelled on DX12 Day 165.

## Tasks

- [x] **Formats** (`metal_format.mm`): every RHI `Format` to `MTLPixelFormat` (sRGB variants, depth formats: `Depth32Float`, `Depth24Unorm_Stencil8` only
      where the device supports it, else refused), with a table test against `DeviceCaps`.
- [x] **Storage modes:** GPU-only resources `Private`; upload, readback, uniform rings `Shared`; a CPU-written, GPU-read resource on a non-unified device
      `Managed`, with `FlushMappedBuffer` as `didModifyRange:` and readback through `synchronizeResource:` in a blit encoder. **This Mac is unified**, so
      the `Managed` path is forced by a debug setting in a test (allowed on macOS) and checked to produce the same bytes; recorded as the only coverage.
- [x] **Buffers:** `CreateBuffer`, mapping (`contents`), the staging ring and uniform ring (`staging_ring.h`, `uniform_ring.h`) on `Shared` buffers;
      alignment rules (constant-buffer offsets 256 on macOS) in `DeviceCaps`.
- [x] **Textures:** 2D, arrays, cube, mips, render-target and depth usage flags to `MTLTextureUsage`; `UploadTexture` through the staging ring and a blit
      `copyFromBuffer:...toTexture:` (row pitch alignment honored), `ReadbackTexture` the reverse; sampled depth with the right view format.
- [x] **Destruction:** resources released when the GPU is done (deferred by the frame fence, as the other backends); `Resources()` counts go back to zero.
- [x] **Tests:** (the Metal file; the shared backend-neutral suites join through the GPU fixture on Day 306) the shared resource tests through a Metal factory (`tests/rhi/*` that are backend-neutral), a `metal_resource_test.mm` (storage-mode
      choice per usage, the forced-`Managed` round trip, pitch alignment, every format's upload and readback round trip).
- [x] **Documentation:** `rhi.md` Metal section (storage modes, the forced-Managed debug setting); `docgen lint` zero.
- [x] **Runs:** `ran 6110 of 6490 tests (phases: all; flavors: all; display: included; crash tests: excluded) in 1479 s` ([the Day 309 run](day-309-metal-lifecycle.md#the-run-of-days-315-325)). Originally: `scripts/test-phase.sh --affected --flavor all` (quote). **Deferred to Day 309 (the user's choice).**
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: resource lifetime against in-flight command buffers, the ring's wraparound with
      completion on another thread, `didModifyRange` ranges.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md` (the Managed coverage limit); commit and push.

**Done:** buffers and textures of every RHI format round-trip through upload and readback on Metal with validation silent, the forced-`Managed` path gives
identical bytes, resource counts return to zero, and the tests pass in three flavors.

## Implementation notes

- **`metal_device_resources.mm`:** buffers and textures in handle pools seeded from the device serial, destruction deferred by the frame fence
  (collected in `BeginFrame` and `WaitIdle`), counts (`Resources()`: buffers, textures, every Metal resource alive with the staging ring, and
  their `allocatedSize`), the injected allocation failure, and a leak count at destruction (`MetalDevice::LeakedUserObjects`).
- **Storage modes (amended by the review):** GPU-only buffers and every texture `Private`; readback memory (`GpuToCpu`) and the staging ring
  `Shared` on every Mac; CPU-written memory (`CpuToGpu`) `Shared` on unified memory and `Managed` otherwise (macOS only), with
  `didModifyRange:` from `FlushMappedBuffer`, `UnmapBuffer` and the `CpuToGpu` `UploadBuffer`. The plan's `synchronizeResource:` path is not
  needed: the GPU never writes Managed memory. `ForceManagedStorageForTest` is the test hook ("debug setting") that covers it here.
- **Copies:** uploads through the 64 MiB staging ring in 16 MiB chunks and bands of rows (depth levels too: Metal copies regions of a
  depth-only texture); readbacks through a temporary `Shared` buffer (`BlitToCpu`). Buffer copies use the 4-byte-aligned window around the
  range (Metal's rule on macOS; buffers are allocated to a multiple of 4), an unaligned write reading the window's edges back first. Rows are
  packed (`buffer_texture_copy_row_pitch_alignment` 1); the caller's row pitch is honoured on upload.
- **Shared change:** `rhi::MipExtent` in `rhi/format.h` (documented), replacing five private copies (D3D12, Vulkan twice, null, Metal).
- **Tests added** (run on Day 309): `tests/rhi/macos/metal_resource_test.mm` (storage modes, buffer paths with and without the forced
  `Managed` mode, unaligned and odd-sized buffers, every format's three mips, a padded row pitch, array layers and a 3D texture, a two-band
  texture, deferred destruction and counts, the injected allocation failure and the leak count); `tests/rhi/macos/metal_test_support.h`
  holds the helpers the Metal test files share (a unity-build collision found by the build).
- **Docs:** `docs/guide/rhi.md` (Metal memory); reference regenerated (`MipExtent`), `docgen lint` at zero.
- **Built:** both unity modes on macOS, every flavor, no warning. The Vulkan and D3D12 files that lost their copies of `MipExtent` do not
  build on the Mac: waiting for the Linux machine and the PC (`KNOWN-ISSUES.md`). **Runs: deferred to Day 309.**

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings, all fixed.

1. **Buffer copies at offsets or sizes that are not multiples of 4** (Metal's rule on macOS; the new test read at 4097). Fixed: the aligned
   window, with the edges read back for a write; buffers are a multiple of 4 long; tests for an unaligned write and a 13-byte buffer.
2. **A persistent mapping of a Managed readback buffer went stale** (synchronized once, at map time). Fixed: readback memory is `Shared` on
   every Mac.
3. **`FlushMappedBuffer` on a Managed readback buffer could overwrite the GPU's results.** Fixed by the same change: only `CpuToGpu` memory
   is ever Managed.
4. **Depth levels were copied whole and refused above 16 MiB.** Fixed: depth levels go in bands like color.
5. **`synchronizeResource:` and `MTLStorageModeManaged` would not compile for iOS (Phase 27).** Fixed: the first is gone, the second is
   behind `ENGINE_PLATFORM_MACOS`.
6. **A lost device's upload failures were not logged, and multisampled textures reached the blits.** Fixed: logged; asserted (a contract
   error, as on the other backends).
7. **Three copies of the readback sequence.** Fixed: `BlitToCpu`.
8. **`MipExtent` copied in five backends.** Fixed: `rhi::MipExtent` in `rhi/format.h`.
9. **An unused include in `metal_device.h`.** Removed.
10. **`STATUS.md` and the day file not updated.** Done in this commit.
