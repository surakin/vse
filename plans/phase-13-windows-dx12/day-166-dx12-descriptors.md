# Phase 13 / Day 166 — DX12 III: Descriptor Heaps, Root Signatures, Bind Groups

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 165; the spec's root-signature model (Day 147).

Bind groups on D3D12: CPU descriptor heaps, a root signature per pipeline layout, and descriptor copies into per-frame shader-visible heaps at `SetBindGroup`.

## Tasks

- [x] **CPU descriptor heaps:** allocators for CBV/SRV/UAV, sampler, RTV and DSV (free lists, sized from the spec, growth by adding heaps), with views created when a
      resource is created and freed with deferred destruction.
- [x] **Samplers:** every RHI sampler state mapped (`D3D12_SAMPLER_DESC`), border colors as the RHI allows, anisotropy; heap samplers (static samplers only if
      the spec chose them for fixed cases).
- [x] **Bind group layouts → root signatures:** one descriptor table per bind group (CBV/SRV/UAV range plus a sampler table) and root constants for push constants (the
      object index needs no root parameter: spec decision 9; the layout is the spec's section 5); serialized with root signature 1.1 and cached by layout hash; the register/space assignment matches the minted
      reflection (a test checks every committed shader's reflection against the layout it is used with).
- [x] **Bind groups:** a bind group stores its CPU descriptors; `SetBindGroup` copies them into the current frame's shader-visible ring (`CopyDescriptorsSimple`)
      and sets the table; the ring is per frame in flight and recycled by fence; running out of ring space is an error with sizes, not a hang.
- [ ] **Measure `SetBindGroup`** (moved to Day 168, see Notes) with `rhi_bench` on DX12 versus Vulkan on the same PC (Day 161 baseline) and record both; if DX12 is much slower, try caching a
      group's GPU copy across frames when unchanged (and record the result) before moving on.
- [x] **Tests** (`tests/rhi/dx12_bindings_test.cpp`, phase 13; the shared bindings tests parameterized by backend): every binding type reaches a compute shader
      and reads back correctly, many groups per frame, ring wrap-around across 3 frames, root signature cache hits, debug layer silent.
- [x] **Runs:** Windows `--affected` plain and asan (Windows files only, unless shared tests changed: then Linux too).
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: descriptors freed while a frame in flight still references their copy, ring indices across
      wrap-around, table offsets per heap type.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every bind group layout becomes a cached root signature, bind groups reach shaders through per-frame shader-visible heaps, and the `SetBindGroup` cost is
measured against Vulkan on the same machine.

## Notes

- **Plan amendment (2026-11-08).** `SetBindGroup` records into a command list, which arrives on Day 168, and a pipeline needs Day 167: so the
  `rhi_bench` measurement, "every binding type reaches a compute shader and reads back", "many groups per frame" and the ring wrap-around with real
  submissions move to Day 168's tests, and the check of every committed shader's reflection against its root signature to Day 167 (where every
  committed shader becomes a pipeline). Day 166 tests what exists without them: samplers, layouts, groups, the shared sampler tables, the ring's
  slots, the transient groups and the root signature cache, with the debug layer silent.
- **The stride.** D3D12 reads a `StructuredBuffer` through a structured view, made with the element size, so `BindingDesc` gained `stride`
  (storage bindings only, at most 2048; Vulkan ignores it). DX12 refuses a storage binding without one; every backend refuses an offset that is not a
  multiple of it. The renderer's object and light bindings give it, and the light slices are now aligned to the lcm of the storage alignment and 48.
  Reflection records it too (`ReflectedBinding::stride`, reflection blob version 2, every committed `*.refl.inc` re-minted; the SPIR-V and DXIL
  are unchanged), so layouts built from reflection have it; the minter refuses `ByteAddressBuffer`, which would need a raw view.
- **Model.** Each bind group's CBV/SRV/UAV descriptors sit together in a CPU heap (power-of-two blocks, 16384 per heap); `SetBindGroup` will copy
  them into the frame's slot of the shader-visible ring (3 slots of 131072), which `BeginFrame` empties when the slot comes round (the frame that
  used it three frames back has finished, since at most three frames are in flight). Samplers: the shader-visible heap holds 2048, so a group's
  samplers are copied once into a table shared by every group with the same samplers in the same order, keyed by the samplers' handles. Root
  signatures (1.1) per pipeline layout: a view table and a sampler table per group, registers as the bindings in `space<slot>`, root constants at
  `b0, space15`, cached by the layouts' identities (equal content, equal id), the push range and compute.
- A build flake again: the non-unity tree's link of `renderer_tests` failed with `LNK1236: corrupt or invalid COFF sections` on
  `dx12_device_later.cpp.obj`, as on Day 165; deleting the object and rebuilding fixed it (filed in `KNOWN-ISSUES.md`).

## For the Linux run

Shared: `engine/rhi/desc.h/.cpp` (`BindingDesc::stride`), `engine/rhi/null/null_device.cpp`, `engine/renderer/{material,lighting,frame_data}.cpp`,
`engine/renderer/shader_reflection.h/.cpp` (version 2, the stride), `tools/shader_mint/reflect_json.cpp`, every `shaders/*.refl.inc` and `*.perm.inc`
(re-minted on Windows; the Linux `slangc` must mint them byte-identical, which `shaders_check` tests), `tests/renderer/shader_reflection_test.cpp`,
`tests/shader_mint/d3d12_conventions_test.cpp`, the guides `docs/guide/rhi.md` and `rhi-shaders.md`, the reference and the lint baseline. Run: both
unity modes, `--affected` with asan and tsan, `ctest -L docs`.

## Runs

Windows PC:

- `scripts/test-phase.sh --affected`: `ran 1237 of 2418 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 202 s; the last full run took 13 s`.
- `scripts/test-phase.sh --affected --flavor asan`: `ran 916 of 2418 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 77 s; the last full run took 13 s`.
- `rhi_tests "[dx12]"`: 23 test cases, 558 assertions, the debug layer silent. All three trees build clean; `cmake --build build-msvc --target docs`:
  51 samples, 0 failed; lint 4367 of 4367 documented.
- Linux: not run; pending until the phase close (Day 174).

## Post-implementation code review

`/code-review high` on the day's diff: 8 findings, 6 fixed (each with a regression test that fails without the fix: checked by taking the fix
out), 2 declined.

- **Fixed:** shared sampler tables were keyed by the samplers' CPU descriptor addresses; a new sampler reuses a destroyed one's descriptor once it is
  collected, and a group made with it would then share an older group's table and sample with the old state. Keyed by the handles now (test:
  "A DX12 sampler table is not shared with a later sampler that reuses a destroyed sampler's descriptor").
- **Fixed:** a uniform range above 64 KiB (D3D12's largest constant-buffer view) reached D3D12; refused now, as Vulkan refuses one above its limit.
- **Fixed:** a storage range shorter than one element made a structured view of zero elements (the debug layer reported it); refused now.
- **Fixed:** the 256-sampler cap per group was checked only when a group was made; the layout is refused now.
- **Fixed:** layouts built from reflection had no stride, so a material with a storage buffer would have failed on DX12 only; reflection records it.
- **Fixed:** the reflection merge's conflict message did not name the strides that differ.
- **Declined (checked on Day 168):** CBV and SRV ranges are flagged `DATA_STATIC_WHILE_SET_AT_EXECUTE`, root signature 1.1's default for them; a pass
  that writes a texture still named by a set table would break it. Day 168's GPU-based validation run over the command-list tests is where it shows;
  `DATA_VOLATILE` is the fallback.
- **Declined:** sampler de-duplication, the layout identities and the root signature cache are linear searches; the counts are tens, as on Vulkan.
