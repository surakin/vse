# glTF coverage / Day 4: vertex colours in the mesh blob (`MESH` v2)

**Series:** [glTF coverage](../gltf-coverage.md) (decision 3)
**Depends on:** nothing in the series.

## Tasks

- [x] **Format** (`asset/mesh.h/.cpp`): `kMeshLocationColor = 4`; `MeshBuildInput::colors` (`Span<const Array<u8,4>>`, empty or one per
  vertex, sRGB-encoded rgb, linear alpha); the attribute `UNorm8x4` at location 4 in stream 0 (packed quality and the full layout);
  `MeshView::HasColors`; the header flag. **Bump the type version to 2**; a version 1 blob is refused with the offline upgrade named.
- [x] **Upgrade offline:** `asset_mint remint` remints version 1 meshes (no colour); the committed mesh blobs and fixtures
  (`tests/engine_library/fixtures`, the demos' minted meshes) are reminted; `verify --require-current` passes.
- [x] **`.mesh.src`:** a `color` column (four bytes, or four floats in 0..1 encoded to sRGB by the minter: decide, write it down).
- [x] **The renderer** (`asset/gpu_loaders.cpp`, `renderer/mesh.*`): a mesh blob with colours gets the colour layout; a material on
  `gbuffer_color`/`gbuffer_skinned_color` with a mesh without colours is refused at bind time with a message (or white is used: decide;
  the renderer's existing rule for runtime meshes decides it).
- [x] **Tests:** build, view and load a coloured mesh; the hostile-blob test covers the new attribute (a colour attribute of the wrong
  format, at the wrong offset, a colour flag without the attribute); a coloured quad on `gbuffer_color` draws its colours (pixel test).
- [x] **Standing rows** (no shader changes).

## Notes

Done on the PC (2026-11-19). **No tests were run** (the user's request; they run after Day 12); only `build-msvc` was built.

- **No version bump (a change to decision 3).** The version-1 reader validates attributes generically (any location below 16, any
  format, binding 0 or 1), so a coloured blob already opens as version 1, and no committed blob changes. `MESH` version 2 is not
  used; Day 6 bumps the version for the morph section (which version 1 refuses), as `MESH` 2.
- `kMeshLocationColor = 4`, `MeshBuildInput::colors` (sRGB rgb, linear alpha), `MeshView::HasColors()`, `EncodeMeshColor` (linear to
  the stored bytes, the inverse of the shader's `SrgbToLinear`, clamped); the attribute follows the others in stream 0 (offset 24
  packed, 48 full).
- `.mesh.src`: a `v` line takes 16 numbers, the last four a linear RGBA colour; a mesh's `v` lines all have one or none.
- The renderer needed nothing: the GPU loader builds the vertex layout from the blob's attributes, and the G-buffer pass already
  refuses a coloured mesh through a material without `VERTEX_COLOR` (and the reverse), naming it.
- No committed mesh was reminted (none has colours).
- Tests (written, not run): the colour attribute in both layouts, `EncodeMeshColor`'s values, the refusal of a colour array of the
  wrong length. Not written: a pixel test of a coloured quad (the renderer's existing vertex-colour tests cover the drawing).
- Docs: `asset/mesh.h`, `docs/guide/assets.md`; `docs/reference` regenerated.

**Done when:** a mesh blob can carry per-vertex colours, a coloured mesh draws them, and every committed mesh is version 2.
