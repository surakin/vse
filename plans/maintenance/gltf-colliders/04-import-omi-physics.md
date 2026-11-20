# glTF colliders / Day 4: the importer: `OMI_physics_body` and `OMI_physics_shape`

**Series:** [glTF colliders](../gltf-colliders.md) (design section 1)
**Depends on:** Day 3.

## Tasks

- [ ] **Read the specification first**: the OMI group's current `OMI_physics_shape` and `OMI_physics_body` texts (field names, the
  capsule's `height` meaning, where friction and restitution live, the motion types). Record the version read and the field names in
  this file's notes; where they differ from the design's assumptions, follow the specification and note it.
- [ ] **Tests first** (in `tests/asset/gltf_collider_test.cpp`):
  - a body with a `box`, one with a `sphere`, one with a `capsule`: `static_collider` with the shape and the unscaled sizes (half of
    `size` for a box; the capsule's `half_height` from the specification's `height`);
  - a `trimesh` shape: an `MCOL` from its mesh, as on Day 3 (shared with a suffix collider of the same mesh);
  - `convex`: a triangle mesh with the convex warning; `cylinder`: dropped with a warning;
  - `motion` `kinematic` or `dynamic`: a static collider with the "dynamic bodies are static" warning; a `trigger`: dropped with a warning;
  - a node with an OMI collider and a `-col` suffix: the OMI one, with a warning;
  - friction and restitution read where the specification puts them; values out of range are clamped with a warning;
  - hostile: a shape index out of range, a negative or non-finite size, a missing `shapes` array, a `mesh` index out of range: refused
    naming the JSON path, no crash (asan);
  - `extensionsRequired` naming either extension imports.
- [ ] **The reader** (`gltf_collision.cpp`): fills `GltfNodeCollision` from the OMI extensions; the summary counts primitives;
  `OMI_physics_body` and `OMI_physics_shape` added to `kSupported` in `gltf_container.cpp`.
- [ ] **Docs**: `docs/guide/assets.md` (the OMI mapping table and the warnings).
- [ ] **Standing rows** (series rules).

**Done:** a file using `OMI_physics_*` imports its static boxes, spheres, capsules and triangle meshes with every listed warning and
refusal tested, and `extensionsRequired` accepts the two extensions.
