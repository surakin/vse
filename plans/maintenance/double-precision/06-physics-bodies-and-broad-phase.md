# Maintenance: double precision, Day 6 — Physics: bodies, broad phase, queries

**Series:** [double precision](../double-precision.md)

## Tasks

- [ ] Body positions in `f64` (orientation, velocities and inertia stay `f32`), the integration of positions in `f64`, the broad phase and the pair tracker on `f64` boxes; the pair feed from `engine/spatial`.
- [ ] Scene queries (ray cast, overlap, sweep, closest point) take `f64` origins and return `f64` points with `f32` normals and distances relative to the origin.
- [ ] The snapshot (`physics` snapshot version moves; current version only), the state hash (positions hashed as `f64`), `PhysicsWorld::SetTransform` and the body description take the new type.
- [ ] The golden test tables are regenerated once, deliberately, on this day (the step now rounds differently), and again equal for 1 to 32 threads; the day file records the old and new hashes and why the numbers moved.
- [ ] Tests: a stack, a pile and a bridge built 10,000 km from the origin behave like the same scene at the origin (positions after 600 steps equal after subtracting the offset to within the spec's tolerance, bit for bit if the spec's body-relative rule makes it so).
- [ ] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** Bodies move and are found exactly far from the origin, and the golden hashes are re-baselined and equal across thread counts.

## Notes

- Written 2026-11-23.
- Day 1 pick (A): body positions and broad-phase boxes in `f64`; contacts built body-relative in `f32`; snapshot version 3; golden hashes re-baselined once here. Baseline of the real step: `physics_bench --scene drop --bodies 5000 --threads 1`: 17.8 ms falling, 32.0 landing, in the day file with the after numbers.

## Post-implementation code review

(To be filled in when the day is done.)
