# Maintenance: double precision, Day 2 — The foundation math types

**Series:** [double precision](../double-precision.md)

## Tasks

- [x] Turn `Vec2`, `Vec3`, `Vec4`, `Aabb`, `Sphere`, `Plane` and `Frustum` into templates on the scalar (`Vec3T<S>`, ...), keep `Vec3 = Vec3T<f32>` and the other names as aliases so no existing use changes, add the `f64` aliases (`Vec3d`, `Aabbd`, `Sphered`, `Frustumd`), the exact mixed operators, explicit instantiation of the out-of-line members for both scalars, and `Placement`; the narrowing from `f64` to `f32` is a named call so it cannot happen by accident. All in `engine/foundation/math/`, with `lang/scalar.h` for the math (libm as is, no own math); a build of all of `engine/`, `game/`, `tools/` and `tests/` with no use changed is the first check.
- [x] The `f64` box, sphere, plane and frustum are the same templates (`math/geometry.h`); the frustum test takes a box of its own scalar, and a conversion from `Aabbd` to `Aabb` relative to an origin exists for the write step.
- [x] The determinism rule: the new operations use `+ - * / Sqrt` only; a test compares them with a reference over 10,000 random values and a fixed set of edge cases (large, tiny, negative zero).
- [x] Precision test: an object 10,000 km from the origin keeps a 1 mm offset exact in the `f64` form and loses it in the `f32` form; the narrowing of a difference of two such positions is exact to `f32`.
- [x] Tests, docs (`///`, `docgen lint` zero, `docs/guide/foundation-types.md` or the math page), module map and `phases.txt` lines.
- [x] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** The types exist, are tested for exactness and determinism, and nothing else uses them yet.

## Notes

- Written 2026-11-23.
- Day 1 pick, changed by the user the same day (spec `2026-11-23-double-precision-design.md`): the vector, box, sphere, plane and frustum types are templates on the scalar with the `f32` names kept as aliases; `Mat4` and `Quaternion` stay `f32`; `Placement` is a plain struct (an `f32` linear block plus a `Vec3d` position, `RelativeTo(origin)`); implicit exact widening, explicit named narrowing. The whole `engine/` and the 3,400 outside uses must compile unchanged: a build of everything is the first check of the day.

## Post-implementation code review

`/code-review high` over the day's diff: 2 findings, 1 fixed, 1 declined.

1. `AabbT<f64>::Narrow` rounds to nearest, so a narrowed box can be a hair smaller than the exact one (a culling hazard if a later day culls an `f32` box made from an `f64` world box). Declined for now: nothing uses it yet and the doc comment says so; the spec's rendering path culls in `f64` and narrows only camera-relative differences. Day 4 must use an outward narrowing if it ever narrows a box for culling (no `NextAfter` wrapper exists yet).
2. The f64 sphere, plane and bounds had no test. Fixed: `The f64 sphere, plane and bounds work like the f32 ones`.

Found while running the scoped tests (left by Day 493, not by this day's code): three `listsettings.golden` files, the renderer settings count (15, now 18) in two tests and a float-default text comparison were stale; fixed here.

Runs: clang unity and GCC unity-off build clean. Plain `ran 4482 of 11993 tests (phases: all; flavors: plain; display: included; crash tests: excluded; soak: excluded) in 154 s`: 5 known failures (DXIL and Metal libraries, voice-cost snapshot, two X11 window tests). asan `ran 3670 of 11993 tests (phases: all; flavors: asan; ...) in 181 s`: the two minted-library failures. tsan not run. `ctest -L docs` passes; guide page `docs/guide/foundation-math.md` ("Double precision"). Reduced runs.
