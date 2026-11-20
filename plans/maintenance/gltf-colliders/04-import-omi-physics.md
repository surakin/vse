# glTF colliders / Day 4: the importer: `OMI_physics_body` and `OMI_physics_shape`

**Series:** [glTF colliders](../gltf-colliders.md) (design section 1)
**Depends on:** Day 3.

## Tasks

- [x] **Read the specification first**: the OMI group's current `OMI_physics_shape` and `OMI_physics_body` texts (field names, the
  capsule's `height` meaning, where friction and restitution live, the motion types). Record the version read and the field names in
  this file's notes; where they differ from the design's assumptions, follow the specification and note it.
- [x] **Tests first** (in `tests/asset/gltf_collider_test.cpp`):
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
- [x] **The reader** (`gltf_collision.cpp`): fills `GltfNodeCollision` from the OMI extensions; the summary counts primitives;
  `OMI_physics_body` and `OMI_physics_shape` added to `kSupported` in `gltf_container.cpp`.
- [x] **Docs**: `docs/guide/assets.md` (the OMI mapping table and the warnings).
- [x] **Standing rows** (series rules).

**Done:** a file using `OMI_physics_*` imports its static boxes, spheres, capsules and triangle meshes with every listed warning and
refusal tested, and `extensionsRequired` accepts the two extensions.

## Notes (2026-11-20, Linux)

**Tests are not run per day** (the user's rule for this series): built with Clang (unity) and GCC (unity off), reviewed; they run after Day 7.

**The specification read** (the OMI group's `OMI_physics_shape` and `OMI_physics_body` READMEs, "Stage 1 Proposal", no version number;
read 2026-11-20). Where it differs from the design's assumptions, it was followed:
- the capsule has `height` = the distance between the caps' centres (so `half_height` = `height / 2`) and two radii, `radiusTop` and
  `radiusBottom` (the larger is used, with a warning when they differ); defaults: box `size` [1, 1, 1], sphere `radius` 0.5, capsule
  `height` 1 and radii 0.5;
- static: "nodes with collider properties are assumed to be static without itself or a parent node having the motion property"; the
  motion types are `static`, `kinematic` and `dynamic`;
- physics materials live in the document-level `OMI_physics_body.physicsMaterials` (`staticFriction`, `dynamicFriction`, `restitution`,
  the combine modes); a collider names one with `physicsMaterial` (-1: the default). The engine has one friction: `dynamicFriction` is used;
  the combine modes are not (the engine's rule applies); with no material, the physics defaults (0.5, 0), not OMI's 0.6.
- triggers and collision filters (`collisionFilters`) are not imported.

**The security review of Day 3's commit** (automated, 2026-11-20): the shear check walked each marked node's ancestors and the scene
check each node's, each up to 1,024 steps (a matrix multiply per step): a hostile file of 200,000 nodes in a chain cost about 200 million
steps. Fixed: one pass computes every node's root, world matrix and (Day 4) "a moving OMI body at or above it", each from its parent's,
so the whole file costs one step per node.

- Tests (`tests/asset/gltf_collider_test.cpp`, written; run at the end): box, sphere, capsule, trimesh and a default sphere with their
  sizes; cylinder, convex, dynamic body, trigger and the name-and-OMI conflict warn; physics materials and their clamping; seven hostile
  cases refused naming the path.

## Post-implementation code review

`/code-review high` on the day's diff: 3 findings; 2 fixed, 1 kept.
1. **The motion check walked every OMI collider's ancestors** (quadratic in a deep chain, the same class as the security finding): fixed,
   it is part of the one pass.
2. **The security review's finding** (above): fixed.
3. **No test pins the linear cost** (a time bound would be flaky under the sanitizers): kept; Day 7's hostile mutation run covers large
   and deep files without a time bound.
