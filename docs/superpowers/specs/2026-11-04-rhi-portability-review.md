# RHI Portability Review (Phase 3, Day 67)

Status: done at the end of Phase 3. Companion to `2026-11-04-rhi-design.md` section 13.

The question this review answers: **is the RHI Vulkan in disguise?** Every type and call in `engine/rhi/*.h` was walked
against Direct3D 12 (D3D12) and Metal, from knowledge of those APIs; no D3D12 or Metal code exists yet, so each verdict
is a prediction that the Phase 12 and Phase 13 backends will test. The golden images (`tests/rhi/golden/`) and the
analytic probes next to them are the portability oracle: a new backend must reproduce them.

Each row ends in one of three verdicts:

- **Fits** — the backend implements it directly.
- **Shim** — the interface stays; the backend does extra work (named in the row).
- **Change** — the interface needed a change. Every change found is listed in "Changes made now"; nothing is left that
  would force a redesign of a built layer.

## 1. Concept map

| RHI concept | Vulkan (built) | Direct3D 12 | Metal | Verdict |
|---|---|---|---|---|
| `Device`, `CreateDevice(Backend)` | `VkInstance` + `VkDevice`, one graphics queue | `ID3D12Device` + a direct queue (DXGI factory for adapter choice) | `MTLDevice` + `MTLCommandQueue` | Fits |
| Opaque generation-checked handles | slot + generation in `HandlePool` | same; the pool is backend-neutral | same | Fits |
| Deferred destruction (`Retire`/`CollectRetired` against the completed frame) | frame timeline decides when `vkDestroy*` runs | release after the fence reaches the frame (the standard "deferred release queue") | release after the shared event; ARC keeps the object alive while command buffers use it | Fits |
| `BufferDesc` (`size`, `usage`, `location`) | `VkBuffer` + suballocated memory | committed or placed `ID3D12Resource`, heap type from `location` | `MTLBuffer`, storage mode from `location` | Fits (see rows 4a, 4b) |
| `MemoryLocation::GpuOnly / CpuToGpu / GpuToCpu` | device-local / host-visible (coherent where available) / host-cached | `DEFAULT` / `UPLOAD` / `READBACK` heaps | `Private` / `Shared` (or `Managed` on a discrete Mac GPU) | Fits |
| 4a. `GpuToCpu` buffers are copy destinations only | enforced by `BufferDesc::Validate` | **required**: a `READBACK` resource is permanently `COPY_DEST` | no restriction | Fits (the rule was written for D3D12 on Day 47) |
| 4b. `CpuToGpu` buffers have a fixed state and never need barriers | no barrier needed | **required**: an `UPLOAD` resource is permanently `GENERIC_READ` | no barrier | Fits (same) |
| `FlushMappedBuffer` | flushes non-coherent ranges | no-op (upload heaps are write-combined and coherent) | `didModifyRange` on a `Managed` buffer | Fits (the call exists for exactly this spread) |
| `MapBuffer`/`UnmapBuffer` | persistent mapping | `Map`/`Unmap` (persistent mapping is legal) | `contents()` | Fits |
| `TextureDesc` (2D, 3D, mips, layers, `samples = 1`) | `VkImage` | `ID3D12Resource` texture | `MTLTexture` | Fits |
| Depth texture that is also sampled | one `VkImage`, two views | **needs a typeless resource** (`R32_TYPELESS`) with a `D32_FLOAT` view for the DSV and `R32_FLOAT` for the SRV | `Depth32Float` is directly sampleable | Shim (typeless format chosen from `Format` + usage in the backend) |
| `Format` set (RGBA8/BGRA8 UNorm and sRGB, R8, RG8, float formats, D32) | table in `vk_format.cpp` | `DXGI_FORMAT` table | `MTLPixelFormat` table | Fits (the set was chosen as the intersection) |
| `FormatSupport(format, usage)`, `CanGenerateMips(format)` | queried from the physical device | `CheckFeatureSupport(FORMAT_SUPPORT)` | family tables | Fits |
| `GenerateMips` | blit chain | **no built-in**: a compute shader per level | `generateMipmaps` on a blit encoder | Shim (D3D12 uses a compute pass; `CanGenerateMips` already lets a caller ask) |
| `SamplerDesc` (filters, address modes, compare, anisotropy, LOD range) | `VkSampler` | sampler descriptor in a sampler heap | `MTLSamplerState` | Fits |
| `AddressMode::ClampToBorder`, `BorderColor` | native | static or dynamic border colors | **not available on iOS family GPUs** (macOS only) | Shim/limit: iOS backend rejects `ClampToBorder` with an invalid handle and a log; recorded as a Phase 13 note, no interface change (a caps flag would add nothing the log does not) |
| `ShaderDesc` (SPIR-V words, stage, entry) | `VkShaderModule` | DXIL bytecode | MSL source or a metallib | **Open** — see section 3 |
| `BindGroupLayout` | `VkDescriptorSetLayout` | a root-signature descriptor table (one CBV/SRV/UAV table and one sampler table per group) | argument-buffer layout | Fits |
| `BindGroup` | `VkDescriptorSet` | descriptor-heap ranges copied (`CopyDescriptors`) into the shader-visible heaps at bind time | argument buffer | Shim (D3D12 may bind only one shader-visible CBV/SRV/UAV heap and one sampler heap per list: the backend keeps bind groups in a CPU-only heap and copies into a per-frame ring at `SetBindGroup`; a transient group is that ring slot) |
| `BindGroupEntry::size` arbitrary | range bound as given | a CBV range must be a multiple of 256 bytes | none | Shim (uniform buffers are allocated with a size rounded up to 256 and the view range rounded; offsets are already 256-aligned via `min_uniform_buffer_offset_alignment`) |
| Push constants (`SetPushConstants`, 128 bytes) | push constants | root constants (32 DWORDs of the 64-DWORD root signature; 4 groups x 2 tables = 8 more, total 40) | `setBytes` | Fits |
| `GraphicsPipelineDesc`, `ComputePipelineDesc` | `VkPipeline` + layout | PSO + root signature (the root signature is derived from the layouts, as `VkPipelineLayout` is) | render / compute pipeline state | Fits |
| Pipeline cache on disk | `VkPipelineCache` blob | `ID3D12PipelineLibrary` | `MTLBinaryArchive` | Fits (`pipeline_cache` is a hint; every backend may ignore it) |
| Compute workgroup size | in the SPIR-V (`LocalSize`) | `[numthreads]` in the shader | **given at dispatch** (`threadsPerThreadgroup`) | Shim (Metal reads the size from the shader's reflection when the pipeline is created and stores it) |
| `Dispatch(x, y, z)` | `vkCmdDispatch` | `Dispatch` | `dispatchThreadgroups` | Fits |
| Dynamic rendering (`BeginRendering(RenderingDesc)`, no render-pass objects) | `vkCmdBeginRendering` | `BeginRenderPass` / `OMSetRenderTargets` | `MTLRenderPassDescriptor` | Fits |
| `LoadOp`, `StoreOp` (`DontCare` discards) | same | render-pass beginning/ending access (`DISCARD`, `NO_ACCESS`) | load/store actions | Fits |
| Dynamic viewport and scissor | dynamic state | `RSSetViewports` / `RSSetScissorRects` | `setViewport` / `setScissorRect` | Fits |
| Clip space: Y down, depth 0..1 (the app's projection flips Y) | native | NDC **Y up**, depth 0..1 | NDC Y up, depth 0..1 | Shim in the shader path (see section 2) |
| `FrontFace`, `CullMode` | native | `FrontCounterClockwise`, `CullMode` | `frontFacingWinding`, `cullMode` | Fits, verified when the golden cube renders |
| Vertex bindings/attributes, `VertexFormat`, `IndexType` | `VkVertexInputState` | input layout | vertex descriptor | Fits |
| `PrimitiveTopology` (list, strip, line, point) | native | topology type + `IASetPrimitiveTopology` | `MTLPrimitiveType` | Fits (strips use no primitive restart in the RHI) |
| Blend state (factors, ops, write mask) | native | `D3D12_RENDER_TARGET_BLEND_DESC` | color attachment blend | Fits (no dual-source blending, no logic ops, no blend constant: all deliberately absent) |
| Depth state (`CompareOp`, write enable) | native | depth-stencil desc | `MTLDepthStencilState` | Fits (no stencil in Phase 3) |
| `Barrier(resource, before, after)` with `ResourceState` | one table: state to (stage, access, layout) | state to `D3D12_RESOURCE_STATES` (or enhanced-barrier layout/access/sync) | **mostly implicit**: hazards on tracked resources are tracked by the driver; untracked heaps need an explicit `memoryBarrier` / fence | Shim (Metal turns a barrier into a no-op, a `memoryBarrier`, or an encoder split, depending on resource tracking mode) |
| `ResourceState::Undefined` as "contents discarded" | `UNDEFINED` old layout | `COMMON` (or enhanced-barrier `LAYOUT_UNDEFINED`) | nothing to do | Fits |
| `ResourceState::Present` | `PRESENT_SRC` layout | `PRESENT` state | nothing to do | Fits |
| `ResourceState::DepthRead` | depth-read-only layout | `DEPTH_READ` (plus `PIXEL_SHADER_RESOURCE` when sampled) | nothing to do | Fits |
| `CopyBuffer`, `CopyBufferToTexture`, `CopyTextureToBuffer` | `vkCmdCopy*` | `CopyBufferRegion`, `CopyTextureRegion` with a placed footprint | blit encoder | **Change** (copy alignment, section 2) |
| Command lists, one thread each | `VkCommandBuffer` from the calling thread's pool | `ID3D12GraphicsCommandList` + a per-thread, per-frame allocator | `MTLCommandBuffer` per thread from the queue | Fits |
| Copies, barriers and dispatches outside render passes; draws inside | what Vulkan requires | what D3D12 allows | **what Metal requires**: copies, compute and render are different encoders, so the backend ends one encoder and opens the next | Fits (the rule that passes do not mix with copies and dispatches was chosen for this) |
| `Submit(lists, n)` returns a fence value | timeline semaphore signal | `ID3D12Fence` signal after `ExecuteCommandLists` | `MTLSharedEvent` signal on the last command buffer | Fits |
| `WaitFence(value, timeout)`, `CompletedFence()` | timeline wait / counter | `SetEventOnCompletion` / `GetCompletedValue` | `waitUntilSignaledValue` / `signaledValue` | Fits |
| `BeginFrame`/`EndFrame`, frames in flight | frame number against the timeline | same | same | Fits |
| `UploadBuffer`, `UploadTexture`, `ReadbackTexture`, `ReadbackBuffer`, `UniformRing` | staging ring + transfer copies | upload-heap ring + copy on the direct queue | shared buffer + blit | Shim (row repacking: `UploadTexture` takes the caller's pitch and `ReadbackTexture` returns tight rows, so the D3D12 backend repacks to and from 256-byte pitches on the CPU) |
| `Swapchain` (`Acquire`, `Present`, `Resize`, `CurrentTexture`) | `VkSwapchainKHR` | DXGI flip-model swapchain | `CAMetalLayer` | Fits, three shims below |
| Swapchain acquire | `vkAcquireNextImageKHR` | none: `GetCurrentBackBufferIndex`, throttled by the frame-latency waitable object | `nextDrawable()` (blocks) | Shim (`Acquire` is the wait; the 0.5 s timeout that reports `Minimized` maps to the waitable's timeout) |
| Swapchain `Resize` | recreate the swapchain | `ResizeBuffers` after **every** back-buffer reference is released and the GPU is idle | set `drawableSize` | Shim (the D3D12 backend waits idle in `Resize`, which is already allowed to be slow) |
| sRGB swapchain format (`ImageFormat()` reports an sRGB format) | native | flip model forbids an sRGB swapchain format: create the buffers as UNorm and the render-target views as sRGB | `BGRA8Unorm_sRGB` is allowed | Shim (the view carries the sRGB format) |
| `PresentMode` (`Fifo`, `Mailbox`, `Immediate`) honoured only when offered | native | `Fifo` = sync interval 1; `Immediate` = `ALLOW_TEARING` with interval 0; `Mailbox` has no direct form | `displaySyncEnabled` covers `Fifo`/`Immediate` | Fits (the fallback policy already says "offered or Fifo") |
| `SwapchainStatus::Lost` / `Device::DeviceLost()` | `VK_ERROR_DEVICE_LOST` / surface lost | `DXGI_ERROR_DEVICE_REMOVED` / `GetDeviceRemovedReason` | command-buffer error status | Fits |
| `PushDebugLabel`/`PopDebugLabel`, `debug_name` | debug utils | PIX markers, `SetName` | `pushDebugGroup`, `label` | Fits |
| `DeviceCaps` (limits, alignments, `depth_format`, timestamps, `supports_bindless`) | from `VkPhysicalDeviceLimits` | from the feature level and `CheckFeatureSupport` | from the GPU family | Fits |
| Timestamps (`timestamp_period_ns`, 0 when unsupported) | `timestampPeriod` | `GetTimestampFrequency` | counter sample buffers (tile GPUs sample only at pass boundaries) | Fits (0 is allowed) |
| `FaultInjection`, validation settings | Vulkan backend | D3D12 debug layer (`ID3D12Debug`) | Metal validation layer (environment) | Fits |

## 2. Mismatches in detail

**Clip-space Y.** The RHI follows Vulkan: clip-space Y points down, and the application's projection matrix flips it
(`PerspectiveVulkan` in `game/common/cube_scene.h`). D3D12 and Metal have Y up in clip space. The RHI itself cannot hide
this (a negative-height viewport is legal in Vulkan 1.1 but not in D3D12), so the fix lives where the shader is
translated: SPIRV-Cross's `flip_vert_y` option, or the equivalent in whichever toolchain is chosen, flips `gl_Position.y`
for the D3D12 and Metal outputs. The front-face convention (counter-clockwise, verified on a real device) is stated in
framebuffer space, which is top-left-origin on all three APIs, so it carries over unchanged once the shader flips. The
lit-cube golden image is the test that this was done correctly. **Decision input for the shader-toolchain decision
(open in `PLAN.md` assumption #18):** the translator must flip Y; the engine's shaders may not assume either clip-space
convention.

**Buffer-texture copy alignment (the one interface change).** D3D12's `CopyTextureRegion` with a placed footprint
requires the buffer offset to be a multiple of 512 and the row pitch a multiple of 256. Vulkan requires only the texel
size, and Metal likewise. `BufferTextureCopy` allowed any texel-aligned pitch and treated `0` as "tightly packed", which
would have been illegal on D3D12 for most textures, and every caller written against Vulkan would have broken on the new
backend. Fixed now: `DeviceCaps::buffer_texture_copy_offset_alignment` and `buffer_texture_copy_row_pitch_alignment`
(defaults 512 and 256, the strictest portable values; the Vulkan backend reports the same although Vulkan needs only
the texel size, so code tested on the one real backend works on D3D12 later), enforced by the null backend with
its default caps and by the Vulkan backend. Code that pads to the caps works on every backend; the null device
now catches code that does not.

**Resource states are caller-stated.** `Barrier(resource, before, after)` makes the caller name `before`. D3D12's
legacy barriers need that and the enhanced barriers use it as well; Metal ignores it. The Phase 4 render graph will
know every transition, so the cost of stating `before` falls on one component.

**Bind groups and descriptor heaps.** D3D12's rule of one shader-visible heap per type is the largest structural
difference in the model. The interface survives because a bind group is created ahead of time and bound by handle:
a D3D12 backend can build the descriptors in a CPU heap and copy them at bind time. The cost is a copy per
`SetBindGroup`; the Day 67 baseline (`plans/phase-03-vulkan-rhi/day-67-portability-review-perf-wrapup.md`) records the Vulkan
cost of the same call so the comparison can be made when D3D12 exists.

**Specialization constants, subgroup operations, ray tracing, mesh shaders.** Not in the RHI; each is a Phase 4+ decision
with an interface addition, not a change to what exists.

## 3. The shader boundary (open, and deliberately not changed)

`ShaderDesc` carries SPIR-V words. The design already says other backends "take their own bytecode in this
descriptor". Which form that takes depends on the open shader-toolchain decision:

1. **Translate at cook time** (SPIRV-Cross or a compiler producing DXIL/MSL offline): `ShaderDesc` becomes a
   format-tagged byte blob (`format`, `bytes`, `size`), and the cooker produces one blob per backend. Cheapest at run
   time, most files to ship.
2. **Translate at load time**: `ShaderDesc` keeps SPIR-V and the D3D12/Metal backend links a translator. Runtime
   dependency, but no interface change.

Changing `ShaderDesc` now would be churn through every test, shader include and doc for a decision that has not been
made, so it is recorded here and in `plans/phase-04-renderer-core.md` as an input. Either outcome is a local change
(`ShaderDesc`, `CreateShader`, `ValidateSpirv`'s caller); no other RHI call depends on the shader format.

## 4. Changes made now

| Change | Why | Tests |
|---|---|---|
| `DeviceCaps::buffer_texture_copy_offset_alignment` / `buffer_texture_copy_row_pitch_alignment`; `BufferTextureCopy` documents them; the null and Vulkan backends enforce them (Vulkan reports the strict values on purpose, so the one real backend exercises the contract) | D3D12 copy footprint rules (512 / 256) | `The default device enforces Direct3D 12's copy alignment, so portable code pads its rows`; the Vulkan caps check; the Vulkan buffer/texture copy test uses a 256-byte row; the existing texel-alignment tests run on a unit-alignment device |

## 5. Filed for later phases

For `plans/phase-12-windows-dx12.md`: typeless formats for sampled depth; compute `GenerateMips`; bind-group descriptor
copies into per-frame shader-visible heaps (measure `SetBindGroup`); 256-rounded uniform buffers and CBV ranges;
CPU repacking in `UploadTexture`/`ReadbackTexture`; `ResizeBuffers` after idling; sRGB views on UNorm flip-model
buffers; the clip-space Y flip in the shader path; `Mailbox` falling back to `Fifo`.

For `plans/phase-13-macos-ios-metal.md`: encoder splitting around passes; barriers as no-ops or `memoryBarrier` by
tracking mode; threadgroup size from shader reflection at pipeline creation; `Managed` storage and `FlushMappedBuffer`;
`ClampToBorder` unavailable on iOS; `nextDrawable()` as `Acquire`; the clip-space Y flip.
