# Maintenance: double precision, Day 3 — Entity transforms in double precision

**Series:** [double precision](../double-precision.md)

## Tasks

- [ ] `ecs::LocalTransform::position` and the translation of `WorldTransform` in `f64` (rotation and scale stay `f32`; the form is the spec's), the propagation system (`MakeTransformSystem`) composing in `f64` for the translation.
- [ ] Every user of the transforms in `engine/ecs`, `engine/scene` and `engine/engine_c` compiles against the new type; each narrowing to `f32` is explicit and named in a comment (or, where it is a render matrix, comes in Day 4).
- [ ] The `TransformReport` counters and the dirty-flag rules stay; a test with a parent chain 10,000 km away proves a child's world position is exact to the `f64` rounding.
- [ ] Storage: the entity save format stores the `f64` position (the format version moves; current version only); tests round-trip a world with entities 1,000 km away.
- [ ] Docs and tests as the rules above.
- [ ] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** An entity far from the origin keeps its position exactly through the hierarchy and a save and load.

## Notes

- Written 2026-11-23.
- Day 1 picks: `LocalTransform` keeps its `Quaternion` and `Vec3 scale` and gets a `Vec3d position`; `WorldTransform` becomes a `Placement`; the storage `FieldKind` gains `Vec3d` and `Aabbd`.

## Post-implementation code review

(To be filled in when the day is done.)
