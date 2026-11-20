# Maintenance: colliders from an imported glTF, and a character that walks on them

Not a numbered phase. Request from the host project (2026-11-20).
Read from `include/engine/engine_c.h`, `docs/reference/script-api.md`, `KNOWN-ISSUES.md` (Day 394 and Day 408/409 entries) and `docs/guide/assets.md` ("Mesh colliders").

## Why

The host's 3D call world is a `.glb` exported by an artist (a ground, trees, walls) that the host imports at run time with `import_gltf` and spawns with `level_spawn`. The local player is a Lua script
that now needs gravity and jumping, which the engine already offers as a `character` (kinematic capsule: `character.create`, `move`, `jump`, `is_grounded`). A character needs something solid to stand on,
and today nothing in an imported scene is solid: the importer makes meshes and materials only, and the C API refuses hull, mesh and compound collider shapes (UNSUPPORTED).

The host could add a box under the floor by hand, but that only works for a flat ground. Artists will bring their own worlds (rooms, stairs, terrain, walls), and they export a `.glb` from their own tools:
the collision has to travel in that file, with no extra tooling and no code in the host that knows the world's shape.

## What exists

- Physics: bodies, colliders (sphere, box, capsule through the C API), raycasts and overlaps, gravity (`physics_set_gravity`, default (0, -9.81, 0)), events, snapshot and restore.
- `character.*` in Lua: capsule, step height, slopes, moving platforms, `gravity_scale`; it sweeps against static and kinematic bodies.
- `MCOL` (`asset/mesh_collider.h`): the triangle mesh of a static collider, `BuildMeshColliderBlob`, `MeshColliderView`, `asset_mint mint-mcol`; the physics module builds the hierarchy when the mesh is added.
- `import_gltf`, `level_spawn`, the import summary, `resource_add` and the resource cache.

## Wanted

1. **The importer reads collision from the glTF.** Say which convention you read (candidates: the `OMI_physics_shape` / `OMI_physics_body` extensions, `KHR_physics_rigid_bodies`, or a node-name rule such as a `-col` suffix);
   recommend the one that Blender and other common exporters already write, so an artist needs no plug-in of ours. A mesh marked as a collider becomes static colliders (a triangle mesh; boxes, spheres and
   capsules where the file says so), is not drawn unless it is also a visual mesh, and keeps its node transform and scale. Say what happens to a file with no collision marks (nothing is solid; the summary says so).
2. **`level_spawn` creates the bodies.** Spawning the level creates its static bodies and colliders with the entities, so a spawned world is solid at once, and the characters in it walk on it. If level despawn exists
   by then (see the streaming request), the bodies go with the entities.
3. **The C API accepts mesh colliders** (the refused shapes of Day 408/409 for what a level needs): a way for a host that builds its own entities to give a body a mesh collider from a resource
   (`resource_add` of an `MCOL`, then a collider that names it).
4. **The import summary reports collision:** how many colliders, triangles and bodies were made, and warnings (a collider with no triangles, a mesh over the limit, a non-uniform scale, an unsupported shape).
   The host shows the warnings to the artist before upload, as it does for other importer warnings.
5. **The character walks on it.** Check that `character.create` on an entity standing on an imported mesh collider works: stands on a ground mesh, climbs steps up to `step_height`, slides along walls, falls off an edge,
   and `is_grounded` is true on the ground. State the ground's thickness rule (the Day 394 note: "a body whose centre passes through a triangle is pushed out the other side").

## Limits the host needs

A world file comes from anyone in a room, so the importer is an untrusted-input parser:

- bounded: the existing 1,048,576 vertices / triangles per mesh collider and a total over the whole file that the host can read from the summary and lower;
- malformed collision (a non-finite position, an index out of range, a triangle with no area, a huge scale) is refused or dropped with a warning, never a crash or a hang;
- importing the same file twice gives the same collider bytes (the resource cache keys on a hash of the file, options and import format).

## Not wanted

- A physics editor, per-material friction authoring beyond what the chosen glTF extension already says.
- Dynamic bodies from the file (props that fall); static colliders only for now.
- The engine fetching or caching anything itself; the host passes bytes as today.

## Tests this needs

- A `.glb` with a ground and a box wall carrying collision marks: after import and `level_spawn`, a character dropped above the ground lands on it (`is_grounded` true, resting height right) and cannot walk through the wall.
- A file with no collision marks imports as before and the summary reports zero colliders.
- A marked mesh with a NaN position, an out-of-range index and a zero-area triangle: refused or warned, no crash.
- A step of `step_height` is climbed, a higher one is a wall; a slope steeper than `min_ground_y` is a wall.
- Import twice: equal collider bytes. The same on Windows and macOS (the physics hash tests are already shared across them).

## Where it landed (2026-11-20)

Built on Linux (`plans/maintenance/gltf-colliders.md`, design `docs/superpowers/specs/2026-11-20-gltf-colliders-design.md`); the PC and the Mac
runs are Day 8.
1. Collision from the file: `-col`/`-colonly` node names (no plug-in needed) and `OMI_physics_body`/`OMI_physics_shape` (boxes, spheres,
   capsules, triangle meshes); a file with no marks imports as before with `"colliders":0`.
2. `level_spawn` makes the static bodies; they follow their entities' position and rotation, not later scale changes.
3. The C API takes mesh colliders: `ENGINE_SHAPE_MESH` with an `MCOL` id (`ENGINE_RESOURCE_MESH_COLLIDER` among an import's results).
4. The summary's `"collision"` member and the warnings; the limit `max_collision_triangles` (default 4,194,304).
5. A character lands, climbs a step of `step_height`, is stopped by a higher one, a steep slope and a wall, and falls off an edge
   (`host_character_world.c`). Thickness: a character never passes through a single-sided ground; a fast dynamic body can.
