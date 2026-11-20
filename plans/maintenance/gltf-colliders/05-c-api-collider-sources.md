# glTF colliders / Day 5: the C API: collider sources, scaled shapes and `level_spawn`

**Series:** [glTF colliders](../gltf-colliders.md) (design section 4)
**Depends on:** Days 1, 2 and 3.

A shape's scale is the entity's world scale when its body is made, but a spawned entity's world matrix exists only after the frame's
transform propagation. So the C API keeps the unscaled shape as a **source** and makes the physics collider in the frame, after the
propagation and before the physics sync.

## Tasks

- [x] **Tests first** (`tests/engine_library/host_import.c`, a new `TestColliders`, plain C; files built inline like
  `kMorphsAndLights`):
  - a ground `ground-col` (a 10 x 10 m quad grid) and a `wall-colonly` box: import, `level_spawn`, one frame: two bodies exist
    (`physics_raycast` down from above the ground hits it at y = 0; a ray at the wall hits it);
  - a spawn with `scale` 2: the 10 m ground (centred at 0, half-size 5 m) becomes 20 m: a ray down at x = 9 m hits, at x = 11 m misses;
  - moving the spawn root by (100, 0, 0) and running a frame: the ray at the old place misses, at the new place hits;
  - changing the root's scale afterwards: the shapes stay; `engine_frame_stats` reports the ignored change;
  - in `tests/engine_library/level_spawn_test.cpp` (C++: a C host cannot rebuild a blob's hashes): a level whose `static_collider` box
    has a NaN size, rewritten and rebuilt as that file's light case does, is refused whole and the world is unchanged.
- [x] **The source component** (`engine/engine_c/runtime.h`): `ColliderSource` (the unscaled shape: kind, `MCOL` id, sizes, friction,
  restitution) and, once made, the scale it was made at. `level_spawn` adds `ColliderSource` and a static `physics::RigidBody` to each
  entity with `asset::StaticCollider`, after checking every one first (finite, sizes above 0 and at most 1e5 after the spawn scale,
  friction >= 0, restitution 0..1, the `MCOL` mounted and an `MCOL`).
- [x] **The resolver** (after the frame's transform propagation, so before the next frame's physics sync: the C API frame runs
  `SyncBodies` before its schedule, Day 1's notes; the collider is made only once the entity has a propagated world matrix, never from the
  identity default): for an entity with
  `ColliderSource` and no `physics::Collider`, read `ecs::WorldTransform`'s scale (column lengths; a shear within 1e-4 of the columns'
  orthogonality gives no collider, counted), make the scaled shape (box per axis; sphere the largest axis; capsule max(X, Z) radius and Y
  half-height; mesh: the cached physics mesh for (content hash, scale)), set `physics::Collider`. An entity whose world scale later
  differs from the made scale by more than 1e-6 relative: counted, unchanged.
- [x] **The mesh cache** (`engine/engine_c/collider_meshes.{h,cpp}`, new; per world, freed with it): key (the `MCOL`'s content hash,
  the scale as three `f32` bit patterns) to the `MeshLibrary` id; builds the `physics::TriangleMesh` from `MeshColliderView` with the
  positions scaled (a negative determinant flips each triangle's winding) and the degenerate triangles of the scaled mesh dropped
  (`MeshColliderTriangleHasArea`), counted; a mesh left empty gives no collider, counted. The `MCOL` is loaded and pinned through the
  resource host (as clips are).
- [x] **Stats**: `engine_frame_stats` gains, at its end, `colliders_made`, `colliders_refused` (shear, empty mesh) and
  `collider_scale_changes_ignored` (documented; the ABI snapshot regenerated).
- [x] **Docs**: `engine_c.h` (`level_spawn`: bodies; the stats), `docs/guide/engine-library.md` (a level's colliders, following, scale
  at creation, snapshots in the same world only).
- [x] **Standing rows** (series rules; asan and tsan for the cache and the resolver).

**Done:** a spawned level with colliders is solid at the next frame, follows its root, keeps its creation scale with the change counted,
refuses a bad level whole, and the tests pass plain, asan and tsan.

## Notes (2026-11-20, Linux)

**Tests are not run per day** (the user's rule for this series): built with Clang (unity) and GCC (unity off), reviewed; they run after Day 7.

- `ColliderSource` (`runtime.h`): the unscaled shape, plus the body offset and rotation, the material and the filter (Day 6's mesh
  shape uses them), and its state (waiting, made, refused) and the scale it was made with. `ColliderMeshEntry` and
  `WorldObject::collider_meshes`: the world's physics meshes by (`MCOL` content hash, scale bits).
- `collider_meshes.{h,cpp}`: `ResolveColliderSources` runs after the world's schedule (so after the transform system) in every frame of a
  world that has sources: a waiting source gets its `physics::Collider` at the entity's world scale (column lengths; a shear over 1e-4 of
  the columns' product or a zero column refuses it; a mirror is a negative z, matching Day 1's flipped third axis, and flips the mesh's
  winding); a made one whose scale changed is counted once. In entity order, so two worlds add their meshes in the same order. The mesh is
  rebuilt from the `MCOL` with the scale applied and the triangles that scale made degenerate dropped (counted).
- `ResourceHost::PinMeshCollider`: loads and pins the `MCOL`, and hashes its bytes once per loaded blob (`MeshColliderView::Bytes()`, new);
  a replaced resource is a new load, so a new hash and a new mesh.
- `level_spawn`: checks every `static_collider` before the world is touched (finite, sizes in range, friction, restitution, the `MCOL`
  pinned) and adds `ColliderSource` and a static `physics::RigidBody`; the world's resolve flag is set when the spawn succeeds.
- `engine_frame_stats` gains `colliders_made`, `colliders_refused`, `collider_scale_changes_ignored` and `collider_triangles_dropped` (the
  frame copies only what the host's struct holds, as before); the ABI snapshot lists them and `engine_import_options`' new field.
- **Timing, as the C API runs a frame:** the resolver makes the collider at the end of the frame that first propagates the entity; the
  physics sync at the start of the next frame makes the body; its step builds the query index. So a raycast finds a spawned level after
  two frames (documented in `engine_c.h` and the guide).
- Tests (written; run at the end): `host_import.c` `TestColliders`: two colliders made in the first frame, raycasts hit the ground and the
  wall, a moved root moves them, a later scale change is counted once and ignored, a spawn at scale 2 doubles the ground, the triangle
  limit refuses with `TOO_LARGE` and an old options struct is served; `level_spawn_test.cpp`: a level whose box has a NaN size is refused
  whole.

## Post-implementation code review

`/code-review high` on the day's diff: 4 findings; 2 fixed, 2 kept.
1. **A hand-written insertion sort** of the waiting sources: replaced by the engine's `Sort` (and it is the order that keeps the mesh ids
   deterministic, so it stays).
2. **Seeded ids collided between the C test's imports** (the default seed of `Options`): fixed, each import has its own seed.
3. **The resolver looks at every collider entity every frame** (to notice a scale change): kept; it is one query per world with sources,
   and the alternative (change tracking on `WorldTransform`) would miss nothing it now catches.
4. **A made collider whose entity later gets a shear in its matrix** is neither counted nor changed: kept; the body keeps its shape (the
   scale rule), and the next spawn of such a level counts it as refused.
