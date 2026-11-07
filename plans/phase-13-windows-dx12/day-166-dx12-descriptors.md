# Phase 13 / Day 166 — DX12 III: Descriptor Heaps, Root Signatures, Bind Groups

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 165; the spec's root-signature model (Day 147).

Bind groups on D3D12: CPU descriptor heaps, a root signature per pipeline layout, and descriptor copies into per-frame shader-visible heaps at `SetBindGroup`.

## Tasks

- [ ] **CPU descriptor heaps:** allocators for CBV/SRV/UAV, sampler, RTV and DSV (free lists, sized from the spec, growth by adding heaps), with views created when a
      resource is created and freed with deferred destruction.
- [ ] **Samplers:** every RHI sampler state mapped (`D3D12_SAMPLER_DESC`), border colors as the RHI allows, anisotropy; heap samplers (static samplers only if
      the spec chose them for fixed cases).
- [ ] **Bind group layouts → root signatures:** one descriptor table per bind group (CBV/SRV/UAV range plus a sampler table) and root constants for push constants (the
      object index needs no root parameter: spec decision 9; the layout is the spec's section 5); serialized with root signature 1.1 and cached by layout hash; the register/space assignment matches the minted
      reflection (a test checks every committed shader's reflection against the layout it is used with).
- [ ] **Bind groups:** a bind group stores its CPU descriptors; `SetBindGroup` copies them into the current frame's shader-visible ring (`CopyDescriptorsSimple`)
      and sets the table; the ring is per frame in flight and recycled by fence; running out of ring space is an error with sizes, not a hang.
- [ ] **Measure `SetBindGroup`** with `rhi_bench` on DX12 versus Vulkan on the same PC (Day 161 baseline) and record both; if DX12 is much slower, try caching a
      group's GPU copy across frames when unchanged (and record the result) before moving on.
- [ ] **Tests** (`tests/rhi/dx12_bindings_test.cpp`, phase 13; the shared bindings tests parameterized by backend): every binding type reaches a compute shader
      and reads back correctly, many groups per frame, ring wrap-around across 3 frames, root signature cache hits, debug layer silent.
- [ ] **Runs:** Windows `--affected` plain and asan (Windows files only, unless shared tests changed: then Linux too).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: descriptors freed while a frame in flight still references their copy, ring indices across
      wrap-around, table offsets per heap type.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every bind group layout becomes a cached root signature, bind groups reach shaders through per-frame shader-visible heaps, and the `SetBindGroup` cost is
measured against Vulkan on the same machine.
