# glTF coverage / Day 3: the importer: the three maps and the emissive colour

**Series:** [glTF coverage](../gltf-coverage.md)
**Depends on:** Days 1 and 2.

## Tasks

- [ ] **Materials** (`asset_import/gltf_material.cpp`): `pbrMetallicRoughness.metallicRoughnessTexture`, `occlusionTexture` (with
  `strength`) and `emissiveTexture` become `metallic_roughness_tex`, `occlusion_tex` and `emissive_tex`; `emissiveFactor` times
  `KHR_materials_emissive_strength` becomes `emissive_color` (rgb, strength in a). The "dropped" warnings go; a map whose `texCoord` is not
  0 stays dropped with a warning naming it.
- [ ] **Textures:** one texture resource per use, as today: `<name>_mr` and `<name>_occlusion` linear, `<name>_emissive` sRGB (BC7). An ORM
  image (occlusion in R, roughness in G, metallic in B, one image for both uses) mints two textures from one decode (decode once, mint
  twice; check the import cache); write down whether a shared linear texture could serve both slots (one resource) and do it if the
  slots' colour spaces agree.
- [ ] **`KHR_materials_unlit`:** the maps are not set (the extension ignores them).
- [ ] **Tests** (`tests/asset/gltf_material_test.cpp`, files built with `gltf_writer.h`): a glTF built in the test with each map, an ORM image used twice, a map with `texCoord: 1`
  (warning), the emissive strength; the imported material's parameters and slots; deterministic bytes (`SeededIdSource`, twice).
- [ ] **The model viewer:** open a file with the three maps (a test file built by the tests is enough) and look; a screenshot ctest if the
  existing ones do not cover it.
- [ ] **Standing rows.**

## Notes

(written during the day)

**Done when:** a glTF's metallic-roughness, occlusion and emissive textures and its emissive colour arrive in the material without a warning.
