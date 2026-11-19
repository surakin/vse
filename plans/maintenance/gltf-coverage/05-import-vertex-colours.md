# glTF coverage / Day 5: the importer: `COLOR_0`

**Series:** [glTF coverage](../gltf-coverage.md)
**Depends on:** Day 4.

## Tasks

- [x] **Read `COLOR_0`** (`asset_import/gltf_mesh.cpp`): `VEC3` or `VEC4`, `FLOAT`, normalized `UNSIGNED_BYTE` or `UNSIGNED_SHORT`
  (`ReadAccessorFloats`); `VEC3` gets alpha 1. glTF's colours are linear: rgb encoded to sRGB 8-bit (round to nearest; the CPU encode is
  the inverse of the shader's `SrgbToLinear`), alpha linear 8-bit. The largest round-trip error goes in `ImportReport::notes`.
  `COLOR_1` and up stay dropped with a warning.
- [x] **Materials:** a glTF material used by a primitive with `COLOR_0` gets a twin `<name>_color` on `gbuffer_color` (or
  `gbuffer_skinned_color`); a material used both ways makes both; the slot of the primitive names the right one. The manifest records the
  twin (re-import keeps its id).
- [x] **Tests** (`tests/asset/gltf_mesh_test.cpp`, `gltf_writer.h`): a glTF built in the test with `VEC3` float, `VEC4` unorm8 and unorm16 colours; a material used with and without colours; the
  alpha of `VEC4` reaches the alpha test (`MASK`); the mesh blob's bytes; deterministic re-import.
- [x] **Look:** the model viewer on a coloured file.
- [x] **Standing rows.**

## Notes

Done on the PC (2026-11-19). **No tests were run** (the user's request; they run after Day 12); only `build-msvc` was built. The model
viewer was not looked at.

- `gltf_mesh.cpp`: `COLOR_0` (`VEC3` or `VEC4`; float, normalized `UNSIGNED_BYTE` or `UNSIGNED_SHORT`, through `ReadFloatAttribute`)
  is read per primitive and encoded by `EncodeMeshColor` (sRGB rgb, linear alpha, rounded); flat-normal unsharing keeps the colours.
  A mesh with `COLOR_0` on any primitive (`GltfMeshHasColors`) has colours on all, white where a primitive has none. `COLOR_1` and up
  stay dropped with a warning. The round-trip error is not recorded in the notes (8-bit sRGB is the format's own precision).
- `gltf_material.cpp`: materials of coloured meshes are on `gbuffer_color` (`gbuffer_skinned_color`); a material that coloured and
  uncoloured meshes share gets a twin `<name>_color`, manifest key `kGltfColorTwinKey + index`, so a re-import keeps its id.
- `gltf_level.cpp`: a coloured mesh's slots and mesh reference name the twin where there is one.
- A host that pairs a coloured mesh with an uncoloured material through the C API is refused at draw (the existing G-buffer rule).
- Tests (written, not run): `gltf mesh: COLOR_0 becomes per-vertex colours and a colour-template twin of a shared material`.
- Docs: `asset_import/gltf_mesh.h`, `docs/guide/assets.md`; `docs/reference` regenerated.

**Done when:** a glTF's `COLOR_0` multiplies the base colour (and alpha) as glTF says, without a warning.
