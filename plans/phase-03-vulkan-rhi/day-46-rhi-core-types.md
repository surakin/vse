# Phase 3 / Day 46 — Core Types: Handles, Formats, Descriptors

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 45.

Header-only data types shared by every backend. No GPU needed; everything is unit-testable.

## Tasks

- [x] `engine/rhi/handle.h`: generation-checked handle template `Handle<Tag>` (`u32 index`, `u32 generation`, `IsValid`, equality, a distinct tag type per resource kind: `BufferHandle`, `TextureHandle`, `SamplerHandle`, `ShaderHandle`, `PipelineHandle`, `BindGroupLayoutHandle`, `BindGroupHandle`); the invalid handle is all-zero, generation starts at 1.
- [x] `engine/rhi/format.h`: the `Format` enum (the formats Phases 3, 4 and 7 need: R8/RG8/RGBA8 UNORM and sRGB, BGRA8 UNORM/sRGB, R16F/RG16F/RGBA16F, R32F/RG32F/RGB32F/RGBA32F, R32_UINT, D32F, D24S8 or D32S8 per capability, BC1/BC3/BC5/BC7 reserved) with `FormatInfo(Format)` giving bytes per texel/block, block dimensions, channel count, `IsDepth`, `IsSrgb`, `IsCompressed`; table-driven.
- [x] `engine/rhi/enums.h`: bitmask enums `BufferUsage` (vertex, index, uniform, storage, copy-src, copy-dst, indirect), `TextureUsage` (sampled, storage, color-attachment, depth-attachment, copy-src, copy-dst), `MemoryLocation` (`GpuOnly`, `CpuToGpu`, `GpuToCpu`), `ResourceState` (the portable state set: undefined, vertex-buffer, index-buffer, uniform-buffer, shader-read, shader-write, copy-src, copy-dst, color-attachment, depth-attachment, depth-read, present), `PrimitiveTopology`, `CullMode`, `FrontFace`, `CompareOp`, `BlendFactor`/`BlendOp`, `FilterMode`, `AddressMode`, `IndexType`, `ShaderStage` bitmask, `VertexFormat`. Type-safe flag helper (`operator|`, `Has()`), no raw integer masks.
- [x] Descriptor structs with defaults and `Validate()` returning a reason: `BufferDesc` (size, usage, location, debug name), `TextureDesc` (dimension 2D/3D/cube-reserved, width/height/depth, mips, array layers, format, usage, samples), `SamplerDesc`, `ShaderDesc` (stage, word pointer + count, entry name), `BindGroupLayoutDesc` (binding index, type uniform/storage/sampled-texture/sampler/storage-texture, stages, count), `GraphicsPipelineDesc` (shaders, vertex layout, topology, raster/blend/depth state, color/depth attachment formats, bind group layouts, push constant range), `ComputePipelineDesc`.
- [x] `DeviceCaps` struct (limits the upper layers need: max texture size, max bound groups, push constant bytes, uniform/storage offset alignments, supported depth format, timestamp period, `supports_bindless` reserved flag) — plain data filled by backends.
- [x] Tests (`rhi_tests`): handle equality/invalidity and generation inequality; `FormatInfo` for every format against hand-computed byte sizes; flag operators; every descriptor `Validate()` accepts a good default and rejects each documented bad field (zero size, mip count too large for the extent, depth format used as color, vertex binding gaps, ...) with a stable message prefix.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** every public symbol carries `///` comments with the labels the strict `rhi` module requires; `docs/guide/rhi.md` gains the "Handles and descriptors" section.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** handles, formats, enums and descriptors are header-only, fully validated by unit tests, and contain no backend-specific names.

## Result

- `engine/rhi/{handle,format,enums,limits,caps,desc}.h` + `desc.cpp`: handles (7 kinds), `Format` (24 formats + layout table, `FormatRowBytes/SliceBytes`), flag enums with `|`/`&`/`Has`/`Any`, all enums, `Span<T>`, descriptors with `Validate()` returning a static reason or null, `DeviceCaps`. 20 test cases / 722 assertions, plain and ASan; every `engine/rhi/*.h` is also compiled alone by a generated TU per header.
- Docs: `rhi` module 328/328 documented, lint baseline zero findings; guide section "Handles and descriptors" with a runnable sample.
- Deviation: the enums header is `enums.h`, not `types.h` (see review finding 1).

## Post-implementation code review

`/code-review high` found 8 issues:

1. `rhi/types.h` shadowed the foundation `types.h` for every `#include "types.h"` inside `engine/rhi/`, so `handle.h`/`types.h` were not self-contained (worked in tests only by include order) — **fixed**: renamed to `enums.h`; regression: a generated header-only TU per public header (`header_check_*.cpp`), confirmed to fail with the old shadowing file present.
2. `GraphicsPipelineDesc::Validate` could abort in `FormatInfo` on a garbage format, contradicting "never by aborting" — **fixed** (range-checked `IsKnownFormat`) with tests.
3. Many descriptor enums/masks were not range-checked (sampler filters/addresses/border, topology, cull, front face, depth compare, blend factors/ops, write mask) — **fixed** with tests.
4. "Vertex binding has no attributes" rejected a layout all three APIs allow — **fixed**: rule removed, test inverted to assert the layout is accepted.
5. `kMaxBindGroups`/`kMaxPushConstantBytes` duplicated `DeviceCaps` defaults — **fixed**: constants moved to `rhi/limits.h`, caps defaults reference them (`max_color_attachments` stays 4: the Vulkan-guaranteed minimum, whereas the descriptor ceiling is 8). Compute push-constant stages: **declined** as a change — compute pipelines have one stage, so the range is implicitly compute; documented on the struct.
6. Documentation said "all-zero is invalid" but validity is `generation == 0` — **fixed in the docs**: validity is `IsValid()`, not comparison with a default handle; making `operator==` collapse all invalid handles was **declined** (a hand-built `{5, 0}` is a programming error).
7. `BufferDesc::Validate` does not reject usage combinations such as a GPU-only buffer with only `CopySrc` — **declined**: such combinations are legitimate (for example a compute-written buffer that is later copied out) and a stricter rule would reject real uses.
8. STATUS.md / review record missing — **fixed** in this commit.

