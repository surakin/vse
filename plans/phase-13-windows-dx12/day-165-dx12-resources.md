# Phase 13 / Day 165 — DX12 II: Buffers, Textures, Upload and Readback

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 164.

Every resource the RHI can create, on D3D12, with the alignment and pitch rules the Phase 4 review predicted.

## Tasks

- [ ] **Buffers:** committed resources on the default, upload and readback heaps per the RHI's memory usage; mapping persistent where the RHI allows it;
      `FlushMappedBuffer` a no-op on D3D12's coherent upload heaps (documented); uniform buffers rounded to 256 bytes and every CBV range a multiple of 256; vertex,
      index and storage usage flags mapped to the right initial state.
- [ ] **The uniform ring and staging ring** (`uniform_ring.cpp` is neutral; the backend part is its upload heap and fence-based recycling): offsets aligned to 256;
      recycling waits on the fence of the frame that used the space.
- [ ] **Textures:** formats (`format.h` ↔ `DXGI_FORMAT`, a table test covering every RHI format, including sRGB pairs); 2D, arrays, mips, render-target and
      depth usage; **sampled depth** through a typeless resource (`R32_TYPELESS` for `D32_FLOAT`, `R24G8_TYPELESS` if `D24S8` is exposed) with a DSV and an SRV of
      the matching typed formats; MSAA only if the RHI exposes it.
- [ ] **`UploadTexture`/`ReadbackTexture`** with CPU repacking to and from 256-byte row pitches (`GetCopyableFootprints`), placement 512; readback of every
      format the golden tests read; odd sizes (1x1, 3x7, 257 wide) tested.
- [ ] **Accounting:** `Device::Resources()` counts buffers, textures and bytes the same way the Vulkan backend does, so the reset tests' count comparisons work;
      debug names via `SetName` from `debug_name`.
- [ ] **Measure:** creation time of 1000 small buffers as committed resources; if it is far above the Vulkan number on this machine, record it and file placed
      resources/sub-allocation (decision 10) — do not add a sub-allocator today unless the spec's threshold is crossed.
- [ ] **Tests** (`tests/rhi/dx12_resource_test.cpp`, phase 13, and the shared buffer/texture tests parameterized by backend where they are already
      backend-neutral): round trips of buffer data, texture upload/readback per format and size, typeless depth view creation, debug layer silent.
- [ ] **Runs:** Windows `--affected` plain and asan (Windows files only, unless a shared test was parameterized: then Linux `--affected` too).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: repacking arithmetic for block-compressed and odd-sized textures, resource state on creation
      versus first use, mapped pointers outliving their buffer.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** buffers and textures of every RHI kind are created, filled and read back on D3D12 with correct pitches and alignments, sampled depth works through
typeless resources, and resource counts match the Vulkan accounting.
