# Maintenance: double precision, Day 10 — The C API and the C++ wrapper

**Series:** [double precision](../double-precision.md)

## Tasks

- [x] `engine_transform` and every struct that carries a position (`engine_transform`, collider offsets, rays, queries, lights, the camera, audio) change their position fields from `float[3]` to `double[3]` in place (the user, 2026-11-23: the only client is adapted with the engine; no struct growth, no `64` twins); rotations, scales, directions and sizes stay `float`.
- [x] `terrain_height`, `physics` ray and overlap results, `component_get/set` of transforms and the world-matrix read take and return `double` positions.
- [x] The ABI snapshot is regenerated (`abi_snapshot_gen.c`) and `include/engine/engine.h` wraps every new member (`engine_h_coverage`).
- [x] Host tests (`host_c`, `host_cpp`, `host_runtime`, `host_terrain`, `host_script`) gain a far-from-origin case; the surface rebind keeps `f64` CPU sources.
- [x] Docs: `docs/guide/engine-library.md`.
- [x] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** A C host places, moves and queries entities 100,000 km away exactly.

## Notes

- Written 2026-11-23.
- Day 1: 24 `float[3]` fields in `engine_c.h`; the user decided (2026-11-23, after the first version of this day) to change the types in place instead of growing structs: the `position64`/`point64`/`flags` fields, `engine_physics_event64` and ten `*64` table entries were removed again.

## Post-implementation code review

`/code-review high` over the day's diff (own reading): 2 findings. (1) **Fixed, found while writing the far host test:** `PhysicsWorld::Interpolated` weighted positions with `1.0f - t` in single precision, so the blend lost up to 2^-25 of the coordinate (3 mm at 100 km, 3 m at 100,000 km); it is now `previous + (current - previous) * t`, and `host_physics` `TestFarWorld` (1e8 m) fails without it. (2) Declined and then removed: the first version had `position64` and a flag; the in-place type change (user, 2026-11-23) removes the question.

Design: positions are `double` in the existing structs and entry points; the ABI snapshot is regenerated and the ABI version numbers are unchanged (the only client is rebuilt with the header). The surface scene has a render origin (steps of 4,096 m) like `scene::SceneLink`. `physics_sample` hashes the position doubles, so its golden checksum is re-baselined (`0xf013d004`, Linux; Windows and macOS pending).

Runs: plain `ran 581 of 12041 tests (phases: 3 23 25 28; flavors: plain; display: excluded; crash tests: excluded; soak: excluded) in 54 s`, asan `ran 511 of 12041 tests (... asan ...) in 135 s`, tsan `ran 313 of 12041 tests (... tsan ...) in 199 s`; plus phases 22, 26 and 27 plain, asan and tsan; only the known failures (Debug voice-cost snapshot, tsan LuaJIT sandbox flake). Reduced runs. GCC unity-off clean; `docgen lint` 14191 of 14191; `ctest -L docs` 13 of 13.
