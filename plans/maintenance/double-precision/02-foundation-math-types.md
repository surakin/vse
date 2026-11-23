# Maintenance: double precision, Day 2 — The foundation math types

**Series:** [double precision](../double-precision.md)

## Tasks

- [ ] Add the types the spec chose (`Vec3d`, the `f64` translation of a transform, conversions to and from the `f32` forms with the narrowing named so it cannot happen by accident) to `engine/foundation/math/`, with `lang/scalar.h` for the math (libm as is, no own math).
- [ ] `Aabb` and `Sphere` in `f64` where the spec says a world box needs it (`math/geometry.h`), with conversions and the frustum test taking a camera-relative box.
- [ ] The determinism rule: the new operations use `+ - * / Sqrt` only; a test compares them with a reference over 10,000 random values and a fixed set of edge cases (large, tiny, negative zero).
- [ ] Precision test: an object 10,000 km from the origin keeps a 1 mm offset exact in the `f64` form and loses it in the `f32` form; the narrowing of a difference of two such positions is exact to `f32`.
- [ ] Tests, docs (`///`, `docgen lint` zero, `docs/guide/foundation-types.md` or the math page), module map and `phases.txt` lines.
- [ ] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** The types exist, are tested for exactness and determinism, and nothing else uses them yet.

## Notes

- Written 2026-11-23.

## Post-implementation code review

(To be filled in when the day is done.)
