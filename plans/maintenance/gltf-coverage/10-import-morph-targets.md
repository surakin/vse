# glTF coverage / Day 10: the importer: morph targets and weight animation

**Series:** [glTF coverage](../gltf-coverage.md)
**Depends on:** Days 6 to 9.

## Tasks

- [ ] **Targets** (`asset_import/gltf_mesh.cpp`): a primitive's `targets` (`POSITION`, `NORMAL`, `TANGENT` deltas; sparse accessors, which
  morph targets usually are, already read) become the mesh's morph targets; every primitive of a mesh must have the same target count
  (glTF requires it: refuse otherwise, naming the primitive); names from `mesh.extras.targetNames` (else `target<N>`); more than 64
  refused. Generated normals and tangents (no `NORMAL` in the base) and morphs: decide whether target normal deltas are generated too, or
  left zero with a warning. The refusal goes; `drop_morph_targets` stays as an option.
- [ ] **Weights:** `mesh.weights` and `node.weights` (the node's wins) become the level entity's `morph_weights`.
- [ ] **Animation** (`asset_import/gltf_clip.cpp`): `weights` channels become weight tracks of the clip, `LINEAR`, `STEP` and `CUBICSPLINE`
  as the TRS channels are handled (cubic resampled at `clip_rate`, the error recorded); the warning that drops them goes.
- [ ] **Summary** (`gltf_summary.cpp`): the meshes' target names.
- [ ] **Tests** (`tests/asset/gltf_mesh_test.cpp` and the clip tests beside `gltf_skin_test.cpp`): glTF files built in the tests: two targets with sparse deltas, a mesh whose primitives disagree (refused), `targetNames`,
  node weights over mesh weights, a weights animation in each interpolation; the imported level plays it (an entity's `MorphWeights`
  after N ticks); deterministic re-import.
- [ ] **Look:** the model viewer plays a morph animation (`N` cycles clips).
- [ ] **Standing rows.**

## Notes

(written during the day)

**Done when:** a glTF with morph targets and weight animation imports and plays, without the refusal or the dropped-channel warning.
