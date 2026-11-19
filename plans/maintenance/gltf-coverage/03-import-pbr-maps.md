# glTF coverage / Day 3: the importer: the three maps and the emissive colour

**Series:** [glTF coverage](../gltf-coverage.md)
**Depends on:** Days 1 and 2.

## Tasks

- [x] **Materials** (`asset_import/gltf_material.cpp`): `pbrMetallicRoughness.metallicRoughnessTexture`, `occlusionTexture` (with
  `strength`) and `emissiveTexture` become `metallic_roughness_tex`, `occlusion_tex` and `emissive_tex`; `emissiveFactor` times
  `KHR_materials_emissive_strength` becomes `emissive_color` (rgb, strength in a). The "dropped" warnings go; a map whose `texCoord` is not
  0 stays dropped with a warning naming it.
- [x] **Textures:** one texture resource per use, as today: `<name>_mr` and `<name>_occlusion` linear, `<name>_emissive` sRGB (BC7). An ORM
  image (occlusion in R, roughness in G, metallic in B, one image for both uses) mints two textures from one decode (decode once, mint
  twice; check the import cache); write down whether a shared linear texture could serve both slots (one resource) and do it if the
  slots' colour spaces agree.
- [x] **`KHR_materials_unlit`:** the maps are not set (the extension ignores them).
- [x] **Tests** (`tests/asset/gltf_material_test.cpp`, files built with `gltf_writer.h`): a glTF built in the test with each map, an ORM image used twice, a map with `texCoord: 1`
  (warning), the emissive strength; the imported material's parameters and slots; deterministic bytes (`SeededIdSource`, twice).
- [x] **The model viewer:** open a file with the three maps (a test file built by the tests is enough) and look; a screenshot ctest if the
  existing ones do not cover it.
- [x] **Standing rows.**

## Notes

Done on the PC (2026-11-19). **No tests were run** (the user's request; they run after Day 12); only `build-msvc` was built. The model
viewer was not looked at.

- `gltf_material.cpp`: three more uses (`_mr` and `_occlusion` linear, `_emissive` sRGB), each a BC7 texture through the same mint
  path; the "dropped" warnings for the three maps are gone; `occlusionTexture.strength` becomes `occlusion_strength` when it is not 1
  (so the blobs of materials without one do not change); an unlit material binds none of the maps.
- **ORM:** when a material's occlusion and metallic-roughness maps are the same glTF texture, it is made once (`_mr`, linear) and bound
  to both slots: the slots' colour spaces agree, so one resource serves both.
- **Manifest keys:** the base colour and normal map keep `texture * 2 + use`, so a re-import with an older manifest keeps their ids;
  the new uses have keys of their own (`0x40000000 + texture * 3 + ...`).
- A map on `TEXCOORD_1` or above stays dropped with a warning (unchanged rule).
- Tests (written, not run): the textured-glb test expects the `_mr` texture and three bindings; a new case checks the ORM sharing, the
  strength and the sRGB emissive texture.
- Docs: `docs/guide/assets.md` ("Importing glTF": textures and materials).

**Done when:** a glTF's metallic-roughness, occlusion and emissive textures and its emissive colour arrive in the material without a warning.
