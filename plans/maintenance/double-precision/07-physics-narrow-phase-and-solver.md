# Maintenance: double precision, Day 7 — Physics: narrow phase, solver, joints

**Series:** [double precision](../double-precision.md)

## Tasks

- [ ] Contacts in the spec's frame (body-relative `f32` points around an `f64` anchor, or `f64` throughout if Spike 2 chose it), the solver, the joints and the continuous collision.
- [ ] The shapes: mesh and height-field colliders (`HeightField`'s centre and bound in `f64`, its samples and heights unchanged), compound shapes, convex hulls and the character controller.
- [ ] The character controller and the vehicle-like systems keep their tests; add far-from-origin variants of the walker and ramp tests.
- [ ] Performance: the Day 412 benchmark scenes before and after; record the cost in `docs/guide/physics.md` ("Cost") and fix anything above the spec's budget.
- [ ] Golden hashes re-baselined again if this day moved them, recorded as on Day 6.
- [ ] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** The whole physics module runs in the new precision, with the step cost recorded.

## Notes

- Written 2026-11-23.
- Day 1 pick (A): manifolds, solver and joints stay `f32` and body-relative. If the per-pair `f64` subtraction costs more than 5% of the narrow phase, amend the spec here before going on.

## Post-implementation code review

(To be filled in when the day is done.)
