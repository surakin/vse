# glTF colliders / Day 6: the C API: `ENGINE_SHAPE_MESH`

**Series:** [glTF colliders](../gltf-colliders.md) (design section 5)
**Depends on:** Day 5.

## Tasks

- [x] **Tests first** (`tests/engine_library/host_physics.c`, a new `TestMeshCollider`; the `MCOL` made with `import_gltf` of a small
  `-colonly` file, or added from bytes the test builds through the importer):
  - an entity with a static body and an `ENGINE_SHAPE_MESH` collider naming the `MCOL`: after a frame a ray hits it; `component_get`
    returns the shape and the id;
  - a host of the first `engine_collider` (struct ending before the new fields) still sets a box, and gets `STRUCT_SIZE` for a mesh;
  - a mesh collider on an entity whose body is dynamic, and a dynamic body set on an entity with a mesh collider: `INVALID_ARGUMENT`,
    either order;
  - an id that is not mounted: `NOT_FOUND`; a resource that is not an `MCOL`: `TYPE_MISMATCH`;
  - the entity's world scale 3: the ray hits the scaled mesh.
- [x] **The header** (`engine_c.h`): `ENGINE_SHAPE_MESH 4u` (3 left free for a hull); `engine_collider` gains at its end `uint32_t
  reserved` and `uint64_t mesh_resource`; the comment's "refused for now" sentence rewritten (hulls and compounds stay refused).
- [x] **The runtime** (`runtime.cpp`): the collider's set path takes the first-version size as today and needs the full size for a mesh;
  a mesh shape becomes a `ColliderSource` (Day 5) instead of a direct `physics::Collider`; the static-only rule on both the collider's
  and the rigid body's set paths; `component_get` of a collider reads back from the source when there is one.
- [x] **ABI**: `abi_snapshot_gen.c` gets the new value and fields; the snapshot regenerated.
- [x] **Docs**: `engine_c.h`, `docs/guide/engine-library.md` (physics section: mesh colliders from a resource).
- [x] **Standing rows** (series rules).

**Done:** a host gives a static body a mesh collider from an `MCOL` through `component_set`, with every refusal tested, and older hosts
are unaffected.

## Notes (2026-11-20, Linux)

**Tests are not run per day** (the user's rule for this series): built with Clang (unity) and GCC (unity off), reviewed; they run after Day 7.

- `ENGINE_SHAPE_MESH` (4), `engine_collider.reserved` and `.mesh_resource` (appended; the first version is the size before `reserved`),
  and `ENGINE_RESOURCE_MESH_COLLIDER` (`MCOL`), so a host finds the collider among an import's resources.
- A mesh collider is a `ColliderSource` (Day 5), made by the frame at the entity's world scale; `SetMeshCollider` checks the struct size,
  `reserved`, the layer, the trigger flag, the static-only rule, the offset/rotation/material (as a primitive's) and pins the `MCOL`
  (`NOT_FOUND`, `TYPE_MISMATCH`). The body's set path refuses a non-static type under a mesh collider. A primitive set later replaces a
  mesh source; `component_remove` of the collider removes the source too. `component_get` returns the host's own values for a mesh
  collider, and both forms copy only what the host's struct holds.
- **Changed from the plan:** the test is in `host_import.c` (`TestMeshShape`), not `host_physics.c`: it needs the importer to make the
  `MCOL`, and `host_import` is the program built with it.
- Tests (written; run at the end): a static body with a mesh collider at world scale 3 is hit at x = 12 and missed at x = 16; get returns
  the shape and the id; static-only both ways; not mounted and not an `MCOL`; the first struct sets a box, is refused a mesh, and reading
  does not write past it.

## Post-implementation code review

`/code-review high` on the day's diff: 3 findings; 3 fixed.
1. **`component_get` of a collider copied the whole new struct into a host's older one** (it wrote `sizeof(result)`): fixed, it copies what
   the host's `struct_size` holds; the test checks `reserved` is untouched.
2. **A mesh collider's `is_trigger` was checked and dropped**: fixed, the source carries it to the physics collider and back.
3. **`engine_collider` was listed twice in the ABI snapshot generator** after a first edit: fixed; the snapshot shows only the two appended
   fields and the size.
