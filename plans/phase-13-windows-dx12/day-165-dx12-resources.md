# Phase 13 / Day 165 — DX12 II: Buffers, Textures, Upload and Readback

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 164.

Every resource the RHI can create, on D3D12, with the alignment and pitch rules the Phase 4 review predicted.

## Tasks

- [x] **Buffers:** committed resources on the default, upload and readback heaps per the RHI's memory usage; mapping persistent where the RHI allows it;
      `FlushMappedBuffer` a no-op on D3D12's coherent upload heaps (documented); uniform buffers rounded to 256 bytes and every CBV range a multiple of 256; vertex,
      index and storage usage flags mapped to the right initial state.
      *(Default, upload and readback heaps; upload and readback buffers mapped for life with fixed states; `Uniform` rounded to 256; UAV flag only
      on the default heap. `FlushMappedBuffer` keeps the base class's no-op.)*
- [x] **The uniform ring and staging ring** (`uniform_ring.cpp` is neutral; the backend part is its upload heap and fence-based recycling): offsets aligned to 256;
      recycling waits on the fence of the frame that used the space.
      *(The staging ring is a 64 MiB upload buffer with the ring allocator, which moved from `rhi/vulkan/vk_ring.h` into the neutral
      `rhi/staging_ring.h` so both backends share it. `UniformRing` is neutral and needs no backend part: it takes `CpuToGpu` buffers.)*
- [x] **Textures:** formats (`format.h` ↔ `DXGI_FORMAT`, a table test covering every RHI format, including sRGB pairs); 2D, arrays, mips, render-target and
      depth usage; **sampled depth** through a typeless resource (`R32_TYPELESS` for `D32_FLOAT`, `R24G8_TYPELESS` if `D24S8` is exposed) with a DSV and an SRV of
      the matching typed formats; MSAA only if the RHI exposes it.
      *(Every format is in the table test; typeless depth with typed DSV and SRV; RTV/DSV/SRV/UAV made at creation from CPU descriptor heaps
      (`dx12_descriptor_heap`, moved forward from Day 166). MSAA is checked against `MULTISAMPLE_QUALITY_LEVELS`.)*
- [x] **`UploadTexture`/`ReadbackTexture`** with CPU repacking to and from 256-byte row pitches (`GetCopyableFootprints`), placement 512; readback of every
      format the golden tests read; odd sizes (1x1, 3x7, 257 wide) tested.
      *(Footprints from `GetCopyableFootprints` (the copyable format of typeless depth, block-compressed mips of one block); 1x1, 3x7 and 257x5 in
      13 formats, every mip of 64x33, an array, a volume, D32 depth (copied whole), BC1 and BC7 down to one block.)*
- [x] **Accounting:** `Device::Resources()` counts buffers, textures and bytes the same way the Vulkan backend does, so the reset tests' count comparisons work;
      debug names via `SetName` from `debug_name`.
      *(`buffers`, `textures`, `internal_objects` (every raw resource, as on Vulkan) and `device_memory_bytes` from `GetResourceAllocationInfo`.)*
- [x] **Measure:** creation time of 1000 small buffers as committed resources; if it is far above the Vulkan number on this machine, record it and file placed
      resources/sub-allocation (decision 10) — do not add a sub-allocator today unless the spec's threshold is crossed.
      *(1000 x 256-byte buffers: DX12 311 ms committed against Vulkan 22 ms on this PC. Recorded and filed; no sub-allocator today (the
      renderer makes buffers at load time and uses rings per frame).)*
- [x] **Tests** (`tests/rhi/dx12_resource_test.cpp`, phase 13, and the shared buffer/texture tests parameterized by backend where they are already
      backend-neutral): round trips of buffer data, texture upload/readback per format and size, typeless depth view creation, debug layer silent.
      *(`dx12_resource_test.cpp`: 6 tests, debug layer silent. The shared buffer and texture tests are Vulkan-specific files; Day 170 runs the
      Phase 4 suite on DX12.)*
- [x] **Runs:** Windows `--affected` plain and asan (Windows files only, unless a shared test was parameterized: then Linux `--affected` too).
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: repacking arithmetic for block-compressed and odd-sized textures, resource state on creation
      versus first use, mapped pointers outliving their buffer.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** buffers and textures of every RHI kind are created, filled and read back on D3D12 with correct pitches and alignments, sampled depth works through
typeless resources, and resource counts match the Vulkan accounting.

## Notes

- **States.** Buffers on the default heap start in COMMON and decay back to it after every `ExecuteCommandLists`, so uploads rely on implicit
  promotion; textures do not decay, and D3D12 has no "undefined" before-state, so the device records each texture's actual state after
  everything submitted (`TextureState`/`SetTextureState`): uploads and readbacks use and update it now, Day 168's command lists at submit.
  `ReadbackTexture` transitions from the recorded state rather than the caller's (logged at debug level when they differ).
- One build flake: the non-unity tree's link of `renderer_tests` failed once with `LNK1236: corrupt or invalid COFF sections` on
  `dx12_device_later.cpp.obj`; deleting the object and rebuilding fixed it. Not seen again.

## For the Linux run

Shared: `engine/rhi/staging_ring.h/.cpp` (moved from `rhi/vulkan/vk_ring.*`, namespace `rhi`), `engine/rhi/vulkan/vulkan_device.h`,
`engine/rhi/CMakeLists.txt`, `tests/rhi/vulkan_buffer_test.cpp` (the include), `docs/guide/rhi.md`, the reference and the lint baseline. Run:
both unity modes, `--affected` with asan and tsan (the ring is used by the Vulkan uploads), `ctest -L docs`.

## Runs

Windows PC:

- `scripts/test-phase.sh --affected`: `ran 1228 of 2401 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 145 s; the last
  full run took 13 s`.
- `scripts/test-phase.sh --affected --flavor asan`: `ran 908 of 2401 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 74 s;
  the last full run took 13 s`.
- `rhi_tests "[dx12]"`: 15 test cases, 448 assertions, the debug layer silent. All three trees build clean; `cmake --build build-msvc --target docs`:
  51 samples, 0 failed; lint 4315 of 4315 documented.
- Linux: not run; pending until the phase close (Day 174).

## Post-implementation code review

`/code-review high` on the day's diff: 4 findings, 2 fixed, 2 declined.

- **Fixed (with a regression check):** integer formats such as `R32_UInt` were not "sampled" on DX12 (the check asked for filtering), while
  Vulkan accepts them; a shader load is enough now. The run before the fix skipped `r32_uint` as not sampleable (seen); the new check asserts it.
- **Fixed:** the staging ring's memory was subtracted as a constant instead of the size D3D12 reported at creation, which would drift the
  `device_memory_bytes` count if the two differed (equal on this PC, so no test).
- **Declined (filed):** committed resources cost 0.31 ms per buffer against Vulkan's 0.02 ms; sub-allocation waits for a measurement that
  matters (decision 10).
- **Declined:** an upload holds the submission lock while it copies a 16 MiB part into the staging ring, as the Vulkan backend does; uploads are
  load-time work.
