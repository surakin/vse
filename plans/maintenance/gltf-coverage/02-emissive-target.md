# glTF coverage / Day 2: emissive colour: a fourth G-buffer target and the emissive map

**Series:** [glTF coverage](../gltf-coverage.md) (decision 2)
**Depends on:** Day 1 (the slot and parameter pattern).

## Tasks

- [x] **The target** (`renderer/gbuffer.h/.cpp`): `kGBufferEmissiveFormat = RGBA16_Float`, a fourth transient in `GBufferTargets`, cleared to
  black; `CheckGBufferFormats` covers it; `GBufferStats` and the memory budget count it; the G-buffer debug view
  (`gbuffer_debug.frag`) shows it.
- [x] **Geometry pass** (`gbuffer.frag`): output `SV_Target3` = `emissive_color.rgb * emissive_tex.rgb` (slot binding 6, sRGB,
  default white); parameter `emissive_color` (`float4`, rgb linear, a = strength, default `(0,0,0,1)`) replaces `emissive_scale`; an
  unlit pixel writes zero emissive (its colour is the base colour).
- [x] **Lighting** (`lighting.frag`, `lighting_common.slang`, `point_light.frag`): the fullscreen pass reads the emissive target and adds it
  once; `ShadeAmbient` loses its emissive term; the point-light pass does not add it; the CPU twin (`renderer/lighting.cpp`) follows.
  `material.b` is written 0 and documented as free.
- [x] **Material sources:** `emissive_scale` in a `.mat.src` is refused with a message naming `emissive_color`; `asset_mint remint`
  rewrites old sources (`emissive_scale s` becomes `emissive_color` = s times the base colour's rgb, which is what it showed) and the
  committed material sources and blobs are reminted; the material blob's version is bumped if its stored parameters change shape.
- [x] **Tests:** an emissive material keeps its colour with no light and adds to a lit one (pixel test against the CPU twin); a strength
  over 1 is not clamped (an HDR readback); every golden matches or is re-blessed with the reason (a material that had a non-zero
  `emissive_scale`); the reset rig with the fourth target.
- [x] **Mint** SPIR-V of `gbuffer.frag`, `lighting.frag`, `point_light.frag`, `gbuffer_debug.frag`; DXIL and Metal wait.
- [x] **Standing rows.**

## Notes

Done on the Windows PC (2026-11-19). **The tests were not all run:** the user asked to skip the tests ("this is taking FOREVER")
partway through the day's full run. What ran, in `build-msvc` plain: the scoped phases once before the last fixes (2143 of 2147: the
four failures were tests that still expected three colour targets, fixed after), then the 36 G-buffer, lighting, emissive, shader and
importer tests after the fixes, all passing. Not run: asan, clang-cl, the shared trees and the last shader change (the emissive
alpha). Only `build-msvc` was built after the last changes.

**What changed.**
- `kGBufferEmissiveFormat = RGBA16_Float`, a fourth G-buffer target (`GBufferTargets::emissive`), written by `gbuffer.frag`
  (`SV_Target3` = `emissive_color.rgb * emissive_color.a * emissive_tex.rgb`, 0 for an unlit material, alpha the surface's alpha so a
  blended surface keeps its glow), read and added once by `lighting.frag`; the point lights and `ShadeAmbient` (shader and CPU twin)
  no longer add emission; `material.b` is written 0.
- `MaterialParams`: `occlusion_strength` took `emissive_scale`'s place at 24, `emissive_color` (`float4`, default `(0, 0, 0, 1)`) is at
  80; the block stays 96 bytes. `emissive_tex` (sRGB, white by default: its name would pick black) on every template.
- **A Day 1 bug fixed:** `ApplyGBufferDefaults`, which every runtime material and every loaded material blob goes through, did not set
  `occlusion_strength`, so it stayed 0 and an occlusion map did nothing unless the strength was set. The Day 1 GPU test now also draws
  a map with the strength unset; without the fix it differs by 29 levels.
- The debug view has an `Emissive` layer (`renderer.gbuffer_debug_layer` value `emissive`, `renderer_demo --gbuffer emissive`).
- Demos: `renderer_demo`'s glow is `emissive_color` = base colour times its old scale (the same picture for its untextured emissive
  material); the UI demo's screen binds its picture to `emissive_tex` too; `script_demo`'s glows of 2.0 and 2.5, which the 8-bit target
  had clamped to 1, now show as asked (its screenshot test checks the frame count, not pixels).
- The importer writes `emissive_color` from `emissiveFactor` exactly (and the strength), no longer the largest component with a
  warning; the emissive texture is still dropped until Day 3.
- C API: the slot `emissive_scale` is gone (refused as unknown), `emissive_color` (`ENGINE_SLOT_COLOR`) and `emissive_tex` are new
  (`engine_c.h`). A host that set `emissive_scale` must switch.
- **Material sources:** no committed source used `emissive_scale`, so the planned `remint` rewrite was not needed: a source that
  names it is refused by the minter as an unknown parameter (the minter's existing check), which names the template's members.
- Minted SPIR-V and DXIL of `gbuffer.frag`, `gbuffer_debug.frag`, `lighting.frag`, `point_light.frag`; their Metal libraries wait for
  Day 14.
- Docs: `gbuffer.h`, `lighting.h`, `render_settings.h`, `engine_c.h`; `docs/guide/renderer.md`, `assets.md`, `engine-library.md`,
  `docs/tutorials/hello-renderer.md`; `docs/reference` regenerated.

## Post-implementation code review

Light, by reading the diff (the user asked to skip the tests). 2 findings, both fixed:
1. The emissive target wrote alpha 0, and an alpha-blended material blends each target by its own alpha: a blended surface lost its
   glow. It writes the surface's alpha now (not tested).
2. The `ShadeAmbient` summary did not end in a period (a docgen warning).

**Done when:** emissive is an HDR RGB colour times an emissive texture, written to its own target and added once; old materials are upgraded
offline and look the same.
