# glTF colliders / Day 6: the C API: `ENGINE_SHAPE_MESH`

**Series:** [glTF colliders](../gltf-colliders.md) (design section 5)
**Depends on:** Day 5.

## Tasks

- [ ] **Tests first** (`tests/engine_library/host_physics.c`, a new `TestMeshCollider`; the `MCOL` made with `import_gltf` of a small
  `-colonly` file, or added from bytes the test builds through the importer):
  - an entity with a static body and an `ENGINE_SHAPE_MESH` collider naming the `MCOL`: after a frame a ray hits it; `component_get`
    returns the shape and the id;
  - a host of the first `engine_collider` (struct ending before the new fields) still sets a box, and gets `STRUCT_SIZE` for a mesh;
  - a mesh collider on an entity whose body is dynamic, and a dynamic body set on an entity with a mesh collider: `INVALID_ARGUMENT`,
    either order;
  - an id that is not mounted: `NOT_FOUND`; a resource that is not an `MCOL`: `TYPE_MISMATCH`;
  - the entity's world scale 3: the ray hits the scaled mesh.
- [ ] **The header** (`engine_c.h`): `ENGINE_SHAPE_MESH 4u` (3 left free for a hull); `engine_collider` gains at its end `uint32_t
  reserved` and `uint64_t mesh_resource`; the comment's "refused for now" sentence rewritten (hulls and compounds stay refused).
- [ ] **The runtime** (`runtime.cpp`): the collider's set path takes the first-version size as today and needs the full size for a mesh;
  a mesh shape becomes a `ColliderSource` (Day 5) instead of a direct `physics::Collider`; the static-only rule on both the collider's
  and the rigid body's set paths; `component_get` of a collider reads back from the source when there is one.
- [ ] **ABI**: `abi_snapshot_gen.c` gets the new value and fields; the snapshot regenerated.
- [ ] **Docs**: `engine_c.h`, `docs/guide/engine-library.md` (physics section: mesh colliders from a resource).
- [ ] **Standing rows** (series rules).

**Done:** a host gives a static body a mesh collider from an `MCOL` through `component_set`, with every refusal tested, and older hosts
are unaffected.
