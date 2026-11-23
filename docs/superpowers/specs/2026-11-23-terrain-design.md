# Terrain: design

Date: 2026-11-23 (Phase 28, Day 485). Decisions of the user (2026-11-23): a height-field collider is required; culling starts on the CPU; the
instance hierarchy (a later feature) has its own structure; the order is sky box, terrain, instanced meshes, hierarchical instanced meshes.
Written from four spikes (below), done as analysis against the code as it stands; no spike code is kept.

## What is built

1. **A height-map asset**: 16-bit heights over a user height range, minted from a raw image, loaded as a reset-safe GPU texture.
2. **A tile quadtree** chosen per frame on the CPU by distance, with morphing between levels so no cracks open.
3. **A terrain draw path**: one shared grid mesh, instanced per selected tile, heights read in the vertex shader.
4. **A layered material** blended by a splat map.
5. **The terrain in the scene**: an ECS component, render sync, spatial cells.
6. **A height-field collider** in `engine/physics` (`ShapeKind::HeightField`), deterministic.
7. **The C API, scripts and a demo scene.**

## Spike 1: the level-of-detail scheme

Candidates: a distance-based quadtree of fixed-size grid tiles (CDLOD style) and a geometry clipmap.

| | Tile quadtree | Clipmap |
|---|---|---|
| Draws | one instanced draw of the visible tiles (one 33x33 grid mesh, 2048 triangles); about 100 to 400 tiles in view | 12 to 20 ring draws per frame |
| Cracks | a per-vertex morph toward the coarser level's position in the last 30% of each level's range; neighbours differ by at most one level (range rule), so the morph hides the seam | rings overlap with a blend band; needs a trim and a skirt |
| CPU selection | a recursive test of node bounds against the camera and frustum; the bounds come from a per-node min/max height pyramid | trivial |
| Culling | per tile, so the spatial module and the frustum cull see tiles | a ring is not cullable below the ring |
| Streamed or edited map | a node's bounds change locally; the texture changes in a rectangle | the whole ring is rewritten as the camera moves |
| Fit with the plan | the CPU culling decision, the spatial cells, one instanced draw | works against both |

**Pick: the tile quadtree.** Tile grid 33x33 vertices (32x32 quads); levels chosen so a level's range is twice the finer one's; the selection
distance is measured to the node's bounding box, not its centre. The morph is a function of the vertex's distance and is a pure function of
the camera position (no state), so a reset recreates nothing.

## Spike 2: height data on the GPU

`rhi::Format` has `R16_Float` and no 16-bit normalized format. A half float holds 11 significant bits, so near 1.0 its step is 2^-11, a
16-bit height would lose five bits; unacceptable for a user height range of a few hundred metres (a step of 0.1 m or worse).

| | `R16_UNorm` texture | Storage buffer of `u16` |
|---|---|---|
| Vertex-stage fetch | sampled in the vertex stage: allowed on Vulkan, Direct3D 12 and Metal | a storage buffer in the vertex stage: needs `vertexPipelineStoresAndAtomics`-free read, allowed, but a buffer binding in the vertex stage of the G-buffer pipeline is new |
| Bilinear between samples | free (`textureLod`) | four loads and a manual lerp |
| Precision | 16 bits over the range | 16 bits |
| Mips for far tiles | the minted mips | none |
| Reset | a `GpuTexture` owner, as every texture | a buffer owner, new |

**Pick: an `R16_UNorm` texture.** Consequence: **Day 486 adds `rhi::Format::R16_UNorm`** (Vulkan `VK_FORMAT_R16_UNORM`, Direct3D 12
`DXGI_FORMAT_R16_UNORM`, Metal `MTLPixelFormatR16Unorm`; all three sample it filtered), with the format table, the texture size rules and a
null-device test. Height = `range_min + sample * (range_max - range_min)`; the range is in the asset. The vertex shader samples at the vertex's
world xz with `SampleLevel`, level chosen by the tile's level so a coarse tile reads a mip, not aliased texels.

## Spike 3: the collider data

Candidates: the regular height grid itself, and a triangle mesh built from it (`ShapeKind::Mesh`).

For 1024x1024 samples:

| | Height grid | Triangle mesh |
|---|---|---|
| Heights / vertices | 1,048,576 `u16` = 2 MB | 1,048,576 vertices x 12 B = 12.6 MB |
| Triangles | implicit (2 per cell) | 2,093,058 x 12 B indices = 25 MB |
| Acceleration | a min/max pyramid over 8x8 blocks, 16,384 blocks x 4 B = 64 KB | a BVH over 2 M triangles, tens of MB and a build cost |
| Query by a box or sphere | the cells under the shape's footprint (a handful), each two triangles | a BVH descent |
| Determinism | fixed diagonal split of each cell, fixed cell order | fixed by the BVH build |

**Pick: the grid itself.** `ShapeKind::HeightField` (static bodies only, as `Mesh`): a `HeightFieldLibrary` of shared `u16` grids with their
range and cell size, a pyramid of block min/max built when the field is added, each cell split along the same diagonal (from the minimum-x,
minimum-z corner to the maximum-x, maximum-z one), and the contacts of a sphere, a capsule and a box against the triangles of the cells the shape
overlaps, in row-major cell order. The collider reads the same `u16` samples the terrain asset stores, so the picture and the physics agree to the
sample (not to the GPU's bilinear between samples: the collider is exact on the triangles of the grid, and the drawn tile surface at a tile's own
level is those same triangles; a coarser level deviates from the collider by design, and a fine tile is always chosen near the player).

## Spike 4: the normals

Candidates: a stored normal map (`RG8_UNorm` octahedral; there is no `RG16_UNorm`) and normals computed from four height taps.

- A stored `RG8` octahedral normal is 8 bits per axis: coarser than the 16-bit heights it is derived from, and a second texture to bind, mint,
  reset and stream.
- Four central-difference taps of the height texture at the texel spacing give a smooth normal from the full 16 bits, need no second asset and
  follow the level (a coarse tile reads a mip, so its normal is a coarse normal). Cost: 4 more fetches per vertex, 4 per pixel.

**Pick: normals from four taps, in the vertex shader (for the morph) and the fragment shader (for the shading); no stored normal map.** This
replaces the "normal map derived offline" of the phase's first outline, and Day 486 drops it from the asset. If a profile later shows the pixel
taps matter, a stored map is an additive change.

## The asset (Day 486)

`HGHT` blob: width, height (samples, each 2 to 8193), `range_min`, `range_max` (metres), the cell size (metres between samples), and the
`R16_UNorm` levels with mips. Minted from a raw 16-bit image (`.hgt.src`, the usual text source), deterministic (the mips are the integer box
filter used for textures). A per-block min/max pyramid (8x8 samples a block) is stored in the blob for both the quadtree bounds and the collider.

## The quadtree and the draw (Days 487 and 488)

- A node covers `2^k` tiles; a leaf is one 32x32-quad tile. Node bounds come from the block pyramid, so a mountain's tile is not culled by a flat
  tile's box.
- Selection: a node is split when the camera is nearer than `level_range[level]` to its box; tiles go to a per-level list; a frustum test happens
  per node before the split.
- Draw: one grid vertex buffer of 33x33 (x, z in 0..1) plus an index buffer, shared by every tile; per-tile instance data (origin, size, level,
  neighbour morph flags) in a per-frame buffer; the vertex shader fetches the height, morphs, and writes the G-buffer. The terrain draws into the
  G-buffer, so the lighting, the sky and the shadows treat it as any surface.
- No state is cached across frames except the shared grid and the height texture (both owners with CPU sources), so a device reset needs nothing
  new.

## Material (Day 489)

Up to 4 layers (albedo plus a tint), blended by an `RGBA8` splat map over the terrain; world-space tiling per layer; the layers share one sampler.
Lit as the standard material (roughness and metal per layer, no normal maps in this phase).

## In the scene (Day 490)

A `TerrainComponent` (asset, material, extents) on an entity; the render sync adds the terrain to the scene's terrain list; the spatial module
holds the terrain as cells (a grid of 8x8-tile cells) so the camera query finds the tiles to consider; holes are not built (a later addition:
an alpha channel of the splat map).

## Determinism and reset

- The collider uses `+ - * /` and the square root, in a fixed cell order: the state hashes are equal on all platforms, like the rest of physics.
- The draw is a pure function of the camera and the asset; a reset reuses the CPU sources.

## Decisions for the days that follow

1. **Day 486 adds the `R16_UNorm` format** to the RHI (not in the first day plan) and drops the stored normal map from the asset.
2. Day 487 selects tiles by the box distance rule above; Day 488 is one instanced draw.
3. Day 489 has no normal maps.
4. Days 491 and 492 build `ShapeKind::HeightField` on the same `u16` samples and block pyramid.
