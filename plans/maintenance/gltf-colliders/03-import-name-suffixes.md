# glTF colliders / Day 3: the importer: name suffixes, `MCOL` per mesh, limits and the summary

**Series:** [glTF colliders](../gltf-colliders.md) (design sections 1-3)
**Depends on:** Day 2.

## Tasks

- [x] **Tests first** (`tests/asset/gltf_collider_test.cpp`, new; its line in `tests/phases.txt`, phase 22; build the files with
  `tests/asset/gltf_writer.h`):
  - "a `-col` node is drawn and is a collider": the entity has its `MeshReference` and a `static_collider` (mesh) naming an `MCOL`
    whose triangles are the mesh's (count and positions, the import's `scale` applied);
  - "a `-colonly` node is a collider only": no `MeshReference`; no mesh blob is made for a glTF mesh no other node draws;
  - "two nodes that use one mesh share one `MCOL`"; "`wall-col.001` is matched"; "`-col` in the middle of a name is not";
    "`-convcol` is a triangle mesh with the convex warning";
  - "zero-area triangles are dropped and counted" (one warning per collider with the count); "a collider with no triangle left is
    dropped"; "a non-finite position refuses the import naming the accessor"; "an index out of range refuses the import";
  - "a marked node without a mesh warns"; "a marked mesh with a `LINES` primitive warns and keeps its `TRIANGLES` ones";
    "morph targets or a skin on a marked mesh warn" (the base shape is used);
  - "a marked node that a clip animates warns"; "a marked node whose rest world matrix has a shear warns";
  - "`max_collision_triangles` refuses an import over it with `TooLarge`", and the setting is its default;
  - "a file without marks gives byte-identical blobs to today's apart from the version" (compare every resource's bytes of the
    existing test files except for what the output version changes) and `"colliders":0`;
  - "two imports give equal `MCOL` bytes and ids" (seeded ids, and again through a manifest).
- [x] **The convention reader** (`engine/asset_import/gltf_collision.{h,cpp}`, new, in the module's sources): reads each node's
  collision mark into a per-node `GltfNodeCollision` (none, mesh-with-draw, mesh-only, primitive...; Day 4 fills primitives), with
  the suffix rule: case-sensitive `-col`, `-colonly`, `-convcol`, `-convcolonly` at the end of the name, after removing a trailing
  `.` and digits.
- [x] **Minting** (in `gltf_collision.cpp`, called from `ImportGltf` after the meshes and before the level): one `MCOL` per glTF mesh
  that a marked node uses, from all its `TRIANGLES` primitives (base positions, the import's `scale`), degenerate triangles dropped with
  `MeshColliderTriangleHasArea`, minted with `BuildMeshColliderBlob`; path `<prefix>/colliders/<mesh name>`; ids through `AssignIds`
  with a new `ImportKind::MeshCollider` (or a key range of an existing kind, as the converted textures did: pick one and document the
  manifest form); `output_bytes` counted against `max_output_bytes`.
- [x] **The level** (`gltf_level.cpp`): a marked node's entity gets `asset::StaticCollider` (mesh); a `-colonly` node's entity gets no
  `MeshReference`; the mesh importer skips a glTF mesh that only `-colonly` nodes use.
- [x] **Limits**: `ImportLimits::max_collision_triangles` (default 4,194,304), the setting `assets.import.max_collision_triangles`
  (`import_settings.cpp`, env and flag names as the other import settings), and `engine_import_options.max_collision_triangles`
  appended to the C struct (0 takes the setting; a host of the old struct gets the setting); the C API's import passes it on. Test in
  `tests/engine_library/host_import.c`: a limit of 1 refuses a marked file with `TOO_LARGE`.
- [x] **The summary** (`gltf_summary.cpp`): `"collision":{"colliders":N,"meshes":M,"triangles":T,"primitives":P,"dropped":D}` after
  `"lights"`; `"version"` stays 1. Documented in `gltf_summary.h`, `engine_c.h` (the summary's member list) and `docs/guide/assets.md`.
- [x] **Versions**: `kGltfImporterOutputVersion` 5 with its history line; `host_import.c`'s `EXPECTED_FORMAT_VERSION` updated from the
  test's printed value.
- [x] **Docs**: the guide's "Importing glTF" section: the suffixes, what is drawn, the warnings, the limit, the summary member.
- [x] **Standing rows** (series rules).

**Done:** a `.glb` with `-col`/`-colonly` meshes imports to `MCOL`s and `static_collider` entities, every listed refusal and warning has
its test, a file without marks is unchanged, imports are deterministic, and the tests pass plain and asan.

## Notes (2026-11-20, Linux)

**Tests are not run per day** (the user's rule for this series): built with Clang (unity) and GCC (unity off), reviewed; they run after Day 7.

- `gltf_collision.{h,cpp}`: `ReadCollisionMarks` (the suffix rule, the warnings), `CollisionOnlyMeshes`, `ImportColliders` (one `MCOL` per
  glTF mesh, `ImportKind::MeshCollider` keyed by the mesh index, path `<prefix>/colliders/<mesh name>`). `ImportReport` gains the five
  collision counts; `ImportLimits::max_collision_triangles` (4,194,304), the setting `assets.import.max_collision_triangles`,
  `engine_import_options.max_collision_triangles` (appended; a host of the first struct is served, the setting applies) and the C API's
  `ImportLimitValues` carry it.
- **Only with a level**: collision is read only when the import makes a level, and only for the nodes under the default scene (a marked
  node elsewhere makes no `MCOL` and is not counted). Without a level, a marked file imports exactly as before.
- **Changes to the plan**: no "a clip animates a marked node" warning: the importer does not import node animation at all (non-joint
  channels are already dropped with a warning), so a marked node never moves from a clip. The C `TOO_LARGE` test of the limit is in
  Day 5's `TestColliders` (it needs a marked file in C, which that day builds). "Byte-identical to today" for a file without marks is
  covered by "no `MCOL`, every mesh made, `"colliders":0`" (a byte comparison with a previous build's output is not available in a test).
- Unity build: `gltf_mesh.cpp`'s file-local `Vec3` (f64) shadowed the engine's `Vec3` in `level.h` when merged; renamed `MeshVec3d`
  (CLAUDE.md's step 2). The new file's `Refuse` collided with `gltf_level.cpp`'s: renamed `RefuseCollider`.
- `kGltfImporterOutputVersion` 5. `host_import.c`'s `EXPECTED_FORMAT_VERSION` is updated from the printed value in the final test run.
- The summary's `"collision"` member; `gltf_mesh_test.cpp`'s lights case now expects `],"collision":{` after the lights.
- Tests (`tests/asset/gltf_collider_test.cpp`, written; run at the end): `-col` drawn and solid (scale applied, summary); `-colonly` not
  drawn, mesh made when another node draws it; the suffix rules (duplicate number, middle, case, convex); degenerate triangles dropped and
  counted; no triangle left and no mesh dropped; NaN position and index out of range refused; LINES, morph and shear warnings; the limit
  and its setting; no marks; outside the scene; determinism and the manifest.

## Post-implementation code review

`/code-review high` on the day's diff: 4 findings; 3 fixed, 1 kept.
1. **Marked nodes outside the default scene were read**: they made `MCOL`s and counted as colliders though no entity carries them. Fixed:
   only nodes under the default scene's roots are read; test "a marked node outside the default scene makes nothing".
2. **The lights summary test pinned the old end of the JSON**: fixed (`],"collision":{`).
3. **Unity-build collisions** (`Vec3`, `Refuse`): fixed by renaming, as the rules ask.
4. **A collision-only mesh is still parsed for collision even though no render mesh is made, so an unsupported primitive mode in it only
   warns** (a drawn mesh would be refused): kept: collision needs only triangles and the warning names the primitive.
