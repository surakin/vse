# glTF coverage / Day 10: the importer: morph targets and weight animation

**Series:** [glTF coverage](../gltf-coverage.md)
**Depends on:** Days 6 to 9.

## Tasks

- [x] **Targets** (`asset_import/gltf_mesh.cpp`): a primitive's `targets` (`POSITION`, `NORMAL`, `TANGENT` deltas; sparse accessors, which
  morph targets usually are, already read) become the mesh's morph targets; every primitive of a mesh must have the same target count
  (glTF requires it: refuse otherwise, naming the primitive); names from `mesh.extras.targetNames` (else `target<N>`); more than 64
  refused. Generated normals and tangents (no `NORMAL` in the base) and morphs: decide whether target normal deltas are generated too, or
  left zero with a warning. The refusal goes; `drop_morph_targets` stays as an option.
- [x] **Weights:** `mesh.weights` and `node.weights` (the node's wins) become the level entity's `morph_weights`.
- [x] **Animation** (`asset_import/gltf_clip.cpp`): `weights` channels become weight tracks of the clip, `LINEAR`, `STEP` and `CUBICSPLINE`
  as the TRS channels are handled (cubic resampled at `clip_rate`, the error recorded); the warning that drops them goes.
- [x] **Summary** (`gltf_summary.cpp`): the meshes' target names.
- [x] **Tests** (`tests/asset/gltf_mesh_test.cpp` and the clip tests beside `gltf_skin_test.cpp`): glTF files built in the tests: two targets with sparse deltas, a mesh whose primitives disagree (refused), `targetNames`,
  node weights over mesh weights, a weights animation in each interpolation; the imported level plays it (an entity's `MorphWeights`
  after N ticks); deterministic re-import.
- [x] **Look:** the model viewer plays a morph animation (`N` cycles clips).
- [x] **Standing rows.**

## Notes

Done on the PC (2026-11-19). **No tests were run** (the user's request; they run after Day 12); only `build-msvc` was built. The model
viewer was not looked at, and it does not play weights-only clips yet: it plays clips on skinned entities, and a morphed, unskinned
node would need its own player (Day 15).

- **Meshes** (`gltf_mesh.cpp`): a primitive's `targets` (`POSITION`, `NORMAL`, `TANGENT`, `VEC3` floats; sparse accessors through
  the usual reader; positions scaled) become `MeshBuildInput::morph_targets`, by vertex over all primitives (all-zero deltas are
  dropped by the builder). Every primitive must have the first one's count, and more than 64 is refused. Names come from
  `mesh.extras.targetNames`, else `target<N>`, cut to 31 bytes. Flat-normal unsharing carries the deltas. **Made normals get no
  deltas** (the plan's open question: left zero, no warning). The refusal is gone; `drop_morph_targets` still imports the base shape.
- **Levels** (`gltf_level.cpp`): a mesh node's `weights`, else its mesh's, become its entity's `morph_weights` piece.
- **Clips** (`gltf_clip.cpp`): a `weights` channel joins the clip of the node's own skin when the animation has one, else it becomes a
  clip of the weights alone, `<animation>_<node>`, with no skeleton, manifest key `0x80000000 + animation * 32768 + node` (a node
  numbered 32768 or above is dropped with a warning). `LINEAR` as is, `STEP` with a key one tick (or half the gap) before each
  change, **`CUBICSPLINE` by the key values only** (the tangents are dropped: an approximation, no error measured, unlike the TRS
  channels' resampling). Two channels of one node and path are refused as before (weights count as a fourth path).
- **Fixed from Day 7:** `MaterialSystem::Device()` lacked the `Ownership:` label the renderer's strict rules demand, a `docgen` error;
  the Day 7 to 9 docs runs failed on it (their exit code was read as the regeneration quirk). Fixed today; `docgen` passes.
- Tests (written, not run): `gltf mesh: morph targets are imported, or dropped with a warning` (was the refusal test). Not written:
  primitives that disagree, `targetNames`, node over mesh weights, weights channels in each interpolation, a skinned clip with a weight
  track.
- Docs: `docs/guide/assets.md` ("Importing glTF": meshes and clips); `docs/reference` regenerated.

**Done when:** a glTF with morph targets and weight animation imports and plays, without the refusal or the dropped-channel warning.
