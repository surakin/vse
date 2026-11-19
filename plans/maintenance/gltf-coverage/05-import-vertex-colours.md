# glTF coverage / Day 5: the importer: `COLOR_0`

**Series:** [glTF coverage](../gltf-coverage.md)
**Depends on:** Day 4.

## Tasks

- [ ] **Read `COLOR_0`** (`asset_import/gltf_mesh.cpp`): `VEC3` or `VEC4`, `FLOAT`, normalized `UNSIGNED_BYTE` or `UNSIGNED_SHORT`
  (`ReadAccessorFloats`); `VEC3` gets alpha 1. glTF's colours are linear: rgb encoded to sRGB 8-bit (round to nearest; the CPU encode is
  the inverse of the shader's `SrgbToLinear`), alpha linear 8-bit. The largest round-trip error goes in `ImportReport::notes`.
  `COLOR_1` and up stay dropped with a warning.
- [ ] **Materials:** a glTF material used by a primitive with `COLOR_0` gets a twin `<name>_color` on `gbuffer_color` (or
  `gbuffer_skinned_color`); a material used both ways makes both; the slot of the primitive names the right one. The manifest records the
  twin (re-import keeps its id).
- [ ] **Tests** (`tests/asset/gltf_mesh_test.cpp`, `gltf_writer.h`): a glTF built in the test with `VEC3` float, `VEC4` unorm8 and unorm16 colours; a material used with and without colours; the
  alpha of `VEC4` reaches the alpha test (`MASK`); the mesh blob's bytes; deterministic re-import.
- [ ] **Look:** the model viewer on a coloured file.
- [ ] **Standing rows.**

## Notes

(written during the day)

**Done when:** a glTF's `COLOR_0` multiplies the base colour (and alpha) as glTF says, without a warning.
