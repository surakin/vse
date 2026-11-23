# Maintenance: double precision, Day 10 — The C API and the C++ wrapper

**Series:** [double precision](../double-precision.md)

## Tasks

- [ ] `engine_transform` and every struct that carries a position (`engine_transform`, collider offsets, rays, queries, lights, the camera, audio) get an `f64` form through sized-struct growth: a new struct or appended fields with the `struct_size` rules, never a change of the old layout; `engine_api` grows only at the end; old entry points keep their `f32` forms and narrow, documented as lossy far from the origin.
- [ ] `terrain_height`, `physics` ray and overlap results, `component_get/set` of transforms and the world-matrix read have `f64` forms.
- [ ] The ABI snapshot is regenerated (`abi_snapshot_gen.c`) and `include/engine/engine.h` wraps every new member (`engine_h_coverage`).
- [ ] Host tests (`host_c`, `host_cpp`, `host_runtime`, `host_terrain`, `host_script`) gain a far-from-origin case; the surface rebind keeps `f64` CPU sources.
- [ ] Docs: `docs/guide/engine-library.md`.
- [ ] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** A C host places, moves and queries entities 100,000 km away exactly.

## Notes

- Written 2026-11-23.

## Post-implementation code review

(To be filled in when the day is done.)
