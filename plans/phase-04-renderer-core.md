# Phase 4 — Renderer Core

**Depends on:** Phase 3 (Vulkan RHI).
**Goal:** a real renderer built on the RHI — multiple objects, materials,
lighting, depth — not just a single hard-coded draw call.
**Definition of done:** a render graph/pass system draws multiple objects
with basic materials and lighting, with correct depth buffering.

## Decisions (settled 2026-11-04)

- **Shader toolchain: Slang** (`slangc`, offline), chosen for platform portability; GLSL ported on Days 69-70 and removed. Recorded in `PLAN.md` assumption #18.
- **Lighting: deferred** (G-buffer, lighting pass, tonemap). MSAA is not added to the RHI (awkward with deferred; post-process AA is a later add-on).
- **Render graph: generic**, built only on the RHI; it owns every barrier.
- **Deferred RHI features (bindless, indirect draws, async compute): all out** for this phase; revisit with profiling data.

- **Vertex colors (added 2026-11-05, from Phase 7's colored 3D text):** meshes may carry an optional RGBA8 per-vertex color in a second vertex stream; `StandardVertex` is unchanged. Built on Day 78 with a `VERTEX_COLOR` material permutation.

## Days

Each day has its own file of individually checkable sub-tasks, a docs task, an end-of-day `/code-review high` task and a concrete Done condition.

68. [Renderer Design Spec & Build Skeleton](phase-04-renderer-core/day-68-renderer-design-spec.md)
69. [Slang Toolchain & First Shader Ports](phase-04-renderer-core/day-69-slang-toolchain.md)
70. [Port the Remaining Shaders & Retire GLSL](phase-04-renderer-core/day-70-slang-port-complete.md)
71. [Shader Reflection & the Mint Tool](phase-04-renderer-core/day-71-shader-reflection-mint.md)
72. [Shader Permutations](phase-04-renderer-core/day-72-shader-permutations.md)
73. [Renderer Foundation: Camera, Meshes & the Scene List](phase-04-renderer-core/day-73-renderer-foundation.md)
74. [The Material System](phase-04-renderer-core/day-74-material-system.md)
75. [Render Graph I: Declaration & Compilation](phase-04-renderer-core/day-75-render-graph-declare-compile.md)
76. [Render Graph II: Automatic Barriers & Transient Resources](phase-04-renderer-core/day-76-render-graph-barriers-resources.md)
77. [Render Graph III: Execution, Parallel Recording & the Swapchain](phase-04-renderer-core/day-77-render-graph-execute.md)
78. [The G-buffer Geometry Pass](phase-04-renderer-core/day-78-gbuffer-pass.md)
79. [Deferred Lighting: Directional & Ambient](phase-04-renderer-core/day-79-deferred-lighting.md)
80. [Point Lights & Light Volumes](phase-04-renderer-core/day-80-point-lights.md)
81. [The Renderer Demo & Depth Correctness](phase-04-renderer-core/day-81-renderer-demo.md)
82. [Hardening, Sanitizers & Performance](phase-04-renderer-core/day-82-renderer-hardening-perf.md)
83. [Portability Proof & Phase 4 Wrap-up](phase-04-renderer-core/day-83-renderer-portability-wrapup.md)

## Key deliverables

- Render graph / frame graph: pass declaration, resource dependency
  tracking, automatic barrier/transition insertion.
- Deferred shading: a G-buffer geometry pass with depth testing, then a lighting pass.
- A minimal material system (shader + parameters + textures) sitting on
  top of the RHI's pipeline objects.
- Basic lighting model (directional + ambient + point lights in the deferred
  lighting pass, then a tonemap) sufficient to prove the pipeline, not a
  final lighting model.
- Camera/view/projection handling decoupled from any one scene format
  (ECS doesn't exist yet — this phase uses a minimal hard-coded scene list,
  replaced by ECS in Phase 9).

## Notes / open questions

- **Shader toolchain decision (settled: Slang, see Decisions).** `PLAN.md` assumption #18
  leaves the long-term shader language and the DX12/Metal path open
  (GLSL+SPIRV-Cross, HLSL+dxc, Slang, or our own). The material system
  needs reflection data, permutations and a mint step, so this must
  be settled, and the answer recorded in a plan file, before this
  phase's day files are written.

## Deferred from Phase 3 (decide here, before the day files are written)

Phase 3 deliberately left these RHI features out (see
`plans/phase-03-vulkan-rhi.md`). Each needs an explicit in/out decision
when this phase's days are written; if "in", the RHI interface changes
and the Phase 3 portability review (Day 67) should be consulted for the
D3D12/Metal impact.

| Feature | Decide when | Already reserved in the Phase 3 RHI |
|---|---|---|
| **MSAA** (sample counts, resolve attachments, pipeline sample count) | With the forward-vs-deferred choice; MSAA is awkward with deferred and may be replaced by post-process AA | `samples` field in `TextureDesc` and the pipeline description, fixed at 1 |
| **Bindless / descriptor indexing** | With the material system design (per-material bind groups vs a global texture table) | `supports_bindless` flag in `DeviceCaps`, always false |
| **Indirect draws** (`DrawIndirect`, count buffers) | Only if GPU-driven culling is wanted | `indirect` buffer-usage flag; no draw command |
| **Async compute / dedicated transfer queue overlap** | Only if profiling shows a need | Compute runs on the graphics queue; uploads use the transfer queue where one exists |

## Facts Phase 3 produced (inputs for this phase's day files)

- **State tracking.** The RHI tracks no resource state: `Barrier(resource, before, after)` makes the caller state
  `before`, and the Vulkan backend turns each state pair into one barrier from a single table. A render graph therefore
  owns all transitions, which is what the RHI was shaped for. Cost of one recorded `Barrier`: about 0.15-0.2 us
  (`rhi_bench`, Release); a frame with a few hundred transitions costs well under 0.1 ms to record.
- **Descriptor model.** Bind group layouts, bind groups created ahead of time (persistent) or per frame (transient,
  freed at the next `BeginFrame` of the slot), and 128 bytes of push constants. Per-draw transient groups work
  (about 0.5-0.6 us per draw including the uniform-ring slice) but the 10000-draw profile shows the device's resource
  mutex (about 11%) and `CreateBindGroup` (about 8%) on the path: the material system should bind per material or per
  pass and keep per-object data in one buffer, not make a bind group per object per frame.
- **Performance baseline** (`rhi_bench`, RTX 3060, Release, validation off): 1000 cubes record in 0.5 ms and 10000 in
  6.3 ms; 14-18 GiB/s uploads; cube pipeline 3.6-5.8 ms depending on the pipeline cache. Full table in
  `docs/guide/rhi.md`, "Performance notes", and in the Day 67 file.
- **Threading.** Command lists record on any thread (one thread per list), submission is serialized, resource creation
  is behind one mutex. Parallel recording by pass or by slice of the draw list needs no RHI change; the mutex is the
  first thing to look at if recording does not scale.
- **Conventions the renderer inherits.** Right-handed, Y up; `PerspectiveVulkan` flips Y and maps depth 0..1; meshes
  counter-clockwise with `FrontFace::CounterClockwise`; `Mat4` is column-major and equals std140.
- **Copy alignment** is now a device cap (`buffer_texture_copy_*_alignment`, defaults 512 and 256); asset upload code
  pads rows to it.
- **Shader format.** `ShaderDesc` carries SPIR-V; the portability review (section 3) lays out the two outcomes of the
  shader-toolchain decision for it, and requires that any translator flip clip-space Y for D3D12 and Metal.
- **Deferred-feature impact on D3D12/Metal** (fills the "Deferred from Phase 3" table above):

| Feature | Direct3D 12 | Metal |
|---|---|---|
| MSAA | resolve with `ResolveSubresource` / render-pass resolve; typeless formats for depth resolve | resolve store action; programmable sample positions differ |
| Bindless | SM 6.6 `ResourceDescriptorHeap`: fits the shader-visible heap model directly | argument buffers tier 2; needs `useResource` residency calls |
| Indirect draws | `ExecuteIndirect` with a command signature | indirect command buffers or `drawIndexedPrimitives` with an indirect buffer |
| Async compute | a second `COMPUTE` queue, fences already timeline-shaped | a second `MTLCommandQueue`, shared events |

