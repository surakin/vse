# glTF colliders / Day 5: the C API: collider sources, scaled shapes and `level_spawn`

**Series:** [glTF colliders](../gltf-colliders.md) (design section 4)
**Depends on:** Days 1, 2 and 3.

A shape's scale is the entity's world scale when its body is made, but a spawned entity's world matrix exists only after the frame's
transform propagation. So the C API keeps the unscaled shape as a **source** and makes the physics collider in the frame, after the
propagation and before the physics sync.

## Tasks

- [ ] **Tests first** (`tests/engine_library/host_import.c`, a new `TestColliders`, plain C; files built inline like
  `kMorphsAndLights`):
  - a ground `ground-col` (a 10 x 10 m quad grid) and a `wall-colonly` box: import, `level_spawn`, one frame: two bodies exist
    (`physics_raycast` down from above the ground hits it at y = 0; a ray at the wall hits it);
  - a spawn with `scale` 2: the 10 m ground (centred at 0, half-size 5 m) becomes 20 m: a ray down at x = 9 m hits, at x = 11 m misses;
  - moving the spawn root by (100, 0, 0) and running a frame: the ray at the old place misses, at the new place hits;
  - changing the root's scale afterwards: the shapes stay; `engine_frame_stats` reports the ignored change;
  - in `tests/engine_library/level_spawn_test.cpp` (C++: a C host cannot rebuild a blob's hashes): a level whose `static_collider` box
    has a NaN size, rewritten and rebuilt as that file's light case does, is refused whole and the world is unchanged.
- [ ] **The source component** (`engine/engine_c/runtime.h`): `ColliderSource` (the unscaled shape: kind, `MCOL` id, sizes, friction,
  restitution) and, once made, the scale it was made at. `level_spawn` adds `ColliderSource` and a static `physics::RigidBody` to each
  entity with `asset::StaticCollider`, after checking every one first (finite, sizes above 0 and at most 1e5 after the spawn scale,
  friction >= 0, restitution 0..1, the `MCOL` mounted and an `MCOL`).
- [ ] **The resolver** (a system in the C world's schedule, after transform propagation and before the physics sync): for an entity with
  `ColliderSource` and no `physics::Collider`, read `ecs::WorldTransform`'s scale (column lengths; a shear within 1e-4 of the columns'
  orthogonality gives no collider, counted), make the scaled shape (box per axis; sphere the largest axis; capsule max(X, Z) radius and Y
  half-height; mesh: the cached physics mesh for (content hash, scale)), set `physics::Collider`. An entity whose world scale later
  differs from the made scale by more than 1e-6 relative: counted, unchanged.
- [ ] **The mesh cache** (`engine/engine_c/collider_meshes.{h,cpp}`, new; per world, freed with it): key (the `MCOL`'s content hash,
  the scale as three `f32` bit patterns) to the `MeshLibrary` id; builds the `physics::TriangleMesh` from `MeshColliderView` with the
  positions scaled (a negative determinant flips each triangle's winding) and the degenerate triangles of the scaled mesh dropped
  (`MeshColliderTriangleHasArea`), counted; a mesh left empty gives no collider, counted. The `MCOL` is loaded and pinned through the
  resource host (as clips are).
- [ ] **Stats**: `engine_frame_stats` gains, at its end, `colliders_made`, `colliders_refused` (shear, empty mesh) and
  `collider_scale_changes_ignored` (documented; the ABI snapshot regenerated).
- [ ] **Docs**: `engine_c.h` (`level_spawn`: bodies; the stats), `docs/guide/engine-library.md` (a level's colliders, following, scale
  at creation, snapshots in the same world only).
- [ ] **Standing rows** (series rules; asan and tsan for the cache and the resolver).

**Done:** a spawned level with colliders is solid at the next frame, follows its root, keeps its creation scale with the change counted,
refuses a bad level whole, and the tests pass plain, asan and tsan.
