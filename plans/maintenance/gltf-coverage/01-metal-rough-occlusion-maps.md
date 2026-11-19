# glTF coverage / Day 1: metallic-roughness and occlusion maps in the G-buffer

**Series:** [glTF coverage](../gltf-coverage.md) (decision 1)
**Depends on:** nothing in the series.

## Tasks

- [ ] **Measure first:** the geometry pass's GPU time of `renderer_demo` and `scene_render_test`'s largest scene with today's
  `gbuffer.frag`, so the cost of two more samples per pixel is known (the numbers go in the notes).
- [ ] **Shader** (`shaders/gbuffer.frag.slang`): set 1 binding 4 `metallic_roughness_tex`, binding 5 `occlusion_tex` (the next free
  registers; one sampler stays, binding 3); new parameter `occlusion_strength` (`float`, default 1) appended to `MaterialParams` (std140:
  check the offsets the header comment lists). Math, glTF's: `roughness = material.roughness * mr.g`, `metallic = material.metallic *
  mr.b`, `occlusion = material.occlusion * (1 + occlusion_strength * (occ.r - 1))`, all from the same UV as the albedo (`albedo_uv`
  applied? decide and write it down: glTF's `KHR_texture_transform` is per texture; today only the albedo has a transform). Update the
  header comment's Math and Interface.
- [ ] **Templates** (`renderer/gbuffer.cpp`): every G-buffer template lists the two slots with `SlotColorSpace::Linear` and
  `DefaultTexture::White`, and the parameter default; `kGBufferSlots` grows to 4 (check every user of its count).
- [ ] **CPU reference:** the material's G-buffer values (roughness, metallic, occlusion) computed on the CPU for a texel; a test compares
  the GPU G-buffer readback with it for a textured quad.
- [ ] **Tests:** a material with each map set and unset (the defaults leave today's pictures unchanged: every committed golden still
  matches); a reset (`tests/renderer/reset_rig.h`) with the maps bound; the material-source minter accepts the two slots and refuses a
  texture of the wrong colour space.
- [ ] **Mint** the SPIR-V (`shaders` target); the DXIL and Metal of `gbuffer.frag` wait for Days 13 and 14 (`KNOWN-ISSUES.md`).
- [ ] **Measure again** and record the difference; if it is over 10 % of the pass, stop and bring the numbers to the user (decision 1's
  alternative).
- [ ] **Standing rows** (the series' rules).

## Notes

(written during the day)

**Done when:** a material can bind a metallic-roughness and an occlusion texture, the G-buffer holds glTF's products, a material without them
draws exactly as before, and the cost is measured.
