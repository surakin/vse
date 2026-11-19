# glTF coverage / Day 2: emissive colour: a fourth G-buffer target and the emissive map

**Series:** [glTF coverage](../gltf-coverage.md) (decision 2)
**Depends on:** Day 1 (the slot and parameter pattern).

## Tasks

- [ ] **The target** (`renderer/gbuffer.h/.cpp`): `kGBufferEmissiveFormat = RGBA16_Float`, a fourth transient in `GBufferTargets`, cleared to
  black; `CheckGBufferFormats` covers it; `GBufferStats` and the memory budget count it; the G-buffer debug view
  (`gbuffer_debug.frag`) shows it.
- [ ] **Geometry pass** (`gbuffer.frag`): output `SV_Target3` = `emissive_color.rgb * emissive_tex.rgb` (slot binding 6, sRGB,
  default white); parameter `emissive_color` (`float4`, rgb linear, a = strength, default `(0,0,0,1)`) replaces `emissive_scale`; an
  unlit pixel writes zero emissive (its colour is the base colour).
- [ ] **Lighting** (`lighting.frag`, `lighting_common.slang`, `point_light.frag`): the fullscreen pass reads the emissive target and adds it
  once; `ShadeAmbient` loses its emissive term; the point-light pass does not add it; the CPU twin (`renderer/lighting.cpp`) follows.
  `material.b` is written 0 and documented as free.
- [ ] **Material sources:** `emissive_scale` in a `.mat.src` is refused with a message naming `emissive_color`; `asset_mint remint`
  rewrites old sources (`emissive_scale s` becomes `emissive_color` = s times the base colour's rgb, which is what it showed) and the
  committed material sources and blobs are reminted; the material blob's version is bumped if its stored parameters change shape.
- [ ] **Tests:** an emissive material keeps its colour with no light and adds to a lit one (pixel test against the CPU twin); a strength
  over 1 is not clamped (an HDR readback); every golden matches or is re-blessed with the reason (a material that had a non-zero
  `emissive_scale`); the reset rig with the fourth target.
- [ ] **Mint** SPIR-V of `gbuffer.frag`, `lighting.frag`, `point_light.frag`, `gbuffer_debug.frag`; DXIL and Metal wait.
- [ ] **Standing rows.**

## Notes

(written during the day)

**Done when:** emissive is an HDR RGB colour times an emissive texture, written to its own target and added once; old materials are upgraded
offline and look the same.
