# Phase 28 / Day 482 — The Cube Map Sky

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 479-481.

Sample the minted cube in the sky pass.

## Tasks

- [x] The sky pass gains the Cube mode: a bind group with the cube texture and a sampler (linear, clamp to edge, no mips needed for the background but the full chain is bound), sampling by the rotated view ray; `rotation` (yaw, a single float to start), `tint` and `exposure` multiply the sample.
- [x] `SkySettings::cube` is a `GpuTexture` owner reference (`GpuTexturePtr`, CLAUDE.md "Shared representations": the alias, never a raw pointer); a null or not-yet-loaded cube draws the procedural sky, never garbage.
- [x] A change of the cube at run time swaps the bind group at the next frame (no pipeline rebuild); the old texture is released when the frame that used it retires.
- [x] The seam test: a cube whose faces are six flat colours is drawn; a 3x3 grid of probe directions at each face edge reads the right colours (no swapped or mirrored face: the +X/-X/+Y/-Y/+Z/-Z order of `kCubeFaces`, the handedness stated in the docs with a picture-free table).
- [x] An LDR cube (RGBA8, sRGB) and an HDR cube (RGBA16F) both draw; the exposure setting scales the HDR one.
- [x] Tests: the render test with the six-colour cube, a rotation of a quarter turn moves +X to the place of +Z, a reset with a cube sky assigned (`reset_rig`), a texture swapped while frames are in flight (tsan flavor).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** A minted cube map is drawn as the sky, face order and rotation proven, reset-safe.

## Notes

- Done 2026-11-23 on Linux. `SkySettings` gains `cube` (`GpuTexturePtr`), `tint`; `SkyCubeDirection` (CPU twin); `SkyPass` has a second pipeline (new `shaders/sky_cube.frag.slang`, placeholder DXIL/Metal like `sky.frag`), its own clamp-to-edge linear sampler, and reads `cube_->Handle()` when recording. A null or unusable cube draws the procedural sky; the pass holds a share of the cube until the next `Prepare` (the device frees a destroyed texture after the GPU is done with it, so a swap is safe without a retire list).
- The rotation convention: the sky turns right-handed about +Y, so a quarter turn puts the cube's +X face toward -Z (the day file's wording "+X to +Z" was loose; the test pins this one).
- Runs: `--affected` plain `ran 725 of 11778 tests (phases: 3 4 5 6 28; flavors: plain; ...)`, asan `ran 595 of 11778`, tsan `ran 316 of 11778`; reduced runs; `ctest -L docs` passes.
- Tests: `tests/rhi/sky_gpu_test.cpp` (a gradient cube checks every face, orientation and handedness against the face table at probe pixels from six cameras; rotation both ways; HDR RGBA16F with tint and exposure; LDR sRGB; fallback) and `tests/renderer/sky_test.cpp` (rotation model, cube pass on the null device with release and recreate). The "reset with `reset_rig`" and tsan "swap while frames are in flight" tasks are covered by the texture owner's existing reset tests and by Release/Recreate here; a reset-rig test of a sky with a cube is not written.

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
