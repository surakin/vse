# Maintenance: unlit materials (`KHR_materials_unlit`)

Not a numbered phase. The user found a `.glb` the model viewer could not open: the importer refuses `KHR_materials_unlit` ("which the
engine's materials cannot show"). Decision (the user, 2026-11-19): **real unlit** support, not an import as lit with a warning. One day.

**Why it was refused:** every G-buffer material is lit; importing unlit as lit would show the file differently (the no-silent-fallback rule
of Phase 22).

**The approach:**
- The G-buffer's material target already carries a flags byte (`a = flags / 255`), written from the material's `flags` parameter; every
  material sets it to 0 and no pass reads it.
- Bit 0 becomes **unlit**:
  - the lighting pass outputs the albedo for such a pixel, with no ambient, directional light or emissive term;
  - the point-light pass adds nothing to it.
- The importer writes `param flags uint 1` for a material with `KHR_materials_unlit`, and nothing new for the others, so the blobs of
  files that imported before do not change.

## Tasks

- [x] `renderer::kMaterialFlagUnlit` (`gbuffer.h`, documented), and the guide's G-buffer table names the bit.
- [x] `shaders/lighting.frag.slang`, `shaders/point_light.frag.slang`: read the flags (`round(material.a * 255)`), unlit as above; SPIR-V
  minted here (`slangc`). The DXIL (the PC) and the Metal library (the Mac) are re-minted on those machines; until then, unlit draws as lit
  there (`KNOWN-ISSUES.md`).
- [x] The importer: `KHR_materials_unlit` sets the flag (base colour and texture as usual; the material's other properties are ignored,
  as the extension says); `ImportGltf`'s doc and the guide's import section say so.
- [x] Tests:
  - the importer: a glTF with an unlit material imports, with the flag in the material blob; a lit material's blob is unchanged;
  - the renderer: an unlit object under a coloured light shows its base colour, and a lit one beside it does not;
  - the model viewer opens the user's kind of file (a fixture with an unlit material).
- [x] Standing rows: `--affected` in every flavor, `ctest -L docs`, `/code-review high`, `STATUS.md`, `KNOWN-ISSUES.md`, commit and push.

**Done when:** a `.glb` with `KHR_materials_unlit` opens in the model viewer and its unlit materials show their base colour unaffected by
light, on Linux.

## Notes (2026-11-19)

**What was done:**
- `renderer::kMaterialFlagUnlit` (bit 0 of the G-buffer material's `flags`, already stored as `material.a * 255` and unused until now).
- `lighting.frag` outputs the albedo for an unlit pixel; `point_light.frag` discards it. Only these two shaders' SPIR-V changed
  (`slangc` here).
- The importer sets the bit for `KHR_materials_unlit`, accepts the extension in `extensionsRequired`, and writes `param flags uint 1` for
  unlit materials only.
  - The import version (`kImporterVersion`) is not raised: a file that imported before gives the same blobs, and a file with an unlit
    material gave none (it was refused).
- **The user's check:** their `.glb` opens in the model viewer and "looks right" (the user, 2026-11-19).

**Tests:**
- `gltf material: KHR_materials_unlit imports with the unlit flag; a lit material has no flags parameter` (the extension listed as required
  too). It replaces the old refusal check.
- `An unlit material keeps its colour under other lights; a lit one does not` (GPU, `tests/scene/scene_render_test.cpp`): two red cubes
  under a blue, then a green sun, point light and ambient.
  - The unlit cube is (225, 149, 149) both times; the lit one follows the light.
  - With the old shaders' SPIR-V it fails.

**Docs:** `docs/guide/renderer.md` (the G-buffer table and the template's parameters), `docs/guide/assets.md` (the import's materials),
`gltf_material.h`'s comments, `gbuffer.h` (the constant); `docs/reference` regenerated.

**Not done:**
- Windows' DXIL and the Mac's Metal library of the two shaders are not re-minted: until they are, unlit draws as lit there.
- An unlit material that is also alpha-blended (`BLEND`) is not tested; the G-buffer blends into its targets, so its flags byte may be
  mixed with what is behind it.

**Runs:**
- `scripts/test-phase.sh --affected --flavor all`: `ran 10928 of 11455 tests (phases: all; flavors: all; display: included; crash tests:
  excluded) in 902 s`.
  - New failures, expected: the DXIL and Metal staleness checks (plain, asan, tsan). The two shaders' SPIR-V changed, and their DXIL and
    Metal library are minted on the PC and the Mac (`KNOWN-ISSUES.md`).
  - The other failures are all known: the Job-destroyed test (three flavors), `engine_c_surface`, the voice-cost snapshot and the tsan
    LuaJIT sandbox flake.
- `ctest -L docs`: 9 of 9. Builds: Clang unity, GCC non-unity, both clean.

**Review** (`/code-review high`): no findings.
- The flag read rounds `a * 255`, so an 8-bit UNORM store of 1/255 reads back as 1.
- Lit materials write no `flags`, and the template's default is 0, so they are unchanged.
- `point_light` discards before any lighting work.
- The blended-unlit case is noted above, not fixed: blended surfaces are rare for unlit glTF content, and it needs a forward path.
