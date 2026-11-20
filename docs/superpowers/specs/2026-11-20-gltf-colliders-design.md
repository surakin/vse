# Colliders from an imported glTF, and a character that walks on them (design, 2026-11-20)

Status: draft for the user's approval, revised after the builder check (2026-11-20). Request: `plans/maintenance/host-gltf-colliders-and-character.md` (a host's, 2026-11-20).

## Intent

A host imports a `.glb` that an artist exported from their own tool (`import_gltf`), spawns it as a level (`level_spawn`), and a
scripted player character walks on it with gravity and jumps. Today nothing in an imported scene is solid: the importer makes meshes,
materials, skeletons, clips, lights and a level, and no collision. The collision has to travel inside the artist's file, need no
plug-in of ours, and need no code in the host that knows the world's shape.

The files come from anyone, so the importer stays a hostile-input parser: bounded, refusing or dropping bad collision with a warning,
never crashing or hanging, and deterministic (the same file and options give the same bytes).

## Goals

1. The importer reads collision marks from the glTF, in **two conventions** (the user's choice, 2026-11-20):
   - **node-name suffixes**, which every exporter writes because it keeps object names, so no plug-in is needed (the default route);
   - the **`OMI_physics_body` / `OMI_physics_shape`** extensions (written by a Blender add-on, read by Godot), for artists who want
     boxes, spheres and capsules instead of triangle meshes.
2. **One static collider per marked node** (approach A, the user's choice): each marked node's entity in the level carries a new level
   component naming its collision shape; a triangle mesh is minted as an `MCOL` resource, a primitive is stored as values.
3. `level_spawn` (C API) makes one **static body** per such entity, so the spawned world is solid at once and characters walk on it.
   **Colliders follow their entities** (the user's choice, 2026-11-20): when the host moves the level's root, or a node with a collider
   moves (an animated node), the static body moves with it.
4. The C API takes **mesh colliders from a resource** for hosts that build their own entities: a mesh shape that names an `MCOL` id.
5. The **import summary reports collision** (counts, and the warnings go to the existing warnings buffer).
6. **The character walks on it** (C API `ENGINE_COMPONENT_CHARACTER` and Lua `character.*`): stands, climbs steps, slides along walls,
   falls off edges, `is_grounded` on ground; the ground-thickness rule is stated.

## Non-goals (the request's, kept)

- No dynamic bodies or triggers from the file (static colliders only); no physics editor; no per-material friction authoring beyond
  what `OMI_physics_*` already carries; the engine fetches and caches nothing itself (the host passes bytes, as today).
- No `KHR_physics_rigid_bodies` / `KHR_implicit_shapes` now: no stock exporter writes them yet (an open issue on the official Blender
  exporter). The importer's structure (a collision description per node, filled by a convention reader) leaves room for a third reader.
- No convex hulls from the file (physics has hulls, but no hull asset can be minted from a glTF mesh yet): a convex mark is a triangle
  mesh with a warning (below).

## Anti-goals

- A file without collision marks must import exactly as before (byte-identical blobs for every resource it made before, apart from
  the importer's output-version bump that this change needs anyway; the summary says 0 colliders).
- No silent approximation: every dropped or changed collider gives a counted warning that names the node.
- No crash, abort or unbounded work on any input, including through `level_spawn` of a hostile level blob (the render scene and
  physics assert on non-finite input; the spawn checks first, as it does for lights since the 2026-11-20 C API work).

## Approaches considered

- **A. One collider per marked node (chosen).** `MCOL` per marked glTF mesh, in the mesh's own space, shared between the nodes that
  use it (the scale is applied when the body is made); primitives as values; a level component per entity; `level_spawn` makes one static body per entity. Bodies
  belong to entities, so a later despawn (the streaming request) removes them with the entities; each mesh stays under the per-mesh
  limit; the C API gets the same pieces.
- **B. One merged mesh per level.** Simpler spawn and one hierarchy, but the level is one rigid blob: nothing can be removed or moved,
  and the 1,048,576-triangle limit binds the whole level. Rejected.
- **C. Give the host the shapes to build bodies from.** Contradicts the request (no host code that knows the world's shape). Rejected.

## Design

### 1. The conventions

**Node-name suffixes** (Godot's, case-sensitive), on the glTF node's `name`:

| Suffix | Meaning |
|---|---|
| `-col` | The node's mesh is drawn **and** is a static triangle-mesh collider. |
| `-colonly` | The node's mesh is a collider only: not drawn (no mesh reference, no render mesh made for it unless another node draws it). |
| `-convcol`, `-convcolonly` | As above with a convex hull: imported as a triangle mesh, with a warning ("convex collision is a triangle mesh"). |

- The suffix may be followed by Blender's duplicate number (`wall-col.001`): a trailing `.` and digits are ignored when matching.
  The entity's name is the node name as the importer makes it today (the suffix is kept: the host finds `wall-col` by that name).
- A marked node without a mesh (an empty) gives a warning and no collider (Godot reads the empty's display shape; glTF does not carry
  it).
- Every primitive of the node's mesh contributes its triangles (`TRIANGLES` mode only; other modes are skipped with a warning);
  morph targets and skinning are ignored for collision (the base shape, in the node's space), with a warning when present.

**`OMI_physics_body` / `OMI_physics_shape`** (the OMI group's specification; the builder reads the current text):

- The document-level `OMI_physics_shape.shapes` array holds the shapes; a node's `OMI_physics_body` names one with `collider.shape`.
- `box` (`size`), `sphere` (`radius`), `capsule` (`radius`, `height`, along Y) become primitives; `trimesh` (`mesh`) becomes a
  triangle mesh from that glTF mesh; `convex` becomes a triangle mesh with the warning above; `cylinder` is dropped with a warning
  (physics has no cylinder).
- A body with no `motion`, or `motion.type` `"static"`, is a static collider. `"kinematic"` and `"dynamic"` bodies: their collider is
  made static with a warning ("dynamic bodies from the file are static"). A `trigger` is dropped with a warning (not wanted now).
- `OMI_physics_body` and `OMI_physics_shape` are added to the supported `extensionsRequired` list.
- A node with both an OMI collider and a suffix: the OMI collider wins, with a warning.
- Friction and restitution: if the OMI version in use carries a physics material, its friction and restitution are read (clamped to the
  C API's ranges, with a warning when clamped); otherwise physics' defaults.

**A file with no marks**: no collider is made; the summary reports `"colliders":0`; nothing else changes.

### 2. What the importer makes

- **`MCOL` per glTF mesh**, in the mesh's own space (the import's `scale` applied, as for drawn meshes): the triangles of all of the
  mesh's `TRIANGLES` primitives, indices from the accessors (or `0..n` for a non-indexed primitive). Every marked node that uses the
  mesh shares it. Path `<prefix>/colliders/<mesh name>`; the manifest gets a new key range (as the converted textures did) so re-imports
  keep ids.
- **Scale is applied at the body, not in the file** (the level stores each node's local transform and a parent link, not world
  matrices, and the host can scale the spawn): see section 4. At import the importer only warns about what will not work: a marked
  node whose world matrix at rest has a shear (it will be refused when the body is made), and a marked node that a clip animates (its
  collider follows the node's position and rotation; a scale change is not followed, section 4).
- **Bad geometry**: a non-finite position or an index out of range refuses the import, as for drawn meshes today (same messages); a
  triangle with no area (or with two equal indices) is **dropped** with one counted warning per collider (artists' meshes have them),
  using physics' own test (`TriangleMesh::Create`: f32 cross product squared above 1e-20; the `MCOL` builder's f64 1e-24 test is
  looser, and a triangle it accepts would make the physics mesh refuse the whole collider). The spawn applies the same test again after
  scaling and drops (counting) what a small scale made degenerate, so a spawn never fails on it;
  a collider with no triangle left is dropped with a warning. More than 1,048,576 vertices or triangles in one collider: dropped with a
  warning (the `MCOL` limit), not a failed import.
- **Limit over the file**: `max_collision_triangles` (a new field at the end of `engine_import_options`, 0 takes the setting
  `assets.import.max_collision_triangles`, default 4,194,304): an import that would make more refuses with `TOO_LARGE`, naming the
  limit. The summary reports the total, so a host can lower the limit for the next file.
- **The level component** `static_collider` (in `asset`, registered by `RegisterLevelComponents`, with the reference table so the
  level's closure includes its `MCOL`s): `shape` (mesh, box, sphere, capsule), the `MCOL` id (a typed reference, null for a
  primitive), `half_extents`, `radius`, `half_height` (unscaled), `friction`, `restitution`. No offset or rotation: the body is at the
  entity.
- **`-colonly`**: the node's entity has no mesh reference; if no other node draws that glTF mesh, no render mesh is made for it.
- **Primitives**: OMI sizes are stored unscaled, like meshes; the scale rules are in section 4.
- **Determinism**: the `MCOL` bytes depend only on the file and the options (minted by `BuildMeshColliderBlob`, which is
  deterministic); `kGltfImporterOutputVersion` goes up (5).

### 3. The summary

A new member after `"lights"`: `"collision":{"colliders":N,"meshes":M,"triangles":T,"primitives":P,"dropped":D}`. Colliders are
the entities that got one; meshes the `MCOL`s made; triangles their total; primitives the boxes, spheres and capsules; dropped the
marked nodes that got none. The summary's `"version"` stays 1 (an added member, as for `"lights"`). Each warning is a line of the
existing `warnings` buffer and names the node.

### 4. Bodies for level entities (physics and C API)

The builder check found that physics makes bodies only for entities **without** a parent, placed by their `LocalTransform`
(`physics/sync.h`), while every spawned level entity has a parent (the spawn root or its node's parent). So:

- **The sync learns static bodies with parents.** An entity with `RigidBody` (static), `Collider`, `ecs::WorldTransform` and a
  `Parent` gets a static body placed by its world transform (position and rotation; the scale is taken out, see below). Entities
  without a parent behave exactly as today (dynamic and kinematic bodies with a parent stay unsimulated, as today). The visit order
  (entity order) is unchanged, so body ids stay deterministic.
- **Following** (the user's choice): each frame, a static body whose entity's world position or rotation changed is re-placed there
  (a teleport, not a sweep: static bodies have no velocity) and the bodies touching it are woken, in entity order. A character standing
  on a static body that moves is carried by the next step's ground check, without a platform velocity (a kinematic body is the tool for
  moving platforms; the guide says so).
- **Scale (proposed by Claude, for the user to check):** the shape takes the entity's world scale **when its body is made** (at spawn, or
  when the collider is set): a box multiplies its size per axis, a sphere takes the largest axis (a counted note when not uniform within
  1e-4), a capsule takes the largest of X and Z for the radius and Y for the height, a mesh is rebuilt from the `MCOL` with the scale
  applied to its positions (a negative scale flips the winding). **A later scale change is not followed**: it is counted in the frame
  report and the body keeps its shape. Reason: physics' `MeshLibrary` has no removal, so re-baking a mesh for every scale an animation
  passes through would grow memory without a bound; following scale would need a mesh release path in physics first. A world matrix
  with a shear (within a tolerance) gives no body, counted.
- **The physics mesh**: the C API turns a loaded `MCOL` into a `physics::TriangleMesh` and adds it to the world's `MeshLibrary` once
  per (world, content hash of the `MCOL`, scale); a replaced `MCOL` (other bytes under the same id) makes a new mesh, and a world keeps
  its meshes until it is destroyed. The conversion lives in `engine_c` (physics does not depend on `asset`). Snapshots restore into the
  same world only, as today (mesh ids are the order meshes were added in that world); a restore into a re-spawned or another world is not
  supported, and the guide says so.
- **Loader**: `MCOL` gets a resource-manager loader (`ResourceTraits<MeshColliderView>`).
- **`level_spawn`**: for each entity with `static_collider` it adds a static `physics::RigidBody` and a `physics::Collider`, after the
  existing components. The values are checked before the world is touched (finite, sizes above 0 and at most 1e5, friction and
  restitution in range, the `MCOL` mounted and an `MCOL`); a bad one refuses the spawn whole.
- **Despawn**: when level despawn exists (the streaming request), destroying the entities removes their bodies, as destroying any body
  entity does today.

### 5. The C API's mesh shape

- `ENGINE_SHAPE_MESH` (4; 3 stays free for a future hull) and, at the end of `engine_collider`, `uint32_t reserved` and
  `uint64_t mesh_resource` (an `MCOL` id the host added or that an import made). A host of the first struct still sets spheres, boxes
  and capsules; a mesh needs the new fields (`STRUCT_SIZE`), a static body, and a mounted `MCOL` (`NOT_FOUND`, `TYPE_MISMATCH`).
  The collider and the body are separate components set in either order, so the static-only rule is checked on both set paths: a mesh
  collider set on an entity whose body is not static, or a body type other than static set on an entity with a mesh collider, is
  `INVALID_ARGUMENT`. `component_get` returns the id. The scale is the entity's world scale, as in section 4.
- The ABI snapshot is regenerated (no table function is added).

### 6. The character on a mesh

- The acceptance check uses the C API's character component and the Lua `character.*` on the same world.
- **Ground thickness rule** (Phase 25 Day 394): a triangle mesh has no inside; a body whose centre passes through a triangle is pushed
  out the other side. A character never passes through at walking or falling speeds (its move is a swept capsule), so a single-sided
  ground is enough for characters; a dynamic body falling fast onto a thin ground can tunnel (CCD is for convex shapes against
  meshes, Day 399). The guide states: give ground meshes closed volumes, or keep the ground at least as thick as a frame's fall.

## Tests (from the request, made concrete)

0. Physics: a static body with a parent is made at its world transform, follows a moved parent (re-placed, neighbours woken), ignores a
   later scale change (counted), and two worlds built the same way get the same body ids.
1. A `.glb` with a ground (`ground-col`, a grid of quads) and a box wall (`wall-colonly`): import, `level_spawn`, a character dropped
   above the ground lands (`is_grounded`, resting height within 1 mm of the expected), walking into the wall stops it.
2. The same file without suffixes: imports as before, `"colliders":0`.
3. A marked mesh with a NaN position: refused; an out-of-range index: refused; zero-area triangles: dropped with the counted warning;
   all without a crash under asan.
4. A step of `step_height` is climbed; one higher is a wall; a slope steeper than `min_ground_y` is a wall.
5. Import twice: equal `MCOL` bytes. The OMI file: a box, a sphere, a capsule, a trimesh; a cylinder, a trigger and a dynamic body give
   their warnings.
6. The C API mesh shape: set, read, refusals (old struct, dynamic body, missing resource).
7. The same tests on Windows and macOS (the user's machines, as for every series).

## Left to the builder

- The exact OMI field names and the physics-material form, from the current specification text.
- How the shared `MCOL` is keyed in the manifest, and the warning texts.
- The tolerance of the shear test and of "uniform".
- Where in `engine_c` the `MCOL`-to-mesh cache lives, as long as it is per world and freed with the world.
- The day split (a plan of about five days follows this document).
