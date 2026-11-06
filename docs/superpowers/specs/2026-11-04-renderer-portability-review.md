# Renderer Portability Review (Phase 4, Day 84)

Status: done at the end of Phase 4. Companion to `2026-11-04-renderer-design.md` and to the Phase 3 review,
`2026-11-04-rhi-portability-review.md`, which covers everything below the renderer (the RHI itself).

The question: **is the renderer, and are its shaders, Vulkan in disguise?** The renderer never names Vulkan (`rhi_boundary_check`
enforces it), so this review looks one level up: the render graph's barrier model, the descriptor-set convention the
material system imposes on shaders, the formats the deferred pipeline needs, and the shaders themselves. As in the RHI
review, no D3D12 or Metal code exists, so every verdict is a prediction from knowledge of those APIs that Phases 10 and 11
will test. The `shaders_portability` ctest is the one check that already runs: it compiles every shader and every
permutation key to HLSL and Metal Shading Language with `slangc` (76 outputs).

Verdicts: **Fits** (the backend implements it directly), **Shim** (the interface stays, the backend does extra work, named in
the row) or **Change** (the code needed a change; each is listed in "Changes made now"). Nothing found forces a redesign of a
built layer.

## 1. Item by item

| Item | Vulkan (built) | Direct3D 12 | Metal | Verdict |
|---|---|---|---|---|
| **Render graph barriers.** `Compile` computes, per resource, the state before and after every use and emits `Barrier(resource, before, after)` ahead of the pass; imports start and end in declared states | the RHI maps each state to stage, access and layout | `ResourceState` maps to legacy `D3D12_RESOURCE_STATES` or to enhanced-barrier layout/access/sync triples; a transition is a `ResourceBarrier` or `Barrier` call. Split barriers are an optimization the graph does not emit | Hazards on tracked resources are tracked by the driver; the graph's barriers become no-ops, and cross-pass ordering inside one command buffer needs `MTLFence`s or untracked heaps only if the backend opts out of tracking | Fits. The explicit-state model is the strictest of the three, so D3D12 consumes it directly and Metal ignores what it does not need |
| **Pass ordering and parallel recording.** `ExecuteParallel` records each pass into its own list on a scheduler participant and submits the lists in graph order | one command buffer per pass, one pool per thread | one command list per pass with a per-thread, per-frame allocator, `ExecuteCommandLists` in order | one `MTLCommandBuffer` per list, committed in order; `MTLParallelRenderCommandEncoder` is not needed because each pass is its own encoder | Fits |
| **Descriptor sets 0/1/2.** Set 0 is the per-frame uniform block, set 1 the material (block, textures, samplers), set 2 the per-object storage buffer; pipelines are built against the three layouts | `VkDescriptorSetLayout` x3 | one root signature per pipeline layout with a descriptor table per set (CBV/SRV/UAV and sampler tables), so 3 sets use 6 table slots plus root constants (budget: 64 DWORDs). Each table is one DWORD | one argument buffer per set; the shader sees them as `[[buffer(n)]]` | Fits, with one point to settle in the shader path: slangc flattens sets to buffer indices on Metal (frame `[[buffer(0)]]`, objects `[[buffer(1)]]` in `gbuffer.vert`), so the Metal backend must read the index from the compiled reflection, not assume `set` equals `buffer`. Filed for Phase 11 |
| **Per-object index.** Object `i` is drawn with `first_instance = i` and the shader reads `SV_VulkanInstanceID` | `gl_InstanceIndex` | **No equivalent**: HLSL's `SV_InstanceID` excludes the base instance and there is no shader-visible base instance without a root constant | `[[instance_id]]` excludes the base instance; `[[base_instance]]` exists (macOS 10.11+/iOS 9+) and is added | **Shim**, filed since Day 74: D3D12 passes the first object index as a root constant (or a second vertex stream) and the shader adds it; the shader source then needs a define for the delivery. Cheap now, but it touches every material shader and so belongs with the backend that needs it (Phase 10) |
| **Clip space Y.** `PerspectiveVulkan` bakes the Y flip into the projection | native (Y down) | NDC Y up, so the same matrix renders upside down | Y up, same | **Shim** in the projection (a `PerspectiveD3D` variant, or a flip in the frame block), not in the shaders: the emitted HLSL and Metal write `SV_Position` unchanged (verified, see README). Depth 0..1 is the same on all three |
| **Matrix layout.** `Mat4` is column-major, copied byte for byte into std140 blocks; shaders use `mul(M, v)` | column-major decoration | slangc emits `#pragma pack_matrix(column_major)` with the same `mul(M, v)` (verified on `gbuffer.vert`); constant buffers hold the same bytes | slangc emits column-major matrix storage structs (`_MatrixStorage_float4x4_ColMajor...`); same bytes | Fits |
| **G-buffer formats.** Albedo `RGBA8_SRGB`, normal `RG16_Float` (octahedral), material `RGBA8_UNorm`, depth `D32_Float`, HDR `RGBA16_Float`; all `ColorAttachment` + `Sampled` | supported | all are mandatory render-target and shader-resource formats at feature level 11_0 and up; `RGBA8_SRGB` needs the `_SRGB` DXGI format on the render target view | all supported on macOS GPUs; on Apple-silicon GPUs `Depth32Float` is supported but memory-hungry (tile memory budget), see below | Fits. `CheckGBufferFormats` asks the device for each one at start-up |
| **MRT count.** The geometry pass writes 3 color attachments plus depth | `maxColorAttachments` is at least 4 | 8 | 8 on A11 and later and on macOS, 4 on older iOS GPUs | Fits, with one change: the device check now also tests `Caps().max_color_attachments` (see "Changes made now") |
| **Depth sampled by the lighting pass.** The depth buffer is a texture the lighting pass reads | one image, two views | needs a **typeless** resource (`R32_TYPELESS`) with a `D32_FLOAT` depth view and an `R32_FLOAT` shader view | `Depth32Float` is directly sampleable | Shim in the RHI backend (already filed in the RHI review, section 2); the renderer asks only for `DepthAttachment | Sampled` and never sees the format split |
| **Depth reconstruction.** World position is rebuilt from depth and the inverse view-projection | depth 0..1, Y down | depth 0..1, Y up | same | Fits: the shader uses NDC from the matrices it is given, so it follows whichever projection the backend uses (a golden test pins it on Vulkan) |
| **Light volumes.** One additive, front-face-culled, scissored sphere per light, generated from `SV_VulkanVertexID` | `gl_VertexIndex` | `SV_VertexID` (base vertex is 0 for non-indexed draws, so `SV_VulkanVertexID` and `SV_VertexID` agree) | `[[vertex_id]]` | Fits. Front-face culling needs the winding flip in the D3D12 projection to be consistent with the Y flip above; the backend's golden cube tests catch it |
| **Blending, scissor, per-light dynamic state.** | native | `OMSetBlendState` is baked into the PSO and the scissor is dynamic | same | Fits |
| **Transient resources.** Pooled by description, no memory aliasing | each its own image | placed resources in a heap would allow aliasing; not needed for correctness | `MTLHeap` aliasing is possible, same | Fits (aliasing is an optimization filed in KNOWN-ISSUES) |
| **Tile-based GPUs (Phase 19: iOS, Android).** The deferred pipeline writes the G-buffer to memory and reads it in a second pass | works, slowly on tilers | n/a | Apple GPUs can keep the G-buffer in tile memory (`memoryless` attachments, programmable blending) if the geometry and lighting passes are one render pass | Shim, later: merging the passes is a graph feature (adjacent passes that read what the previous wrote at the same pixel), not a renderer rewrite. Filed for Phase 19, where a forward path may be the better answer on mobile |
| **Compute shaders and buffer sizes.** `fill.comp` and `saxpy.comp` call `RWStructuredBuffer.GetDimensions` | native | native | **Not available** in Metal Shading Language: slangc rejects both ("unavailable features in entry point") | Change deferred: both are RHI compute test shaders, listed with the reason in `shaders/portability.cmake`. The Metal backend passes the element count as a constant and these two shaders get that change with it. No renderer shader uses it |

## 2. What the shaders look like on the other targets

`ctest -R shaders_portability` (needs `slangc`, skipped without it) compiles all 24 shaders and the permutation keys of the four
permuted ones to HLSL and Metal; it fails on any error, and on a shader that is listed as unavailable but now compiles. If `dxc` is
installed it also compiles the HLSL to DXIL; it is not installed on the development machine, so that step has not run (see
`KNOWN-ISSUES.md`). The Metal compiler needs macOS and is not run.

- Matrix layout: see the table. The `-matrix-layout-column-major` flag the engine already passes is what keeps `mul(M, v)` and
  the byte layout identical across targets.
- Clip-space Y: not flipped by the compiler (the SPIR-V-only flag `-fvk-invert-y` does not apply), so the flip stays in the
  projection.
- Metal renames the entry point `main` to `main_0` with a warning; the backend must look up the renamed entry or the shaders must
  get real entry names. The RHI already carries an entry name in `ShaderDesc`.

## 3. Changes made now

1. **`CheckGBufferFormats` also checks the device's color attachment limit.** The geometry pass writes three colors; a device that
   allows fewer (older mobile GPUs allow 4, a stricter profile could allow less) used to pass the format check and then fail
   every material pipeline with a log line per pipeline. It now fails at start-up with
   "G-buffer: the device allows N color attachments and the geometry pass writes 3". Tests: `gbuffer_test.cpp` (the check with a
   2-attachment and a 3-attachment null device) and `renderer_fault_test.cpp` (the demo frame refuses such a device before building
   a pass).
2. **`shaders_portability`** (above), with `shaders/README.md` updated.

## 4. Filed for later phases

- **Object index delivery on D3D12** (Phase 10): a root constant or per-draw vertex stream added to the base instance, behind a
  shader define. Needs a change in every material shader's vertex stage and in `GBufferPass`'s draw recording.
- **Metal buffer-slot mapping** (Phase 11): read slots from the compiled reflection.
- **Projection variants** (Phases 10-11): a `PerspectiveD3D` (Y up) or a per-backend flip; the cube and demo golden images are
  the oracle.
- **Passes sharing a tile** (Phase 19): graph-level merging for Apple/Android tilers.
- **`fill.comp`/`saxpy.comp` on Metal** (Phase 11): pass the length instead of `GetDimensions`.
- **`dxc` DXIL check** (Phase 10): the optional step in `shaders_portability` has not run on a machine with `dxc`.

Everything in this list is a backend or shader-path task; none changes a renderer interface. The renderer's public API
(`Renderer`, `RenderGraph`, `MaterialSystem`, the passes) has no Vulkan concept in it, and the single place that carries a
Vulkan convention is the projection function, which is a function and not a type.
