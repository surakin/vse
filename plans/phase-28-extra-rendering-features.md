# Phase 28 — Extra Rendering Features

**Status: planned (added 2026-11-23, the user; day files written the same day, Days 478-505). Day 478 done 2026-11-23 (design spec and spikes; `CURRENT_PHASE` is 28). Next: Day 479.**

**Depends on:** Phase 5 (the renderer core: the G-buffer, the lighting pass, `SceneFrame`), Phase 8 (the RHI reset: every new GPU owner keeps its CPU source and rebuilds), Phase 17 (ECS components), Phase 18 (spatial: culling and the hierarchy), Phase 22 (asset pipeline: minted heightmaps, cube maps), Phase 24 (scenes for hosts: the C API's worlds), Phase 25 (physics, for a height-field collider, if the user wants one).
**Goal:** the rendering features a game world needs beyond meshes and lights: terrain from height maps, mesh instancing at scale (flat and hierarchical), and a sky drawn behind the world.
**Definition of done:** each feature below renders in the demo scene on Vulkan, Direct3D 12 and Metal through the RHI only, survives a device reset, is set from the ECS and from the C API (and from scripts where a setting is natural), has `///` docs and a guide page, and is covered by tests (CPU-side logic) and a reference image or counts test (GPU side).

## Days

Sky box (478-484), terrain with the height-field collider (485-493), instanced and hierarchical instanced meshes (494-501), Windows and macOS (502-503, the user's), demo and benchmarks (504), closing (505).

478. [Design Spec, Spikes and Phase Setup (Sky Box)](phase-28-extra-rendering-features/day-478-design-spec-spikes-and-phase-setup.md)
479. [The RHI Cube Texture (Vulkan and the Null Device)](phase-28-extra-rendering-features/day-479-rhi-cube-textures.md)
480. [Cube Map Assets: Minting and Loading](phase-28-extra-rendering-features/day-480-cube-map-assets-and-minting.md)
481. [The Sky Pass and the Procedural Sky](phase-28-extra-rendering-features/day-481-procedural-sky-pass.md)
482. [The Cube Map Sky](phase-28-extra-rendering-features/day-482-cube-map-sky.md)
483. [Reflections and Ambient from the Sky](phase-28-extra-rendering-features/day-483-sky-reflections-and-ambient.md)
484. [The Sky from the ECS, the C API and Scripts](phase-28-extra-rendering-features/day-484-sky-ecs-c-api-and-scripts.md)
485. [Terrain Design Spec and Spikes](phase-28-extra-rendering-features/day-485-terrain-design-spec-and-spikes.md)
486. [The Height Map Asset and its Minter](phase-28-extra-rendering-features/day-486-height-map-asset-and-minting.md)
487. [Terrain Tile Selection on the CPU](phase-28-extra-rendering-features/day-487-terrain-tile-selection-on-the-cpu.md)
488. [Drawing the Terrain](phase-28-extra-rendering-features/day-488-terrain-drawing.md)
489. [Terrain Materials: Layers and Splat Maps](phase-28-extra-rendering-features/day-489-terrain-materials-and-splat-maps.md)
490. [Terrain in the Scene: ECS, Render Sync and Spatial Cells](phase-28-extra-rendering-features/day-490-terrain-in-the-scene.md)
491. [The Height-Field Collider: Shape, Library and Queries](phase-28-extra-rendering-features/day-491-height-field-collider-shape.md)
492. [Height-Field Collision and the Character Controller](phase-28-extra-rendering-features/day-492-height-field-collision.md)
493. [Terrain in the C API, Scripts and the Demo Scene](phase-28-extra-rendering-features/day-493-terrain-c-api-scripts-and-demo.md)
494. [Instancing Design Spec and Spikes](phase-28-extra-rendering-features/day-494-instancing-design-spec-and-spikes.md)
495. [The Instance Set](phase-28-extra-rendering-features/day-495-instance-set-data-structure.md)
496. [Chunked Instance Culling on the CPU](phase-28-extra-rendering-features/day-496-chunked-instance-culling.md)
497. [The Instanced Draw Path](phase-28-extra-rendering-features/day-497-instanced-draw-path.md)
498. [Instanced Meshes in the ECS and the Scene](phase-28-extra-rendering-features/day-498-instanced-meshes-in-the-scene.md)
499. [The Hierarchical Instance Structure](phase-28-extra-rendering-features/day-499-instance-hierarchy-structure.md)
500. [Hierarchy Culling and Levels of Detail](phase-28-extra-rendering-features/day-500-hierarchy-culling-and-levels-of-detail.md)
501. [Hierarchical Instances in the Scene, the C API and Scripts](phase-28-extra-rendering-features/day-501-hierarchy-draw-c-api-and-scripts.md)
502. [Windows: Direct3D 12 for the Phase's Features (the user's day)](phase-28-extra-rendering-features/day-502-windows-direct3d-12.md)
503. [macOS: Metal for the Phase's Features (the user's day)](phase-28-extra-rendering-features/day-503-macos-metal.md)
504. [The Demo Scene and Benchmarks](phase-28-extra-rendering-features/day-504-render-features-demo-and-benchmarks.md)
505. [Docs, Tutorials, Closing Runs and the Phase Review](phase-28-extra-rendering-features/day-505-docs-tutorials-closing-runs-and-review.md)

## Features

1. **Height maps (terrain).** A minted height-map asset (16-bit, with a normal map derived offline), drawn as a tiled grid with a level of detail that changes with distance and with no cracks between levels; a material with layered textures blended by a splat map. Open: a height-field collider in `engine/physics` (a separate day, if wanted), holes, and how a terrain is split into spatial cells.
2. **Instanced meshes.** The renderer already merges a run of the same mesh and material into one instanced draw, fed by a per-object storage buffer. This feature makes instancing a first-class thing: an `InstancedMesh` component holding thousands of transforms (and per-instance data) that does not cost one entity per instance, culled in chunks through `engine/spatial`, with per-instance colour or material parameter. Open: a GPU-driven path (indirect draws, a compute cull) against a CPU path, and how it is set from the C API in bulk.
3. **Hierarchical instanced meshes.** Instances arranged in a tree (a forest of trees with branches, a city of buildings with parts): a node's transform composes its children's, culling takes whole subtrees, and a level of detail picks a simpler mesh far away (to impostors as a stretch). Built on feature 2 and on the spatial module's tree. Open: whether a hierarchy node is an entity or a row in the instance buffer.
4. **Sky box rendering.** Drawn behind everything: a cube map (minted from six images or an equirectangular one), and a procedural sky from the existing environment settings (`sky_color`, exposure). The same cube map feeds the reflection term that today is a flat sky colour (`reflection_intensity`), with a prefiltered mip chain for rough surfaces. Open: sun disk and atmospheric scattering are out unless the user asks.

## Out of scope (stays in Phase 35, feature parity)

Terrain streaming of world cells, foliage painting tools, virtual texturing, GPU occlusion culling beyond what feature 2 needs.

## Decisions (the user, 2026-11-23)

- A **height-field collider is required** (`engine/physics`): it is a day of this phase, built on the terrain's height data, deterministic like the rest of physics.
- The **instance hierarchy has its own packed structure**, not ECS entities; the entities that own an instanced mesh hold a handle to it.
- **CPU-driven culling and draws first** (the recommendation, accepted in principle); the instance data is shaped so a GPU-driven path can be added later. A GPU path becomes its own day only if a measurement asks for it.

- **The cube map is part of the sky box** (not deferred): minted cube textures drawn behind the world and feeding the reflection term, with the procedural gradient as the default when no cube map is assigned.
- **Order:** sky box, terrain (with its collider), instanced meshes, then hierarchical instanced meshes (which build on instanced meshes).

## Open questions

None for the user now; Day 478 may start.
