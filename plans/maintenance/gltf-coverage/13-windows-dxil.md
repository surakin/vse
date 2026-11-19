# glTF coverage / Day 13: the PC: DXIL and Direct3D 12

**Series:** [glTF coverage](../gltf-coverage.md) (decision 8)
**Depends on:** every shader day (1, 2, 7, 11).

## Tasks

- [ ] **Mint the DXIL** of every shader the series changed (`gbuffer.frag`, `gbuffer.vert`, `lighting.frag`, `point_light.frag`,
  `gbuffer_debug.frag`, and any other the build reports stale), with the pinned slangc and dxcompiler; no other `.dxil.inc` changes.
- [ ] **Direct3D 12 bindings:** the new texture slots, the fourth G-buffer target, the morph buffer and palette, the 64-byte `LightData`
  (root signature and descriptor tables in `engine/rhi/dx12/`; `tests/shader_mint/d3d12_conventions_test.cpp`).
- [ ] **Run** the series' tests on Vulkan and Direct3D 12 (`FOR_EACH_GPU_BACKEND`), MSVC and clang-cl, plain and asan, unity and not; the
  DXIL staleness test passes; the goldens match on Direct3D 12.
- [ ] **Look:** the model viewer on Direct3D 12 with a file of each feature.
- [ ] **Standing rows** (on the PC: ctest directly, the reset-loop test left out).

## Notes

(written during the day)

**Done when:** every feature of the series draws on Direct3D 12 as on Vulkan, and no DXIL is stale.
