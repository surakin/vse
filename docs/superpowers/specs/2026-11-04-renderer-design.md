# Renderer Design (Phase 4)

Status: settled on Day 68; later days implement it and amend it here when the code teaches something. Companion to
`2026-11-04-rhi-design.md` and `2026-11-04-rhi-portability-review.md`.

Goal: a renderer built only on the RHI that draws many objects with materials and lights, deferred-shaded, with correct
depth, driven by a render graph. Not a final lighting model.

## R1. Layering

```
game / tools
    |
engine/renderer     camera, meshes, scene list, materials, render graph, passes   (target: renderer)
    |
engine/rhi          Device, CommandList, Swapchain, handles, null backend          (target: rhi)
    |
engine/rhi/vulkan   the only place Vulkan is named                                 (target: rhi_vulkan)
```

`renderer` links `rhi`, `jobs` and `foundation` and nothing else. It never names Vulkan, Wayland or X11 and never
includes `rhi/vulkan/`: the `rhi_boundary_check` gate already scans `engine/`, and a planted-violation fixture covers
`engine/renderer/`. Application code that needs a device creates it with `rhi::CreateDevice` and hands the `Device*` to
the renderer; the renderer does not own the device or the window.

Namespace `engine_placeholder::renderer`; headers are included as `"renderer/<name>.h"` rooted at `engine/`. The renderer logs
on the foundation's existing `LogRender` channel through `RenderLog(level, fmt, ...)`, so `--log-level` applies to it
(`LogRhi` and `LogJobs` have no such hook; a separate renderer channel would have been silent under `--log-level`).

## R2. The frame

```
RenderScene + Camera
   -> cull (frustum)                       CPU, single thread or jobs
   -> build RenderGraph (declare passes)   CPU, single thread
   -> compile (order, cull passes, barriers, transients)
   -> record passes (parallel on jobs)     one CommandList per pass or per draw-list slice
   -> Submit (graph order) -> Present
```

Everything the frame allocates on the CPU comes from a per-frame arena released at the frame boundary. Persistent GPU
objects (meshes, materials, pipelines, pooled transient textures) live across frames and are destroyed through the
device's deferred destruction.

## R3. Deferred shading, no MSAA

Passes, in graph order:

1. **G-buffer**: every visible object is drawn once into the G-buffer with its material, depth-tested.
2. **Lighting**: a fullscreen triangle for directional and ambient light, then one sphere volume per visible point light,
   additive, all into an HDR accumulation target; pixels at depth 1.0 are skipped.
3. **Tonemap**: exposure and an operator, linear to sRGB through the swapchain format, written to the swapchain image.

Deferred was chosen by the user. Consequences accepted: no hardware MSAA (the RHI keeps `samples = 1`; post-process AA is a
later add-on); transparent and overlay geometry needs a forward pass after tonemap (Phases 5 and 6 add it); materials
are limited to what the G-buffer can carry.

## R4. The render graph

Generic and built only on the RHI. Passes declare reads and writes of virtual resources; the graph orders and culls them,
derives every `ResourceState` transition and emits the `Barrier` calls (the RHI tracks no state), and allocates transient
textures from a pool keyed by descriptor. Pass callbacks are function pointers with a context pointer (no `std::function`,
no exceptions). Errors in a graph (read of an unwritten resource, cycle) are reported at compile time with names, never
during execution. Details land on Days 75-77.

## R5. Materials

A material is a shader permutation plus a parameter block and textures. Bind groups by set:

| Set | Contents | Bound |
|---|---|---|
| 0 | `FrameData` uniform (camera matrices, viewport, time) | once per pass |
| 1 | the material: parameter uniform buffer, textures, samplers | on material change |
| 2 | the per-object storage buffer (world and normal matrices) | once per pass |

Draws select their object by instance index, so the renderer creates no bind group per draw (the Phase 3 `rhi_bench`
profile put per-draw transient groups and the device mutex at about 19% of a 10000-draw frame). Pipelines are created
lazily and cached by (template, permutation, target formats, vertex layout) and never inside a frame's recording; a warm-up
call creates what a scene needs. Draws are sorted by pipeline, then material, then mesh.

## R6. Conventions (inherited from Phase 3)

Right-handed, Y up; `PerspectiveVulkan` flips Y and maps depth to 0..1 (no reverse-Z); meshes counter-clockwise with
`FrontFace::CounterClockwise` and `CullMode::Back`; `Mat4` column-major, equal to std140; light math in linear space; sRGB
only at the swapchain and in sRGB texture formats.

## R7. Errors

As in the RHI. Programming errors (a stale handle, a pass declared twice, a destroyed material used) are `ENGINE_ASSERT`
and documented `Aborts:`. Environment problems (a format the device cannot render to, out of memory, a lost device, a
pipeline that fails to build) are logged on `LogRenderer` and returned as an invalid handle, `false` or a status; the
renderer then stays destroyable. Failure injection via `rhi::FaultInjection` is part of the test set from Day 77.

## R8. Threading

Graph building and compiling are single-threaded. Recording is parallel: independent passes, and slices of a pass's draw
list, record on `jobs::Scheduler` participants into separate command lists, which are submitted together in graph order.
Resource creation goes through the device's mutex and happens outside recording. The renderer's public objects are
`thread-compatible` unless a header says otherwise.

## R9. Out of scope

| Feature | Decision trigger |
|---|---|
| MSAA | post-process AA first; revisit only if the user asks |
| Transparency, particles, overlays | a forward pass after tonemap, added with Phase 5 (text) and Phase 6 (UI) |
| Shadows, IBL, ambient occlusion | a Phase after the first playable demo, or when a demo needs them |
| Skinning, instancing of animated meshes | Phase 7 (ECS) and animation work |
| Bindless, indirect draws, async compute | profiling data showing a need (Phase 3 showed per-draw bind groups, solved by R5) |
| Occlusion culling, LODs | when scenes outgrow frustum culling |

## R10. Shaders: Slang

Shaders are Slang sources compiled offline by `slangc` (PLAN.md assumption #18, decided 2026-11-04 for platform
portability). One `.slang` source per shader; SPIR-V is the output now and HLSL/DXIL and MSL later. Reflection (bindings,
uniform block layouts, vertex inputs, push constants, compute local size) comes from `slangc` and is cooked into committed
`*.refl.inc` files next to the `*.spv.inc` words, so a checkout without `slangc` still builds and the material system never
hand-writes a layout twice (Day 71). Permutations are cooked offline from a manifest (Day 72). The Phase 3 GLSL shaders
are ported on Days 69-70 and deleted once the Slang output reproduces the golden images unchanged. Any translation to
D3D12 or Metal must flip clip-space Y (portability review, section 2).

## R11. G-buffer and lighting formats

| Target | Format | Contents |
|---|---|---|
| Albedo | `RGBA8_SRGB` | rgb albedo (sRGB-decoded on sample), a = baked occlusion |
| Normal | `RG16_Float` | world-space normal, octahedral-encoded |
| Material | `RGBA8_UNorm` | r = roughness, g = metallic, b = emissive scale, a = flags |
| Depth | `D32_Float` | sampled in `DepthRead` state; world position is reconstructed from depth and the inverse view-projection |
| Light accumulation | `RGBA16_Float` | linear HDR |

Three color attachments plus depth, inside the RHI's guaranteed four. Every format is checked with `Device::FormatSupport`
for its usage at renderer creation; a missing one is a logged failure naming the format. Depth is sampled, which Direct3D 12
needs a typeless resource for (a backend shim, portability review). The lighting model is Lambert diffuse plus GGX specular
(Trowbridge-Reitz distribution, Smith visibility, Schlick Fresnel) in a metallic-roughness workflow, kept in one shader
function so a later phase can replace it. Point-light attenuation is smooth and reaches exactly zero at the light's radius.
