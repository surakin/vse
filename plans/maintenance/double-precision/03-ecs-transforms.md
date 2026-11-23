# Maintenance: double precision, Day 3 — Entity transforms in double precision

**Series:** [double precision](../double-precision.md)

## Tasks

- [x] `ecs::LocalTransform::position` and the translation of `WorldTransform` in `f64` (rotation and scale stay `f32`; the form is the spec's), the propagation system (`MakeTransformSystem`) composing in `f64` for the translation.
- [x] Every user of the transforms in `engine/ecs`, `engine/scene` and `engine/engine_c` compiles against the new type; each narrowing to `f32` is explicit and named in a comment (or, where it is a render matrix, comes in Day 4).
- [x] The `TransformReport` counters and the dirty-flag rules stay; a test with a parent chain 10,000 km away proves a child's world position is exact to the `f64` rounding.
- [x] Storage: the entity save format stores the `f64` position (the format version moves; current version only); tests round-trip a world with entities 1,000 km away.
- [x] Docs and tests as the rules above.
- [x] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** An entity far from the origin keeps its position exactly through the hierarchy and a save and load.

## Notes

- Written 2026-11-23.
- Day 1 picks: `LocalTransform` keeps its `Quaternion` and `Vec3 scale` and gets a `Vec3d position`; `WorldTransform` becomes a `Placement`; the storage `FieldKind` gains `Vec3d` and `Aabbd`.

## Post-implementation code review

`/code-review high` over the day's diff: 3 findings, 1 fixed, 2 recorded.

1. `WorldTransform` keeps `matrix` and `position` (two members for one fact); a hand-written writer that sets only `matrix` leaves `position` at zero. Fixed for the two in-tree writers (the C API attachments and the renderer demo) with `WorldTransform::FromMatrix`; the pair is kept on purpose until Days 4-8 move the readers of `matrix` to `position` or a camera-relative matrix (`matrix` then goes).
2. The SQL projection of a `Vec3d` field has no test (the binary path does). Recorded, not fixed.
3. Scripts, physics sync, the C API and spatial still read narrowed positions. Recorded in KNOWN-ISSUES; each is the later day.

Also: the propagation first built `Placement`s per entity and ran 2x slower in the 100,000-entity bench (7.1 ms); `ComposeWorld` multiplies the matrices as before and adds only the `f64` translation (5.2 ms in a Release tree, noisy machine).

Runs: see `STATUS.md`. Pages touched: `docs/guide/ecs.md`, `docs/guide/storage.md`; `ctest -L docs` passes.
