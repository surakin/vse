# glTF coverage / Day 14: the Mac: Metal

**Series:** [glTF coverage](../gltf-coverage.md) (decision 8)
**Depends on:** every shader day (1, 2, 7, 11).

## Tasks

- [x] **Mint the Metal libraries** of every shader the series changed (`tools/metal_mint`, the committed libraries' macOS build); a CPU block
  must lay out the same in Metal as in std140 (`shader_mint` refuses a difference: the grown `MaterialParams`, `LightData` and the morph
  palette).
- [x] **Metal bindings:** the new slots, the fourth target, the morph buffers (`engine/rhi/metal/`; argument indices within Metal's limits).
- [x] **Run** the series' tests on Metal with validation (plain only, the Mac's rule), unity tree; arm64 compile-only.
- [x] **Look:** the model viewer on Metal.
- [x] **Standing rows** (on the Mac).

## Notes

Done on the Mac, 2026-11-19: `build-macos` (unity, Debug, x86-64), plain only (the Mac's rule), the console unlocked. The day started at
`169a40a4` (`--start-day`), after the PC's Days 1-12, whose tests had not been run since Day 2.

**Minted.** `cmake --build build-macos --target shaders` (slangc 2026.18.2, `tools/metal_mint` on macOS 15.8.1, Build 24H32, the
committed libraries' build) re-minted the Metal libraries of `gbuffer.frag` (every permutation), `gbuffer_debug.frag`, `lighting.frag`,
`point_light.frag` and `morph.comp`; no other `.metallib.inc` changed. `shader_mint` accepted every CPU block's Metal layout (it refuses
a difference). `gbuffer.vert` and the 48-byte `LightData` needed nothing.

**Found and fixed.**
1. **The build was a cycle on the Mac.** Day 7 put `morph.comp`'s includes in `material.cpp`, in the core `renderer` library, which
   `shader_mint` links; the shader includes are `shader_mint`'s outputs. Ninja stopped with `dependency cycle: librenderer.a ->
   ... -> morph.comp.metallib.inc -> shader_mint -> librenderer.a`. (`engine/renderer/CMakeLists.txt` keeps the passes in
   `renderer_passes` for exactly this reason.) `shader_reflection.cpp` and `shader_permutation.cpp`, which need only the RHI's types
   and the foundation, are now a library of their own, `renderer_reflection` (per flavor), below `renderer`, which links it;
   `shader_mint_lib` links it instead of `renderer` (`modules.txt`, `ARCHITECTURE.md` and docgen's link lists name it). The same
   cycle exists wherever the minters run (Linux with slangc, the PC with dxc), so this fixes them too; not yet built there.
2. **The morph pass's push constants were 160 bytes, past the 128 bound** (8 header words and `uint4 targets[8]`). Metal's validation
   stopped the new morph test: `argument push_1[0] from buffer(2) with offset(0) and length(128) has space for 128 bytes, but argument has
   a length(160)`. The CPU side wrote `push[8 + k * 4 + 2]` into an `Array<u32, 32>`, past its end from the seventh active target on.
   A target is now three words (`uint4 targets[6]`, `TargetWord(i)`), so eight fill the 32 words exactly; a `static_assert` in
   `gbuffer.cpp` holds it. `morph.comp` re-minted: SPIR-V and Metal here, **its DXIL is stale until the PC re-mints it** (Day 13).
3. **The scene's GPU tests ran on Metal without validation.** `tests/scene/CMakeLists.txt` never set `MTL_DEBUG_LAYER` and the rest,
   as `tests/renderer` and `tests/rhi` do, so the series' pixel tests there (and the earlier unlit test) ran unvalidated on Metal. Set
   now; phase 17 then passed, 152 of 153, the one failure being finding 2.
4. **`morph.comp` was in neither staleness test.** Added to `shader_dxil_test.cpp` and `shader_metallib_test.cpp`: the Metal test
   passes; the DXIL test now fails on `morph.comp` until the PC re-mints (finding 2).
5. **Two importer tests expected Days 5 and 10 to drop what they now import** (the PC did not run them): `gltf mesh: other attributes
   are dropped with a counted warning` (COLOR_0 is imported: one warning, TEXCOORD_1) and `gltf clip: channels that cannot play are
   dropped with a warning, ...` (a `weights` channel becomes a weights-only clip: one warning). Expectations updated.
6. **A Clang error in Day 12's test** (`gltf_mesh_test.cpp`: two literals joined inside an array, `-Wstring-concatenation`). The PC
   fixed it the same evening (`cffc0d1c`, a parenthesized literal); the rebase kept its form.

**New GPU tests** (`tests/scene/scene_render_test.cpp`; nothing drew a morphed mesh or a spot light on a device before):
- `A morph target moves the mesh on the GPU by its weight`: a cube with eight targets of 0.075 along x; weight 0 and no weights draw
  exactly the base; all at 1 match the cube moved by 0.6, all at 0.5 the cube moved by 0.3, within 1 in 200 pixels. It failed with
  the old push layout (finding 2); it passes on Metal with validation.
- `A spot light lights inside its cone as a point light does, and nothing outside it`: a narrow spot 1 m in front of a cube's face
  lights the middle as a point light there does (within 1) and leaves the face's edge black, which the point light reaches.

**Also this day, at the user's request: the script demo is not run on the Mac.** The day first added a per-machine switch to
`scripts/test-phase.sh` (`--machine-script-demo off`, a label `script_demo_suite`, a test in a scratch repository). The same evening
the PC's session, also at the user's request, made the demo's ctests and `tests/script_demo` Linux-only (`cffc0d1c`), so the Mac has
none of them; on the rebase the switch was dropped for that. The runs quoted below were made with the switch, so their summary lines
end in `script demo: excluded`; the twelve tests it left out are the ones the Mac no longer defines.

**Runs.**
- Before the fixes, every phase plain: `ran 4101 of 11307 tests (phases: all; flavors: plain; display: included; crash tests: excluded;
  soak: excluded; script demo: excluded) in 657 s`. Failures: `Throughput scales with participant count` and the voice-cost snapshot
  (both known), and the two importer tests of finding 5.
- After them, `--affected` from the day start: `ran 1586 of 11309 tests (phases: 3 4 5 6 17 18 22 27; flavors: plain; display:
  excluded; crash tests: excluded; soak: excluded; script demo: excluded) in 407 s`, no failures.
- Shader checks: the Metal staleness test (with `morph.comp`), `shaders_check`, `shaders_portability`, `metal_validation_planted`,
  `metal_autorelease_pools`: pass. The DXIL staleness test fails on `morph.comp` (finding 2). Docs: 7 of 7.
- **Look:** the model viewer's screenshots of the robot and the field on Metal look right, and `model_viewer_window_cocoa` passes.
  Neither file has morph targets, vertex colours or punctual lights; sample files of those are Day 15's.
- After the review's fixes: every phase plain (the renderer's CMake changed), `ran 4103 of 11309 tests (phases: all; flavors: plain; display: included; crash tests: excluded; soak: excluded; script demo: excluded) in 670 s`; the only failures are the known voice-cost snapshot and the DXIL staleness test on `morph.comp` (finding 2, for the PC); docs 7 of 7.
- **Not done:** the arm64 compile-only build (no macOS code changed, and 4.5 GB were free); unity-off and sanitizer builds (the Mac's rules).

**Done when:** every feature of the series draws on Metal, and no Metal library is stale.

### Post-implementation code review

`/code-review high` on the day's diff: 8 findings; 7 fixed, 1 recorded (findings 5, 6 and 8 were about the script demo switch, dropped on
the rebase).
1. **`morph.comp`'s DXIL is stale** (the old 160-byte block), so the DXIL staleness test fails on it everywhere and Direct3D 12 morphs
   wrongly until it is re-minted. Recorded: the Mac has no dxc; the PC re-mints it on Day 13 (`KNOWN-ISSUES.md`).
2. **The push-size `static_assert` used literals**, tied to nothing the shader declares. Fixed: `gbuffer.cpp` includes
   `morph.comp.refl.inc` and asserts that the CPU block's size and `MaterialSystem::kMorphPushBytes` equal `kMorphCompPushSize`,
   that the targets start after the eight header words, and that eight targets fit. With the CPU block set to 40 words (the old
   layout's size) the build stops on the first assertion (checked, then restored).
3. **The new tests re-implemented `gpu_test::CreateTestDevice`**, without Vulkan's synchronization validation. Fixed: both use it.
4. **Compiling the two renderer files into `shader_mint_lib` put the same symbols in two archives.** Fixed: the `renderer_reflection`
   library (finding 1 above).
5. **`CLAUDE.md`'s closing run would leave out the script demo on the Mac without saying so.** Fixed at first (the rule named the
   exception); moot after the rebase, since the Mac defines no script demo tests (`cffc0d1c`).
6. **The switch was keyed on macOS, not on the slow machine.** Fixed at first with a per-machine record (`--machine-script-demo`);
   dropped on the rebase for the PC's Linux-only change.
7. **The spot test took the face's extent from red above 0**, assuming a black background and no ambient. Fixed: it compares with a
   picture without the light.
8. **The new label reused `script_demo`, the plain flavor's own label.** Fixed at first (`script_demo_suite`); dropped with the switch.

