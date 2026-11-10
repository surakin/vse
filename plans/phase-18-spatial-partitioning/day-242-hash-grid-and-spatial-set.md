# Phase 18 / Day 242 — Hash Grid & the Spatial Set

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 239-241; experiment (a) of Day 237.

The third structure (kept only if the experiment justifies it) and the composition that systems actually use.

## Tasks

- [ ] **`spatial::HashGrid`** (`engine/spatial/hash_grid.h`): cell size fixed at creation, items inserted into every cell their box touches, cells in an open-addressing table keyed by the cell coordinates; `Insert/Remove/Move/Query` with the Day 241 vocabulary; intended for many same-size dynamic items. If experiment (a) of Day 237 found no workload where it beats the dynamic BVH by 1.5x, this task is replaced by recording the numbers in the spec and removing the grid from the plan (the day then ends after the next task).
- [ ] **`spatial::SpatialSet`:** one static structure plus one dynamic structure (plus the grid if kept) behind one query API that visits all of them and merges results in the documented order; `AddStatic`, `AddDynamic`, `Remove`, `Move`, `RebuildStatic()` (rebuilds the static tree from its items, deterministic), `Stats()`; an item is either static or dynamic, and `MakeStatic/MakeDynamic(proxy)` moves it across (a door that was baked as static and now opens).
- [ ] **Tests** (`tests/spatial/spatial_set_test.cpp`): a mixed scene answers every query like the brute force; moving an item between static and dynamic keeps queries exact; rebuilding the static tree does not change results; the grid (if kept) equals brute force under 100,000 random operations and costs what experiment (a) predicted; sets are independent (an item in one set is invisible to another); results with both structures are in the documented order.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "The spatial set".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a set composes static and dynamic structures behind the one query API, exact against a brute force, with the grid kept or removed on evidence.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
