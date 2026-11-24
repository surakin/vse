# Terrain

A terrain is a height map drawn as tiles, with a ground that physics, the C API and scripts can ask about. The pieces are in the modules that own them; this page is the map between them.

## The data: a height map

An `HGHT` blob (`asset/height_map.h`) holds 16-bit samples (2 to 8192 a side), the heights of sample value 0 and 65535, the metres between samples, the mip chain and a pyramid of block minima and maxima. A sample `s` is the height `range_min + s / 65535 * (range_max - range_min)`.
`asset::BuildHeightMapBlob` writes one deterministically; `asset::HeightMapView` reads it in place and answers `HeightAt` and `NormalAt` by the two triangles of a cell (each cell is cut from its minimum-x, minimum-z corner to its maximum-x, maximum-z corner). The map's own space starts at the first sample: x and z are
metres from it, y the height.

The renderer's copy is `renderer::TerrainSource` (the tile quadtree and the `R16_UNorm` height texture, `asset::MakeTerrainSource`), made once from a view and shared. The physics copy is a `physics::HeightField` over the view's own bytes (nothing is copied). All three give the same height at a point, bit for bit.

## Drawing

`renderer::RenderScene` holds up to 8 terrains a frame (and 4096 tiles in all): `AddTerrain`, `SetTerrain` and `RemoveTerrain` take a `renderer::TerrainObject` (source, material, world matrix: a translation and a turn about y). In the ECS, `scene::Terrain` (a source and a material) and the render sync system mirror an entity into the scene; the C API's component does the same on a
surface. The surface of a terrain is a `renderer::TerrainMaterial`: up to four layers (tiling, tint, roughness, metallic) taken from a layer array texture and blended by a splat map, a slope rule that replaces the weights on steep faces and a distance fade (see [the renderer](renderer.md), "The terrain material").

Which tiles are drawn is chosen by `TerrainSelectSettings`; a tile next to a tile two levels finer is split until neighbours are within one level, whatever the heights of the boxes are, and a box's height range covers the heights the mesh can reach past its edge (the mip filter's reach). A frame that holds more tiles than the buffer (4096) keeps the nearest ones and counts the others (`SceneFrame::TerrainTilesDropped`). The scene frame takes them from the render settings `terrain_first_range`, `terrain_morph_fraction` and `terrain_max_distance` ([Render settings](render-settings.md)) until `SceneFrame::SetTerrainSelect` sets them directly.

`asset::VerifyHeightMap` checks a blob's mips and block pyramid against its samples (the loader checks only their shape): use it in tools and tests, not on every load.

## Colliding

`physics::HeightField` is the collider shape (`physics::Shape::MakeHeightField`): static bodies only, no mass. Its space is centred on the map, so a collider that stands for a terrain placed with its first sample at the entity's origin has an offset of half the map's size. Bodies rest on it, roll on it, and fast ones are held by continuous collision;
a body that has sunk below the surface is pushed back up (the field has no underside). Queries: rays and shape-less overlaps through the physics queries, plus `TerrainHeight` and `TerrainRayCast` for the ground only ([Physics](physics.md)).

## From a host and from scripts

A C host adds a height map (`resource_add` of a minted blob, or `height_map_create` from samples), sets `ENGINE_COMPONENT_TERRAIN` on an entity, optionally with `ENGINE_TERRAIN_COLLIDER`, and reads the ground with `terrain_height` ([The C API](engine-library.md), "Terrain"). Scripts read the ground of the colliders through `terrain.height`, `terrain.normal`
and `terrain.raycast` ([Scripting](scripting.md), "Physics").

## Cost

Measured with `render_features_bench` (a Release build): choosing the tiles of an 8192 x 8192 map for a camera 120 m above it looking along the ground takes 0.004 ms and draws 126 tiles (one draw each); the quadtree of the map is built in 1 ms from its block pyramid. The sky pass costs 0.07 ms a frame at 1920 x 1080 for the procedural sky and 0.14 ms for the cube map sky (the whole frame of the demo's scene with 20,000 trees is about 1.4 ms on an RTX 3060).

## Limits

- A terrain's placement is a translation and a turn about y; a scale or a tilt is not drawn, and a collider made from one is refused.
- A terrain's collider is made once, from the samples at that time; replacing the height map afterwards changes what is drawn and what `terrain_height` reads, not the collider that was made.
- The layer array and splat map of the C API's terrain are texture resources; a terrain with none draws flat grey.
