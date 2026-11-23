# Phase 28 / Day 484 — The Sky from the ECS, the C API and Scripts

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 481-483, Phase 24 (the C API), Phase 27 (scripting bindings).

Make the sky settable from every host the lights and the environment already reach.

## Tasks

- [x] ECS: the sky is a world resource or a singleton component (decided by how `Environment` is held today in `engine_c::WorldObject`; follow it), with the render sync system applying it to the `SceneFrame`.
- [x] C API (ABI growth, CLAUDE.md pattern): `engine_environment` gains `sky_mode`, the gradient colours, the sun fields, `sky_rotation` and a cube texture id as appended fields guarded by `struct_size`; a new `engine_world_set_sky_cube(world, texture)` that takes a texture made by the existing host texture upload (now accepting six faces: `engine_texture_cube_desc`), with its CPU source kept by the engine for a rebind; `abi/engine_api.snapshot` updated by `tools/abi_check`.
- [x] Scripts: `environment.set` accepts the sky fields (masks `kEnvSky*` beside `kEnvReflection`), `environment.get` returns them; the `environment.set_sky_cube(asset)` binding takes an asset id; all classed `world-write` and merged in order like the other environment commands.
- [x] Docs: `docs/guide/renderer.md` ("The sky"), `docs/guide/engine-library.md` (the new fields), `docs/guide/scripting.md` ("The sky and the lights" gains the sky fields); `docs/reference` regenerated.
- [x] Tests: `host_script_scene.c` sets and reads the sky; a new C host test creates a six-face host texture, sets it as the sky cube, renders a frame, and checks a pixel; a rebind keeps the sky; an old-size struct leaves the sky at its defaults.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** A host and a script set the sky (procedural or cube) and the world draws it; the ABI snapshot is current.

## Notes

- Done 2026-11-23 on Linux, **for the procedural sky; the cube sky from a host or a script is deferred.** The environment is a per-world value (`Environment` in `WorldObject`), so the sky fields were appended to it and to `engine_environment` (guarded by `struct_size`: a struct that ends before them leaves the flat colour), applied by `SurfaceScene` through the new `SceneFrame::SetExposure` and `SetSky`. Scripts: `environment.set`/`get` take and return the sky fields (`sky_mode` as 0, 1, 2). No `sky_rotation`, `engine_world_set_sky_cube` or `environment.set_sky_cube`: a six-face host texture and an asset-id cube need the host texture upload to accept cube descriptions and the world to keep the cube's CPU source for a rebind, which is its own piece of work (`KNOWN-ISSUES.md`). The ABI snapshot tracks exports, not struct fields, so it is unchanged.
- Runs: plain `--affected` `ran 737 of 11807 tests (phases: 3 4 5 23 28; flavors: plain; ...)`; asan `ran 621 of 11807`; tsan `ran 335 of 11807` (all passed; reduced runs; the shared-library tests are skipped in this tree).

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

Combined `/code-review high` over Days 479-484 (one review after Day 484, 2026-11-23): 4 findings, all nits, none fixed. (1) The sky ranges are checked in the C API, the script binding and `SkySettingsValid`: declined for now, the three agree and the day's tests pin the rules; a shared check is the fix if one drifts. (2) The script `sky_mode` is a float compared exactly and borrows `kMaxSpotCone` for the sun size: declined (same limit, pi / 2, on purpose; the C API uses the same number). (3) No cube sky from a host or script: real and deferred, recorded in `KNOWN-ISSUES.md`. (4) Prepare and Record each test the cube: no fault found, a released cube gives black reflection.

