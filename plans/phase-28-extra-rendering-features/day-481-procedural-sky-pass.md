# Phase 28 / Day 481 — The Sky Pass and the Procedural Sky

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 479-480 (the pass uses no cube yet), Phase 5 (the lighting pass).

Draw the sky behind the world, procedurally: the default when no cube map is assigned.

## Tasks

- [ ] `renderer::SkySettings` (a value: `mode` None, Color, Procedural, Cube; `zenith`, `horizon`, `ground` colours, `sun_direction`, `sun_color`, `sun_size`, `horizon_blend`, `exposure`, `rotation`) with validation, defaults that reproduce today's flat `sky_color` background when the mode is Color.
- [ ] `shaders/sky.frag.slang` (and `sky.vert.slang` as a full-screen triangle sharing `fullscreen.vert`): rebuilds the view ray from the inverse view-projection in the frame block, a gradient from `ground` through `horizon` to `zenith` by the ray's elevation with a smooth horizon band, and a sun disc with a soft edge; no `float3` push members (scalars, the lesson of the lighting pass).
- [ ] The pass is added to the render graph after lighting and before tone mapping or the output step, reads the depth buffer with a `depth == far` test so only uncovered pixels change (no draw order dependence), and writes the HDR colour target; with the mode None it is not in the graph.
- [ ] `SceneFrame::SetSky(const SkySettings&)` (and `SetEnvironment` keeps working: its background colour becomes `mode = Color`); the setting is classified in `docs/guide/render-settings.md` as a per-frame value (no rebuild).
- [ ] Slang compiled and minted to SPIR-V (`cmake --build build --target shaders`); the DXIL and Metal outputs come on Days 502 and 503 (recorded in `KNOWN-ISSUES.md`).
- [ ] Tests: the CPU model of the gradient (a function the shader mirrors, tested at the zenith, horizon and nadir directions); a render test that draws the sky into a small target and checks the corner and centre pixels against the model within a tolerance; a covered pixel (a mesh in front) is unchanged; a resize and a reset keep working.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** The renderer draws a procedural sky behind the scene on Vulkan, the flat colour stays the default, and the gradient matches its CPU model.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
