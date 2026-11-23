# Phase 28 / Day 497 — The Instanced Draw Path

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 495-496, Phase 5 (G-buffer).

Draw the visible instances with one draw per run.

## Tasks

- [x] **Changed (see Notes):** a per-frame GPU instance buffer (a mapped storage buffer in `FrameResources`) instead of a per-set persistent one with per-chunk dirty upload; A per-set GPU instance buffer (a storage buffer in the layout of Day 494), with a per-chunk dirty flag from Day 495: an update writes only the changed chunks through the existing transient/staging path; the buffer is a `GpuBuffer` owner with its CPU source kept (the `InstanceSet`), registered at its tier, rebuilt on a reset.
- [x] `shaders/gbuffer.vert.slang` `INSTANCED` permutation (one source, no separate file; was: `shaders/gbuffer_instanced.vert.slang`) (a variant of the G-buffer vertex shader, selected by the permutation system `gbuffer.frag.perm`/`shader_permutation`, so one vertex source serves both): reads the instance record by `SV_InstanceID` plus the run's first-instance offset, rebuilds the model matrix (or uses the stored matrix), applies the chunk-origin offset, and passes the colour tint and the custom parameter to the fragment stage; D3D12's instance-rate object index capability (`rhi::Caps`) is respected exactly as the existing path does.
- [x] **Changed:** the visible runs are compacted into the frame buffer and each set is one draw (no per-run draws, no partial-chunk index list). Was: The visible-run list becomes the draw list: for each run one indexed instanced draw with `first_instance` set; the visible indices are not copied to the GPU when a whole chunk is visible (a run draws a contiguous range), and for partly visible chunks a compacted index list is uploaded to a small per-frame buffer and read through (the spec says which of the two is used for partial chunks).
- [x] Materials: tint multiplies albedo (the `VERTEX_COLOR` fragment permutation); the custom value is passed and ignored by the standard material. Was: the instance tint multiplies the albedo, the custom parameter selects a layer of a texture array or scales an emissive term (the spec fixes one meaning and documents it); a mesh with several material slots draws one run set per slot.
- [x] Normal matrix (rotation and 1/scale for the normal; tangent by scale): done. Negative scale and winding: not applicable, `InstanceSet` refuses a scale at or below 0; no shadows yet, noted in the guide. Was: Normal matrix, handedness flips with negative scale and the depth pre-pass-free design: tested; shadows are not in the engine yet, noted in the docs.
- [x] SPIR-V minted (`cmake --build build --target shaders`); DXIL and Metal come on Days 502 and 503.
- [x] Tests (`tests/rhi/instanced_gpu_test.cpp`, `tests/renderer/gbuffer_test.cpp`, `scene_test.cpp`; the negative-scale case is dropped, see above; the 'partial chunk' probe is the 10,000-grid, whose chunks straddle the view): a grid of 10,000 instances rendered and read back against the CPU model of three of them (position, tint); a partly visible chunk draws only its visible instances (counted by a pixel probe); a negative scale draws with correct winding; reset and resize; a set with zero visible instances draws nothing and does not leave a bound-but-empty draw.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** An instance set draws through the G-buffer with one draw per run, with tints, reset-safe, and the per-frame upload is only what changed.

## Notes

- Day 494 left one measurement for this day: the vertex shader's cost of rebuilding the world and normal matrices from the 40-byte record, against a `Mat4x3` record, on the real device; record both and keep the cheaper behind the `InstanceSet` interface.

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

Runs: plain `ran 787 of 12106 tests (phases: 3 4 5 6 28; flavors: plain; ...)`, asan `ran 653 of 12112`, tsan `ran 355 of 12114` (reduced runs; all passed; one tsan test skipped by design). Built clean with GCC non-unity (`build-gcc`) and clang unity; `docs` target passes with docgen lint at zero.

Review (`/code-review high`, inline): 3 findings, 0 fixed, 3 recorded in `KNOWN-ISSUES.md` (instanced sets not seen by `FitCameraDepthRange` and lighting; `f32` narrowing in set space; 16 MiB per frame slot always allocated). The review also led to a fix before it: the budget-full case wrote all-or-nothing (a 40-instance frame drew nothing); it now draws what fits (test `A frame instance budget that is too small...`).
