# Phase 3 / Day 46 — Core Types: Handles, Formats, Descriptors

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 45.

Header-only data types shared by every backend. No GPU needed; everything is unit-testable.

## Tasks

- [ ] `engine/rhi/handle.h`: generation-checked handle template `Handle<Tag>` (`u32 index`, `u32 generation`, `IsValid`, equality, a distinct tag type per resource kind: `BufferHandle`, `TextureHandle`, `SamplerHandle`, `ShaderHandle`, `PipelineHandle`, `BindGroupLayoutHandle`, `BindGroupHandle`); the invalid handle is all-zero, generation starts at 1.
- [ ] `engine/rhi/format.h`: the `Format` enum (the formats Phases 3-5 need: R8/RG8/RGBA8 UNORM and sRGB, BGRA8 UNORM/sRGB, R16F/RG16F/RGBA16F, R32F/RG32F/RGB32F/RGBA32F, R32_UINT, D32F, D24S8 or D32S8 per capability, BC1/BC3/BC5/BC7 reserved) with `FormatInfo(Format)` giving bytes per texel/block, block dimensions, channel count, `IsDepth`, `IsSrgb`, `IsCompressed`; table-driven.
- [ ] `engine/rhi/types.h`: bitmask enums `BufferUsage` (vertex, index, uniform, storage, copy-src, copy-dst, indirect), `TextureUsage` (sampled, storage, color-attachment, depth-attachment, copy-src, copy-dst), `MemoryLocation` (`GpuOnly`, `CpuToGpu`, `GpuToCpu`), `ResourceState` (the portable state set: undefined, vertex-buffer, index-buffer, uniform-buffer, shader-read, shader-write, copy-src, copy-dst, color-attachment, depth-attachment, depth-read, present), `PrimitiveTopology`, `CullMode`, `FrontFace`, `CompareOp`, `BlendFactor`/`BlendOp`, `FilterMode`, `AddressMode`, `IndexType`, `ShaderStage` bitmask, `VertexFormat`. Type-safe flag helper (`operator|`, `Has()`), no raw integer masks.
- [ ] Descriptor structs with defaults and `Validate()` returning a reason: `BufferDesc` (size, usage, location, debug name), `TextureDesc` (dimension 2D/3D/cube-reserved, width/height/depth, mips, array layers, format, usage, samples), `SamplerDesc`, `ShaderDesc` (stage, word pointer + count, entry name), `BindGroupLayoutDesc` (binding index, type uniform/storage/sampled-texture/sampler/storage-texture, stages, count), `GraphicsPipelineDesc` (shaders, vertex layout, topology, raster/blend/depth state, color/depth attachment formats, bind group layouts, push constant range), `ComputePipelineDesc`.
- [ ] `DeviceCaps` struct (limits the upper layers need: max texture size, max bound groups, push constant bytes, uniform/storage offset alignments, supported depth format, timestamp period, `supports_bindless` reserved flag) — plain data filled by backends.
- [ ] Tests (`rhi_tests`): handle equality/invalidity and generation inequality; `FormatInfo` for every format against hand-computed byte sizes; flag operators; every descriptor `Validate()` accepts a good default and rejects each documented bad field (zero size, mip count too large for the extent, depth format used as color, vertex binding gaps, ...) with a stable message prefix.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** every public symbol carries `///` comments with the labels the strict `rhi` module requires; `docs/guide/rhi.md` gains the "Handles and descriptors" section.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** handles, formats, enums and descriptors are header-only, fully validated by unit tests, and contain no backend-specific names.
