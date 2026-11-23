# Phase 28 — Extra Rendering Features

**Status: goal level (added 2026-11-23, the user). Its day files are not written yet: they must be written and committed before the phase starts.**

**Depends on:** Phase 5 (the renderer core: the G-buffer, the lighting pass, `SceneFrame`), Phase 8 (the RHI reset: every new GPU owner keeps its CPU source and rebuilds), Phase 17 (ECS components), Phase 18 (spatial: culling and the hierarchy), Phase 22 (asset pipeline: minted heightmaps, cube maps), Phase 24 (scenes for hosts: the C API's worlds), Phase 25 (physics, for a height-field collider, if the user wants one).
**Goal:** the rendering features a game world needs beyond meshes and lights: terrain from height maps, mesh instancing at scale (flat and hierarchical), and a sky drawn behind the world.
**Definition of done:** each feature below renders in the demo scene on Vulkan, Direct3D 12 and Metal through the RHI only, survives a device reset, is set from the ECS and from the C API (and from scripts where a setting is natural), has `///` docs and a guide page, and is covered by tests (CPU-side logic) and a reference image or counts test (GPU side).

## Features

1. **Height maps (terrain).** A minted height-map asset (16-bit, with a normal map derived offline), drawn as a tiled grid with a level of detail that changes with distance and with no cracks between levels; a material with layered textures blended by a splat map. Open: a height-field collider in `engine/physics` (a separate day, if wanted), holes, and how a terrain is split into spatial cells.
2. **Instanced meshes.** The renderer already merges a run of the same mesh and material into one instanced draw, fed by a per-object storage buffer. This feature makes instancing a first-class thing: an `InstancedMesh` component holding thousands of transforms (and per-instance data) that does not cost one entity per instance, culled in chunks through `engine/spatial`, with per-instance colour or material parameter. Open: a GPU-driven path (indirect draws, a compute cull) against a CPU path, and how it is set from the C API in bulk.
3. **Hierarchical instanced meshes.** Instances arranged in a tree (a forest of trees with branches, a city of buildings with parts): a node's transform composes its children's, culling takes whole subtrees, and a level of detail picks a simpler mesh far away (to impostors as a stretch). Built on feature 2 and on the spatial module's tree. Open: whether a hierarchy node is an entity or a row in the instance buffer.
4. **Sky box rendering.** Drawn behind everything: a cube map (minted from six images or an equirectangular one), and a procedural sky from the existing environment settings (`sky_color`, exposure). The same cube map feeds the reflection term that today is a flat sky colour (`reflection_intensity`), with a prefiltered mip chain for rough surfaces. Open: sun disk and atmospheric scattering are out unless the user asks.

## Out of scope (stays in Phase 35, feature parity)

Terrain streaming of world cells, foliage painting tools, virtual texturing, GPU occlusion culling beyond what feature 2 needs.

## Open questions for the user before the day files are written

- Which of the four come first, and is the procedural sky enough for the first day of the sky box?
- Is a height-field collider part of this phase or of a physics follow-up?
- CPU-driven culling and draws first, or the GPU-driven path from the start?
- Does the hierarchy of feature 3 live in ECS entities or in its own packed structure?
