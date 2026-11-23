# Maintenance: double precision, Day 11 — Demos, benchmarks, docs and closing

**Series:** [double precision](../double-precision.md)

## Tasks

- [x] The demos build and run (the closing run's demo tests). `renderer_demo` gains `--origin-offset M` (objects, lights, camera and the render origin placed M metres out) and the GPU test `The demo picture 100,000 km from the world origin equals the picture at the origin` (`tests/rhi/reset_golden_gpu_test.cpp`; it fails with the render origin left at zero). **Not done:** `physics_demo`, `model_viewer`, `render_features_demo`, `script_demo` and `ui_demo` have no offset option (`KNOWN-ISSUES.md`); their subsystems' far behaviour is covered by the module tests.
- [x] Benchmarks, Release, one machine, before (`04ec16c9`, built in a throwaway worktree) and after: `physics_bench --scene drop` 5000 bodies, falling 17.58 to 18.06 ms, landing 31.58 to 32.24 ms, asleep 32.2 to 33.1 ms (+2 to +3 percent); `renderer_bench --section frame --objects 10000`, single 1.017 to 1.048 ms, jobs 1.095 to 1.154 ms; `ecs_bench` hierarchy propagation (100,000) 3.24 to 2.89 ms, iteration and change-stamping unchanged, lifecycle unchanged; spatial benchmark at 1,000,000 boxes: build 426 to 458 ms, tree cull 0.135 to 0.144 ms, box overlap 4 m 1.07 to 1.01 us, 40 m 10.9 to 11.3 us, ray 1.45 to 1.76 us (the million-instance query time the Day 5 note asked for: no change beyond noise). There is no instancing benchmark yet (instancing is paused at Phase 28 Day 495). Numbers are in `docs/tutorials/big-worlds.md`.
- [x] Tutorial and mental-model page `docs/tutorials/big-worlds.md` with three compiled and run samples (`ctest -L docs`).
- [x] Windows and macOS: the list of runs is in `KNOWN-ISSUES.md` ("Double precision series ... what the Windows PC and the Mac must run"): the hash confirmations (physics golden table, step, joint and hinge constants, `physics_sample` `0xf013d004`, render-features demo hash, script demo `0x0c091fc4195f2f75`), the new far tests, the level and importer changes, the audio and script tests, the docs samples, and `--affected` plain and asan on both machines.
- [x] The closing run and the review (below).
- [x] `KNOWN-ISSUES.md`: the entry "Large world coordinates" and the Day 5 measurement note are removed; `STATUS.md` and `PLAN.md` updated.
- [x] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** Everything that holds a world position is double precision where the spec says, the full suite passes in every flavor on Linux, and the Windows and macOS runs are listed for the user.

## Notes

- Written 2026-11-23.

## Post-implementation code review

`/code-review high --max-findings all` over the whole series' diff (`f67afefe` to the working tree, about 170 files), in three slices read by three independent reviewers (foundation, ECS, scene, spatial and renderer: 8 findings; physics, script and audio: 6; the C API, storage, tools and games: 12): 26 findings, none a crash.

**Fixed (11):**
1. `physics/ccd.cpp` `LerpPoint` blended with an `f32` weight (`1.0f - t`), so a CCD hit at 1e8 m was placed up to about 3 m off; now `a + (b - a) * t` (the same defect as the interpolation fixed on Day 10).
2. `script_physics/physics_bindings.cpp` `TerrainHeightOf` narrowed the `f64` height to `f32` before giving it to a script; now `FromFloat(height)`.
3. `script/entity_bindings.cpp` accepted script positions up to 1e30 while physics refuses beyond 1e12: now the same limit.
4. `scene::UpdateRenderOrigin` moved the origin to NaN on a NaN camera; it now returns false for a non-finite camera (regression assertion added in `tests/scene/scene_sync_test.cpp`).
5. `SurfaceScene::PixelRay` returned the camera's place rebuilt from the `f32` camera (about 0.2 mm off at 1e8 m); it keeps the exact `Vec3d` (`host_scene.c` now uses a position that is not a float multiple, `1e8 + 0.001`).
6. `engine_c/runtime.cpp` `AttachWorld` placed a joint-attached entity with `f32` matrices; it composes the skeleton's exact placement with the joint and local transform.
7. The C header's docs of `surface_pixel_ray`, `surface_project` and `terrain_height` still said floats and no limit.
8. `abi_snapshot_gen.c` did not list `engine_world_transform.position`; added, snapshot regenerated.
9. The 1e12 limit was written as a literal in four places in `engine_c`; now `physics::kMaxWorldCoordinate` (the importer, a leaf module, keeps its literal).
10. `host_terrain.c` re-applied the far placement instead of restoring the original; restored.
11. `renderer_demo --origin-offset` parsed into a `long` (32 bits on Windows) and was missing from the usage text; now an `f64` through `ParseDoubleExact`, listed in the usage.

**Declined or recorded (15):** the hash grid keeps its 2^20-cell index limit (a grid set holds a bounded world; the trees do not have the limit; this is as before and now written in `KNOWN-ISSUES.md`); `SceneLink::generation` is the only re-base signal (documented on the field: set `origin` through `UpdateRenderOrigin`); a fresh `SceneLink` on the same system instance (low severity, not reachable through the engine's own code); a hand-built `WorldTransform` with only `matrix` set has `position` zero (`FromMatrix` is the documented way; nothing in the engine builds one otherwise); the ray and overlap queries check finite but not 1e12 (they return no hit; recorded); swept shape-cast poses are `f32` (the old limit, not a regression); the joint with a stale body id (unreachable with live ids); cosmetic comment and cast notes (2); the ABI version stays 1.0 (the user's decision: the only client is rebuilt with the header; recorded in the Day 10 file); a character set far from the origin is built at an `f32` position (recorded in `KNOWN-ISSUES.md`); `Vec3d` decoders accept NaN, inf and out-of-range values as the `f32` decoder did (pre-existing, recorded); missing negative-path tests (old `LEVL` version, 1e12 refusals of the force, impulse, teleport and overlap calls, a second re-base, point lights and terrain after a re-base) are recorded in `KNOWN-ISSUES.md`; the `listsettings.golden` blank descriptions are not from this series (reviewer note on the working tree, which has no change to those files).

Regression tests added: the NaN camera assertion, the exact ray origin. Not added (not practical for a file-local function in a pure-math path): a CCD far-hit test for `LerpPoint`; the fix is the same expression as the tested `Interpolated`.

Runs: `scripts/test-phase.sh --all --flavor all`: `ran 12043 of 12043 tests (phases: all; flavors: all; display: included; crash tests: included; soak: included) in 644 s` (before the last two edits below), 7 failed: the six known minted-library tests (DXIL and Metal, plain, asan and tsan, `KNOWN-ISSUES.md`) and `renderer_demo_settings_help`, whose `help.golden` gained the `--origin-offset` line (regenerated, passes). After it: `SceneLink` doc comment, the snapshot regenerated for `engine_world_transform.position`; `ctest -L docs` 13 of 13; `docgen lint` 14191 of 14191; scene, engine_library and phase 8 tests pass. GCC unity-off built clean on Day 10's code, not re-run after the review fixes.
