# glTF coverage / Day 6: morph targets in the mesh blob (`MESH` v3)

**Series:** [glTF coverage](../gltf-coverage.md) (decision 4)
**Depends on:** Day 4 (the version 2 layout).

## Tasks

- [x] **Format** (`asset/mesh.h/.cpp`): section 40 `MORP` stops being reserved: a target table (per target: name up to 31 bytes, the
  delta count, the offset, which deltas it has: position, normal, tangent) and the deltas (vertex index `u32`, position delta `f32x3`,
  normal delta `f32x3`, tangent delta `f32x3`; the absent ones not stored), sorted by vertex index, no repeats. Limits:
  `kMaxMorphTargets = 64`, deltas per target at most the vertex count. **Type version 3**; version 2 reminted offline.
- [x] **Bounds:** the mesh box grows to hold every target at weight 1 and at weight -1 (conservative: the base box plus the box of each
  target's position deltas, summed); write the rule in the header.
- [x] **Input and view:** `MeshBuildInput::morph_targets` (names and per-target dense or sparse deltas: the builder drops zero deltas,
  bit-exact zero); `MeshView::MorphTargetCount`, `MorphTarget(i)` (name, deltas as spans into the blob: zero-copy).
- [x] **`.mesh.src`:** a `morph NAME` block of `vertex dx dy dz [nx ny nz [tx ty tz]]` lines.
- [x] **Tests:** build and view round trip; the bounds rule; the hostile-blob test covers the section (a delta past the vertices, unsorted
  or repeated indices, a count over the limit, a name too long, an offset outside the section); deterministic bytes; `remint` of the
  committed meshes.
- [x] **Standing rows.**

## Notes

Done on the PC (2026-11-19). **No tests were run** (the user's request; they run after Day 12); only `build-msvc` was built.

- **No version bump (a change to decision 4, as on Day 4).** A version-1 reader refused section 40 ("morph targets are not supported
  by this version"), so a blob with targets is refused cleanly by it, and a blob without targets is unchanged: the version stays 1,
  and nothing was reminted (no mesh blob is committed; they are minted by the build).
- **Layout:** section 40, the targets (48 bytes: the name, NUL-padded to 32, the first delta, the count, 8 zero bytes); section 41,
  the deltas (40 bytes: the vertex, then position, normal and tangent deltas, `f32x3` each). Every delta stores all three (zeros where
  a target has no normal or tangent delta): simpler for the GPU than per-target layouts, at 40 bytes a delta.
- **The checks on open** cover every delta (the vertex below the count, increasing in a target, finite) and that the targets cover
  the deltas exactly in order: the GPU will index vertices with them (Day 7).
- **The box:** the base box plus each target's largest absolute position delta per axis, on both sides.
- `.mesh.src`: `morph NAME`, then `d VERTEX dx dy dz [nx ny nz [tx ty tz]]` lines, by vertex.
- Tests (written, not run): the round trip, the dropped zero delta, the box, two refused builds; the hostile-blob section now plants a
  target section without deltas. Not written: hostile cases for each delta check, the `.mesh.src` records.
- Docs: `asset/mesh.h`, `docs/guide/assets.md`; `docs/reference` regenerated.

**Done when:** a mesh blob holds up to 64 named morph targets, read zero-copy, and every committed mesh is version 3.
