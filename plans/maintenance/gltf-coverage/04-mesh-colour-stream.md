# glTF coverage / Day 4: vertex colours in the mesh blob (`MESH` v2)

**Series:** [glTF coverage](../gltf-coverage.md) (decision 3)
**Depends on:** nothing in the series.

## Tasks

- [ ] **Format** (`asset/mesh.h/.cpp`): `kMeshLocationColor = 4`; `MeshBuildInput::colors` (`Span<const Array<u8,4>>`, empty or one per
  vertex, sRGB-encoded rgb, linear alpha); the attribute `UNorm8x4` at location 4 in stream 0 (packed quality and the full layout);
  `MeshView::HasColors`; the header flag. **Bump the type version to 2**; a version 1 blob is refused with the offline upgrade named.
- [ ] **Upgrade offline:** `asset_mint remint` remints version 1 meshes (no colour); the committed mesh blobs and fixtures
  (`tests/engine_library/fixtures`, the demos' minted meshes) are reminted; `verify --require-current` passes.
- [ ] **`.mesh.src`:** a `color` column (four bytes, or four floats in 0..1 encoded to sRGB by the minter: decide, write it down).
- [ ] **The renderer** (`asset/gpu_loaders.cpp`, `renderer/mesh.*`): a mesh blob with colours gets the colour layout; a material on
  `gbuffer_color`/`gbuffer_skinned_color` with a mesh without colours is refused at bind time with a message (or white is used: decide;
  the renderer's existing rule for runtime meshes decides it).
- [ ] **Tests:** build, view and load a coloured mesh; the hostile-blob test covers the new attribute (a colour attribute of the wrong
  format, at the wrong offset, a colour flag without the attribute); a coloured quad on `gbuffer_color` draws its colours (pixel test).
- [ ] **Standing rows** (no shader changes).

## Notes

(written during the day)

**Done when:** a mesh blob can carry per-vertex colours, a coloured mesh draws them, and every committed mesh is version 2.
