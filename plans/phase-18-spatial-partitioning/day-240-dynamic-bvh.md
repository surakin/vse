# Phase 18 / Day 240 — Dynamic BVH: Insert, Remove, Move

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Day 239.

The structure for what moves: updated every frame at a cost proportional to what actually moved.

## Tasks

- [ ] **`spatial::DynamicBvh`** (`engine/spatial/dynamic_bvh.h`): `Insert(box, key, layer) -> ProxyId`, `Remove(proxy)`, `Move(proxy, new_box) -> bool` (returns whether the tree changed), `Query...` with the same traversal core as Day 239; nodes in a pool with a free list (no per-node allocation), proxy ids are stable `u32` handles (generation-checked: a stale id is refused, counted).
- [ ] **Fat margins:** leaves store the box expanded by the margin (`spatial.fat_margin`, a **fraction of the item's largest extent, default 0.25**, per structure at creation; Day 237 experiment (b)); `Move` is a no-op while the new box stays inside the stored fat box, and re-inserts otherwise (with the margin applied along the direction of movement too, the Box2D approach, amended by experiment (b)); the tight box is kept beside the fat one so queries can reject on it exactly where the caller wants exactness.
- [ ] **Balance:** insertion chooses the sibling by minimum surface-area increase (branch and bound), then refits upward with tree rotations; **an insert-built tree queries 1.8x to 3.3x slower than a rebuilt one (Day 237 experiment (a)), so the tree also has a bulk top-down `Build(items)` (the Day 239 binned SAH, one item per leaf; used on a scene load and as the full rebuild, 24 ms per 100,000) and `Optimize(budget)` rebuilds a fraction of the tree per call (a rotating subtree) or the whole tree when the quality metric has degraded past a threshold the day sets by test;** the quality metric from Day 239 is reported by `Stats()`.
- [ ] **Tests** (`tests/spatial/dynamic_bvh_test.cpp`): insert N, queries equal brute force; random insert/remove/move for 100,000 operations against a model (a flat `Vector` of boxes) with the tree's invariants checked (`Validate()`: parent/child links, boxes enclose children, free list intact, no leaf lost) every 500 operations; a stale proxy id is refused; moves inside the fat box change nothing (counted); a scene where everything moves every frame for 1,000 frames keeps the quality metric within 2x of a fresh build (with `Optimize`); allocation: steady-state `Move` and queries allocate nothing; move cost per object recorded.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "The dynamic BVH".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** moving objects update the tree incrementally, the tree stays balanced under heavy motion, and every query equals a brute-force scan.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
