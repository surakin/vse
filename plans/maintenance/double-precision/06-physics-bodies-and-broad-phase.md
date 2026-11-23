# Maintenance: double precision, Day 6 — Physics: bodies, broad phase, queries

**Series:** [double precision](../double-precision.md)

## Tasks

- [x] Body positions in `f64` (orientation, velocities and inertia stay `f32`), the integration of positions in `f64`, the broad phase and the pair tracker on `f64` boxes; the pair feed from `engine/spatial`.
- [x] Scene queries (ray cast, overlap, sweep, closest point) take `f64` origins and return `f64` points with `f32` normals and distances relative to the origin.
- [x] The snapshot (`physics` snapshot version moves; current version only), the state hash (positions hashed as `f64`), `PhysicsWorld::SetTransform` and the body description take the new type.
- [x] The golden test tables are regenerated once, deliberately, on this day (the step now rounds differently), and again equal for 1 to 32 threads; the day file records the old and new hashes and why the numbers moved.
- [x] Tests (changed, see Notes): a body in free fall, the broad phase's pairs, ray, overlap, shape cast and closest point queries, a snapshot and the position limits, all 10,000 km away, equal the same scene at the origin (`tests/physics/far_world_test.cpp`). The stack, pile and bridge 10,000 km away move to Day 7: they rest on contacts, which are still single precision in world coordinates.
- [x] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** Bodies move and are found exactly far from the origin, and the golden hashes are re-baselined and equal across thread counts.

## Notes

- Written 2026-11-23.
- Day 1 pick (A): body positions and broad-phase boxes in `f64`; contacts built body-relative in `f32`; snapshot version 3; golden hashes re-baselined once here. Baseline of the real step: `physics_bench --scene drop --bodies 5000 --threads 1`: 17.8 ms falling, 32.0 landing, in the day file with the after numbers.

## Post-implementation code review

`/code-review high`: 5 findings, none needing a change on this day, all recorded:
1. (important) The narrow phase's poses (`ColliderPose`) and the solver's centres of mass narrow the `f64` positions, so contacts far from the origin round to the `f32` grid. This is Day 7's work; the stack/pile/bridge test moved there.
2. (pre-existing) Joints anchored to the world (no body) keep an `f32` world anchor. Day 7.
3. (important) The solver narrows positions (`CenterOfMass`, joint anchors). Day 7.
4. (nit) Snapshot version 3 and input-log version 2 refuse older data (current version only); contact points in a snapshot stay `f32`.
5. (nit) The golden tables, the step, joint and hinge hash constants, the C sample's checksum, the render-features demo hash and the script demo's cross-machine hash were re-baselined on Linux only; Windows and macOS runs are listed in KNOWN-ISSUES.

Changes beyond the day file's list: the queries put the other colliders in their own frame (`PoseFrom`: the difference of two positions in `f64`, narrowed once), so they are exact far away already; `Shape::Bounds(Vec3d, rotation)` and `Shape::BoundsExtent` are new public API; `TerrainHeight` takes `f64` x, z and returns an `f64` height; the character controller walks in `f64`; `kMaxWorldCoordinate` (1e12 m) bounds body positions, `kMaxCoordinate` (1e7) stays for sizes, speeds and `f32` points.

**The hashes moved because** positions are now hashed as `f64` (eight bytes each instead of four) and the integration adds the `f32` velocity times the step to an `f64` position (the old sum was rounded to `f32` every step). Old and new values:

| Scene | Old, step 600 | New, step 600 |
|---|---|---|
| Stack | 0x4fcfe6a4b3e3ccb2 | 0x8218b5703ae3f5f0 |
| Pyramid | 0x0ab83763c8fdac6c | 0xf98addac7a78d90a |
| Pile | 0x87517963c57c5c64 | 0x3c897a1bdf6911f4 |
| Bridge | 0x077ca18fa61333c6 | 0x79148ef0f84fb88f |
| Walker | 0xd55d16fd4e48a862 | 0x4278bd5b79993864 |
| Bullet | 0x390632fda4fe9522 | 0x602b89b3a693debc |
| Terrain | 0x52eeeb961668c74f | 0x6319c18f62fad8e7 |

(Every step of every scene is in `tests/physics/golden_test.cpp`; the golden test again passes for 1 to 32 threads.) Other constants: step hash 0x25109949efc555a6 to 0x79eda110a53a3725, joint 0xa2253470c556a2d0 to 0xad0fffc8778b814f, hinge 0xf159eb48d1b6d124 to 0xebde560bdeb9a442, C sample checksum 0x661cbfc4 to 0xe7f06c62, render-features demo hash 0x1e1cb53ed947ac04 to 0xfcca005c39a038f4, script demo hash 0xcd4a4433e698fad7 to 0x6223dffc07e3f7f9.

**Cost** (`physics_bench --scene drop --bodies 5000 --threads 1`, Release, this machine): falling 17.8 to 18.9 ms, landing 32.0 to 34.0 ms (broad phase 8.8 to 9.7 ms, narrow 2.2 to 2.6, solve 19.6 to 20.1): about 6 percent more, from the broad phase's double-size boxes. Under the spec's 5 percent line for Day 7's pair subtraction; Day 7 measures its own.

Runs: plain `ran 901 of 12023 tests (phases: 3 23 25 27 28; flavors: plain; display: excluded; crash tests: excluded; soak: excluded) in 68 s`, asan `ran 823 of 12023 tests (... flavors: asan ...) in 165 s`, tsan `ran 625 of 12023 tests (... flavors: tsan ...) in 242 s`; no failure in any. Reduced runs. Builds clean: clang unity, GCC unity-off. `docgen lint` 14187 of 14187; `ctest -L docs` 13 of 13.
