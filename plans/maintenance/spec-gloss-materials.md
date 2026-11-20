# Specular-glossiness materials (maintenance, 2026-11-20, Linux)

Not a numbered phase. The user's `doom_eternal_classic_marine.glb` was refused by the importer with "the extension
KHR_materials_pbrSpecularGlossiness is required and not supported": all 15 of its materials use the older specular-glossiness
model (37 PNG textures). The user chose the exact conversion, per texel, over a dielectric approximation.

## Done

- [x] **The conversion** (`asset_import/spec_gloss.{h,cpp}`, new): the Khronos sample tools' formula. The metallic value is solved from the
  perceived brightness of the diffuse and specular colours, the base colour blends the colour each implies by the metallic value
  squared, and the roughness is one minus the glossiness. `SpecGlossToMetalRough` converts one sample. `ConvertSpecGlossImages` converts
  two decoded images and the factors per texel into a base-colour image (sRGB, the diffuse alpha as opacity) and a metallic-roughness
  image (the glTF packing). The output is as large as the larger image; a smaller one is sampled bilinearly in linear space. sRGB goes
  through tables (256 entries in, 4096 out). `SpecularIsDielectric` tells when no texel can be metal.
- [x] **The importer** (`gltf_material.cpp`): the extension is in the supported `extensionsRequired` list, and it wins over
  `pbrMetallicRoughness` in the same material.
  - When the factors alone decide (no texture; or no specular-glossiness texture and a specular colour below 4%), the conversion goes
    into the factors, and a diffuse texture is used as it is.
  - Otherwise the material gets its own converted pair, `<material>_base_color` and `<material>_metal_rough` (manifest keys
    `0x60000000 + material * 2 + k`). The pair is converted once and its factors are 1. An unlit material gets the base colour only, and
    a size mismatch warns.
- [x] **`kGltfImporterOutputVersion` 4.** A file that listed the extension only in `extensionsUsed` used to import with its specular-glossiness
  ignored; its blobs change now. The host fingerprint is `0x6d605b9cd8e631ea`.
- [x] **Found on the way: the import manifest refused ten-digit keys** (`gltf_import.cpp`, `ImportManifest::Parse`). It read at most nine
  digits, but the keys of the metallic-roughness, occlusion and emissive maps (glTF coverage Day 3) start at `0x40000000`, which is ten
  digits. So a re-import with the manifest of any file with those maps failed ("is not `kind index id name`"). It now reads up to ten
  digits, up to the largest `u32`. Test: in "gltf: the import manifest keeps ids through a reorder and a rename, and round trips as
  text" (failed without the fix).
- [x] **Tests** (`tests/asset/gltf_material_test.cpp`):
  - "spec gloss: a sample converts as the Khronos tools convert it" (dielectric, metal, in between);
  - "spec gloss: textures convert per texel at the larger image's size" (resampling, factors, opacity, the pixel limit);
  - "gltf material: KHR_materials_pbrSpecularGlossiness imports converted to metallic-roughness": the converted pair, the factor path,
    the extension over `pbrMetallicRoughness`, and an unlit material.
- [x] **The user's file:** `asset_mint import-gltf` imports it with 67 resources and 0 warnings in 6.6 s. Imported twice with one manifest,
  the blobs and the manifest are byte-identical. The model viewer draws it (15 meshes, 15 materials) with plausible colours (checked
  only by Claude, on a screenshot).
- [x] **Docs:** `docs/guide/assets.md` (the supported list, a "Specular-glossiness materials" paragraph); `docs/reference/` regenerated.

## Runs

- Plain: `ran 1051 of 11539 tests (phases: 3 22 23 27; flavors: plain; display: excluded; crash tests: excluded; soak: excluded) in 27 s`.
  Only the known `engine_c_surface` failed: it fails in a Release tree, and `build` is Release (`KNOWN-ISSUES.md`, Phase 25 closing,
  item 2).
- asan: `ran 932 of 11539 tests (phases: 3 22 23 27; flavors: asan; ...) in 102 s`, no failures.
- tsan: `ran 746 of 11539 tests (phases: 3 22 23 27; flavors: tsan; ...) in 79 s`, no failures. This run came before the review fixes.
- Both sanitizer runs warned that `engine_c_script_sample_asan` (70.7 s) and `_tsan` (77.2 s) ran over 60 s without the soak label.
  This is recorded in `KNOWN-ISSUES.md`.
- `ctest -L docs`: 9 of 9.
- `build-gcc` (GCC, unity off) built `asset_import`, `asset_tests` and `asset_mint` clean.

## Post-implementation code review

`/code-review high` on the diff: 4 findings, all fixed.
1. **The pair was converted twice** (once for each image). It is now converted once, and the metallic-roughness image is kept for the
   next entry.
2. **The factor path repeated the formula** (a hand-written `(1 - max specular) / 0.96` scale). It now uses `SpecGlossToMetalRough` of
   the factors, which is the same value when the metallic value is 0.
3. **The dielectric test was a second copy of the brightness formula.** It is now `SpecularIsDielectric` in `spec_gloss.h`, and the
   test checks it.
4. **The unlit case was untested.** Added to the import test: base colour only, one texture bound.

The re-import check after the review found the manifest bug above. It was fixed with its own regression test.

## Not done

- Not built or run on the PC or the Mac (`KNOWN-ISSUES.md`).
- No human has looked at the converted model. The conversion is exact where both images are the same size; with a diffuse texture,
  the factor path clamps the factor at 1 rather than each texel, so with a black specular colour a texel below white comes
  out up to 4% darker than the per-texel conversion would make it.
