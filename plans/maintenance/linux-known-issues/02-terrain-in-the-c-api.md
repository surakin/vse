# Maintenance / linux-known-issues / Day 02 — Terrain in the C API and scripts: lifetimes, queries and the cube sky

**Series:** [Known issues Linux can fix](../linux-known-issues.md)

## Tasks

- [ ] `scripts/test-phase.sh --start-day` at the start; scoped runs (`--affected` plain, then asan and tsan when the day touches concurrency or lifetimes) at the end, quoting the script's `ran X of Y` line.
- [ ] Release replaced height maps: `engine/engine_c/resources.cpp` `PinHeightMap` drops a pin when the resource is replaced or released (keyed by resource id and version, not only the handle), `SurfaceScene::terrain_sources_` and `collider_meshes.cpp`'s `collider_fields` drop the entries of a source no entity uses; test: replace a 1024 by 1024 map ten times and count the pins and live sources (one).
- [ ] `height_map_create`: use `TryResize` for the host-sized buffers and return `ENGINE_ERROR_LIMIT_EXCEEDED` instead of aborting; lower the transient memory by building the mips from the host's samples in place; test with an allocation limit (the test allocator of `tests/support`, if there is none, a size cap of the call) and a 8192 by 8192 map's peak (measured, recorded).
- [ ] `terrain_height`: a terrain whose parent has no world transform yet answers `NOT_READY`; a terrain with a scale or a tilt is skipped like the renderer skips it; the pin lock is taken once per call and the first pin's hash is made outside the world lock; tests for the three cases.
- [ ] The terrain component set is atomic: validate every step (the collider, the body, the source) before the first removal, or roll back; test with a forced `Add` failure through a full archetype or a refused body.
- [ ] The environment's sky fields are validated whatever the mode is: the host call (`world_set_environment`) and the script merge validate the merged `Environment` and refuse it with `INVALID_ARGUMENT` / a script error (the surface's fallback stays as the last defence); `sun_direction` finiteness; tests with the zero-filled struct and the infinite sun direction.
- [ ] A cube sky from a host or a script (Day 484 of the phase): `engine_environment.sky_rotation`, `engine_world_set_sky_cube` (a six-face host texture or an asset id, the CPU source kept for a rebind) and `environment.set_sky_cube`; table order and `struct_size` rules as for the other calls; the `engine.h` wrapper and `engine_h_coverage`; a reset-rig test (`tests/renderer/reset_rig.h`) of a scene with an environment cube, counting `Device::Resources()` before and after.
- [ ] The C API's terrain material: all four layers validated whatever `layer_count` is (the pass already resets the unused ones); tests with a NaN tint in layer 3.
- [ ] Read-back of a height field through the C API reports a height field, not a capsule (Day 491), and `ENGINE_SHAPE_HEIGHT_FIELD` is accepted by `body_set_collider` for a registered height map; test.
- [ ] Both compilers, unity on and off; public API documented (`docgen lint` zero, `docs` target, the guide pages); `/code-review high` on the day's diff, findings verified, fixed with a regression test or declined with a reason, recorded below.
- [ ] `KNOWN-ISSUES.md` entries removed or rewritten, `STATUS.md` updated; commit and push.

**Done:** A host and a script can set, replace and release terrain and the sky without leaks, aborts or wrong answers; the entries for Days 484, 486, 491 and 493 and the review's C API items are removed from `KNOWN-ISSUES.md`.

## Post-implementation code review

(To be filled in when the day is done.)
