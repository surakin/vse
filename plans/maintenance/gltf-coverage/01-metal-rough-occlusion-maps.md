# glTF coverage / Day 1: metallic-roughness and occlusion maps in the G-buffer

**Series:** [glTF coverage](../gltf-coverage.md) (decision 1)
**Depends on:** nothing in the series.

Done on the Windows PC (2026-11-19), not on Linux as decision 8 said: the user started the series here. The PC mints SPIR-V and DXIL
together, so `gbuffer.frag`'s DXIL is current already and only its Metal library waits (Day 14).

## Tasks

- [x] **Measure first:** the geometry pass's GPU time of `renderer_demo` and `scene_render_test`'s largest scene with today's
  `gbuffer.frag`, so the cost of two more samples per pixel is known (the numbers go in the notes).
- [x] **Shader** (`shaders/gbuffer.frag.slang`): set 1 binding 4 `metallic_roughness_tex`, binding 5 `occlusion_tex` (the next free
  registers; one sampler stays, binding 3); new parameter `occlusion_strength` (`float`, default 1) appended to `MaterialParams` (std140:
  check the offsets the header comment lists). Math, glTF's: `roughness = material.roughness * mr.g`, `metallic = material.metallic *
  mr.b`, `occlusion = material.occlusion * (1 + occlusion_strength * (occ.r - 1))`, all from the same UV as the albedo (`albedo_uv`
  applied? decide and write it down: glTF's `KHR_texture_transform` is per texture; today only the albedo has a transform). Update the
  header comment's Math and Interface.
- [x] **Templates** (`renderer/gbuffer.cpp`): every G-buffer template lists the two slots with `SlotColorSpace::Linear` and
  `DefaultTexture::White`, and the parameter default; `kGBufferSlots` grows to 4 (check every user of its count).
- [x] **CPU reference:** the material's G-buffer values (roughness, metallic, occlusion) computed on the CPU for a texel; a test compares
  the GPU G-buffer readback with it for a textured quad. (Done as an equivalence instead: see the notes.)
- [x] **Tests:** a material with each map set and unset (the defaults leave today's pictures unchanged: every committed golden still
  matches); a reset (`tests/renderer/reset_rig.h`) with the maps bound; the material-source minter accepts the two slots and refuses a
  texture of the wrong colour space.
- [x] **Mint** the SPIR-V (`shaders` target); the DXIL and Metal of `gbuffer.frag` wait for Days 13 and 14 (`KNOWN-ISSUES.md`). (DXIL
  minted too, on the PC.)
- [x] **Measure again** and record the difference; if it is over 10 % of the pass, stop and bring the numbers to the user (decision 1's
  alternative).
- [x] **Standing rows** (the series' rules).

## Notes

**Measurement.** The RHI has no timestamp queries (`DeviceCaps::timestamp_period_ns` only), so no pass can be timed alone. A new
`renderer_bench --section gpu` times the whole frame from submission to the end of `WaitIdle`, no point lights, the demo scene at
1280x720 (Release, `build-msvc-release`, the PC's AMD Radeon 860M; 200 frames, two runs each):

| | D3D12, 1k objects | D3D12, 10k | Vulkan, 1k | Vulkan, 10k |
|---|---|---|---|---|
| before | 0.946 / 0.947 ms | 4.025 / 4.118 ms | 0.841 / 0.841 ms | 3.527 / 3.485 ms |
| after | 0.947 / 0.944 ms | 3.677 / 4.096 ms | 0.837 / 0.842 ms | 3.460 / 3.437 ms |

No change beyond the noise. These frames are not bound by pixel shading at 720p (10k objects is CPU recording), so this says the two
samples cost nothing measurable here, not that they are free in a scene whose geometry pass is fill-bound; decision 1 stands.

**The coordinates.** Both maps read the mesh's coordinates untransformed, as the normal map does; `albedo_uv` transforms the albedo
only (it exists to fit a host's picture into a quad). A per-texture glTF transform is still refused by the importer.

**The parameter block** grows from 80 to 96 bytes (`occlusion_strength` at 80). Material blobs minted before keep their 80-byte
block and still load: the GPU loader applies the template's defaults and then sets the blob's parameters by name, so
`occlusion_strength` gets 1. No material version change.

**The equivalence test instead of a G-buffer readback.** Reading a G-buffer target back would need new plumbing in the demo frame.
`A metallic-roughness or occlusion map draws as its products set as scalars do` (`tests/scene/scene_render_test.cpp`, Vulkan and
Direct3D 12, directional, point and ambient light) draws a cube with a 1x1 map and with the map's products set as scalars, which is the
shader's own scalar path as the reference: they agree within 1 level; the map changes the picture (more than 8 levels against the same
scalars without it); white maps draw exactly as no maps. Without the shader change the test fails (`SetTexture` of
`metallic_roughness_tex` refused). Also: `The G-buffer templates have linear metallic-roughness and occlusion slots that default to
white` (`gbuffer_test.cpp`), `a material binds linear metallic-roughness and occlusion maps without an option` and two refusals of an
sRGB texture in a map slot (`tests/asset/material_test.cpp`; the round-trip test's block is 96 bytes now), `The metallic-roughness and
occlusion maps follow their texture through device resets` (`reset_soak_test.cpp`), and the C host's slot-override checks
(`host_scene.c`).

**The C API** accepts `occlusion_strength`, `metallic_roughness_tex` and `occlusion_tex` as slot overrides (`engine_c.h`).

**Minted:** `gbuffer.frag`'s SPIR-V, DXIL, permutations and reflection; `gbuffer.vert` and `gbuffer_debug.frag` were recompiled and came
out the same. The Metal library of `gbuffer.frag` is stale until Day 14 (`KNOWN-ISSUES.md`).

**Docs:** `gbuffer.h` (the template parameters and slots), `engine_c.h` (the slot overrides), `docs/guide/renderer.md` (the slots and the
`gpu` bench section), `docs/guide/engine-library.md` (the slot list); `docs/reference` regenerated.

**Runs** (ctest directly, the phase 8 reset loop left out, crash tests out; phases 3, 4, 5, 8, 13, 17, 22, 23, 24 and 27 plus every asan
test):
- `build-msvc` plain 1737 of 1737; asan 3459 of 3460 (the Metal staleness test, expected until Day 14);
- `build-clang-cl` plain 1734 of 1737: `engine_c_acceptance_scene`, `engine_c_acceptance_scene_vulkan` and `engine_c_runtime` (a
  timeout) failed in the loaded run and passed alone (`KNOWN-ISSUES.md`);
- `engine_library` 89 of 89 in `build-msvc-shared` and `build-clang-cl-shared`, and again in `build-msvc` and `build-clang-cl` after the
  review's fix; `ctest -L docs` 8 of 8;
- clean builds: MSVC and clang-cl, unity and not, static and shared.

## Post-implementation code review

`/code-review high` over the day's diff: 4 findings.
1. **Fixed:** no C host test used the new slot names. `host_scene.c` now checks that each map slot is known (a colour is "another
   kind", not an unknown name) and that `occlusion_strength` takes a float.
2. **Fixed:** the C API's unknown-slot message lists the valid names and left out the three new ones.
3. **Recorded, not fixed today:** the Metal library of `gbuffer.frag` is stale until the Mac mints it (Day 14, the plan's decision 8).
4. **Recorded:** the `gpu` bench section cannot isolate the pass (no timestamp queries); the notes say what the numbers do and do not
   show.

**Done when:** a material can bind a metallic-roughness and an occlusion texture, the G-buffer holds glTF's products, a material without them
draws exactly as before, and the cost is measured.
