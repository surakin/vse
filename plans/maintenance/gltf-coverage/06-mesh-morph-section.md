# glTF coverage / Day 6: morph targets in the mesh blob (`MESH` v3)

**Series:** [glTF coverage](../gltf-coverage.md) (decision 4)
**Depends on:** Day 4 (the version 2 layout).

## Tasks

- [ ] **Format** (`asset/mesh.h/.cpp`): section 40 `MORP` stops being reserved: a target table (per target: name up to 31 bytes, the
  delta count, the offset, which deltas it has: position, normal, tangent) and the deltas (vertex index `u32`, position delta `f32x3`,
  normal delta `f32x3`, tangent delta `f32x3`; the absent ones not stored), sorted by vertex index, no repeats. Limits:
  `kMaxMorphTargets = 64`, deltas per target at most the vertex count. **Type version 3**; version 2 reminted offline.
- [ ] **Bounds:** the mesh box grows to hold every target at weight 1 and at weight -1 (conservative: the base box plus the box of each
  target's position deltas, summed); write the rule in the header.
- [ ] **Input and view:** `MeshBuildInput::morph_targets` (names and per-target dense or sparse deltas: the builder drops zero deltas,
  bit-exact zero); `MeshView::MorphTargetCount`, `MorphTarget(i)` (name, deltas as spans into the blob: zero-copy).
- [ ] **`.mesh.src`:** a `morph NAME` block of `vertex dx dy dz [nx ny nz [tx ty tz]]` lines.
- [ ] **Tests:** build and view round trip; the bounds rule; the hostile-blob test covers the section (a delta past the vertices, unsorted
  or repeated indices, a count over the limit, a name too long, an offset outside the section); deterministic bytes; `remint` of the
  committed meshes.
- [ ] **Standing rows.**

## Notes

(written during the day)

**Done when:** a mesh blob holds up to 64 named morph targets, read zero-copy, and every committed mesh is version 3.
