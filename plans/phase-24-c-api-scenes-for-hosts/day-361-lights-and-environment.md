# Phase 24 / Day 361 — Lights & the Environment

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Day 360; Phase 5 (lighting pass).

Lit scenes: directional and point lights as components, an ambient term and a background for the world.

## Tasks

- [x] `ENGINE_COMPONENT_LIGHT` (`engine_light`: kind directional or point, color, intensity, radius) mapping to `scene::LightSource` (a directional light points along the entity's -Z, stated in the header); the render sync already mirrors it; limits (lights per scene) are reported, not crashed on (`lights_refused` in the surface's state).
- [x] `world_set_environment(world, &engine_environment)`: ambient color and intensity, the clear color behind the scene (a flat sky color, optionally a vertical gradient), exposure of the tone map; a world has one environment, every surface that shows it uses it; unset gives documented defaults.
- [x] Shadows: only if the renderer has them (it does not today); recorded as a later item in `KNOWN-ISSUES.md` and in the guide, not built. *(not built; filed in KNOWN-ISSUES.md)*
- [x] Tests: a lit sphere and plane under a directional light matches a golden image; the light's rotation changes the shading in the expected direction (a pixel on the lit side is brighter than on the dark side, checked numerically); a point light's falloff; the ambient term alone lights an unlit-by-lights scene; a removed light disappears next frame; the environment's clear color is the background.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds. *(not run: postponed to the closing pass by the user's decision)*
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`. *(Linux only, and not run)*
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`. *(postponed to the closing pass; see the section below)*
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a scene with a directional light, an ambient term and a sky color renders to a golden image.

## Outcome

Day 361 done on the Linux machine (2026-11-15), tests written and built, **not run** (the closing pass runs them). `ENGINE_COMPONENT_LIGHT`, `world_set_environment`, `lights_refused` in `engine_surface_state`; `LightingPass::SetBackground` (the accumulation target's clear colour) and `SceneFrame::SetEnvironment` (exposure and background) in the renderer; `SurfaceScene::SyncLights` mirrors the world's light entities and its ambient term into the render scene each frame, and keeps the default sun only for a world with neither a light entity nor an environment. Tests (`host_scene.c`): argument checks, a directional light lights the triangle and turned around does not, a removed light is gone, ambient alone lights the scene, a point light's near, far and beyond-radius brightness, the sky colour at a corner pixel, 70 lights against room for 63 (`lights_refused == 7`). No golden image is added; the closing pass decides. Builds: Clang, unity. Not done: shadows, a sky gradient.

## Post-implementation code review

Self-review (the formal `/code-review high` runs once in the closing pass): the default sun used to be added in the constructor and could never go; it is now managed per frame so a light entity or an environment removes it. A zero-length world axis of a directional light is counted in `lights_refused` rather than aborting the renderer.
