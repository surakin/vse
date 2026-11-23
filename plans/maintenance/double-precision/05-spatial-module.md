# Maintenance: double precision, Day 5 — The spatial module in double precision

**Series:** [double precision](../double-precision.md)

## Tasks

- [ ] `engine/spatial`: the structures (grid, BVH or whichever the module holds), the pair feed and the query vocabulary (rays, boxes, spheres, nearest) take and return `f64` world positions and boxes; local or relative values stay `f32` per the spec.
- [ ] The ECS sync system writes `f64` positions; `physics`, the renderer and audio read through the same queries (their own days adapt the callers).
- [ ] Tests: the existing spatial suite at the origin gives the same results; the same scene 100,000 km away gives the same pair lists and query results (compared after subtracting the offset), and a million-instance query stays within the existing time bound.
- [ ] Docs and module guide updated; `ARCHITECTURE.md` untouched unless an edge changes.
- [ ] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** Spatial queries are exact far from the origin and unchanged near it.

## Notes

- Written 2026-11-23.

## Post-implementation code review

(To be filled in when the day is done.)
