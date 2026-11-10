# Phase 18 / Day 242 — Hash Grid & the Spatial Set

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 239-241; experiment (a) of Day 237.

The third structure (kept only if the experiment justifies it) and the composition that systems actually use.

## Tasks

- [x] **`spatial::HashGrid`** (`engine/spatial/hash_grid.h`): cell size fixed at creation, items inserted into every cell their box touches, cells in an open-addressing table keyed by the cell coordinates; `Insert/Remove/Move/Query` with the Day 241 vocabulary; intended for many same-size dynamic items. **Kept (Day 237 experiment (a)): on 100,000 same-size agents the all-pairs query is 2x faster than the best BVH and updates cost a quarter; the best cell size is 2x to 4x the item size.** A set chooses `Bvh` or `Grid` for its dynamic side; it is opt-in (it loses on variable sizes and rays).
- [x] **`spatial::SpatialSet`:** one static structure plus one dynamic structure (plus the grid if kept) behind one query API that visits all of them and merges results in the documented order; `AddStatic`, `AddDynamic`, `Remove`, `Move`, `RebuildStatic()` (rebuilds the static tree from its items, deterministic), `Stats()`; an item is either static or dynamic, and `MakeStatic/MakeDynamic(proxy)` moves it across (a door that was baked as static and now opens).
- [x] **Tests** (`tests/spatial/spatial_set_test.cpp`): a mixed scene answers every query like the brute force; moving an item between static and dynamic keeps queries exact; rebuilding the static tree does not change results; the grid (if kept) equals brute force under 100,000 random operations and costs what experiment (a) predicted; sets are independent (an item in one set is invisible to another); results with both structures are in the documented order.
- [~] **Runs (CLAUDE.md "Tests run by phase"; done on the Windows PC, so tsan, Clang and GCC wait for the Linux machine):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [~] **Two machines (CLAUDE.md "Windows is a supported platform"; done on the Windows PC, the open half is Linux):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "The spatial set".
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a set composes static and dynamic structures behind the one query API, exact against a brute force, with the grid kept or removed on evidence.

## Notes

- **Done on the Windows PC** (2026-11-10); the Linux build (Clang, GCC, unity on and off) and tsan wait for the Linux machine (`KNOWN-ISSUES.md`).
- **The grid is kept, on evidence** (Release, Windows PC, 100,000 agents of 1 x 2 x 1 on a 1000 x 1000 plane, each moving 0.1 a frame, cell 4): the
  all-pairs query takes 6.8 to 7.2 ms on the grid against 16.5 to 16.7 ms on a static tree of the same boxes (2.4 times faster; experiment (a): 2.1),
  both finding the same 20,090 pairs; an update costs 36 ns per moved agent (experiment: 60 ns), where a static tree would need its 35 ms rebuild.
- **`engine/spatial/hash_grid.h` / `.cpp`:** `HashGrid(cell_size)`, `Insert/Remove/Move` (generation-checked `ProxyId`; `Move` returns whether the cells
  changed), `Clear`, `IsLive`, `ItemOf`, `Stats()` (`HashGridStats`); the occupied cells in the engine's open-addressing `HashMap`, keyed by the three
  cell coordinates packed in 21 bits each. An item touching more than `kMaxCellsPerItem` (64) cells, or beyond the coordinates a key holds, goes to an
  oversized list that every query scans. Cells stay once made (empty), so a moving crowd allocates nothing in the steady state.
  `ForEachInRegion(region, exact, visit)` and `ForEachOverlappingPair(visit)` report each result once from one canonical cell (the cell of the lowest
  corner of the overlap, which lies in both ranges), with no per-query marks, so concurrent const queries stay safe. `Traverse`/`TraverseRay` scan the
  items, so every query of `spatial/query.h` works on a grid; box and sphere overlaps and `ForEachPair` have grid overloads that use the cells.
- **`engine/spatial/spatial_set.h` / `.cpp`:** `SpatialSet(SpatialSetConfig)` with `DynamicKind::Bvh` (default) or `Grid`; `AddStatic`, `AddDynamic`,
  `Remove`, `Move` (refused and counted for a static item), `MakeDynamic`, `MakeStatic` (the handle stays), `RebuildStatic` (slot order, so
  deterministic), `Optimize`, `Clear`, `IsLive`, `IsStatic`, `ItemOf`, `Stats()`; its own handles (`SetHandle`). The structures store the slot as their
  key and the visitors map it back to the caller's item. Static items added or made static since the last rebuild wait in a pending list that queries
  scan; tree entries of items removed or made dynamic are skipped by a flag on the slot, so every query is exact at every moment. The composed
  `Traverse`/`TraverseRay` visit the static tree, the pending list, then the dynamic side (the documented order; a ray's shortened `max_t` carries over),
  and `ForEachInRegion` uses the grid's cells for box and sphere overlaps. `query.h` gained the set's and the grid's overloads.
- **Where it differs from the task list:** the grid answers the frustum, ray, sweep and nearest queries by scanning its items (exact, but not faster than
  a scan): a cell walk for an arbitrary node test would report items in several cells more than once without per-query marks; the grid is for overlaps
  and pairs, as the spec chose it. `ForEachPair` on a set is the generic one (an overlap query per item); the pair tracker (Day 243) is the fast path.
- **Tests** (`tests/spatial/spatial_set_test.cpp`, 6 cases): the grid against a flat model through 100,000 random operations (inserts, removes, steps,
  jumps; 1 in 20 boxes oversized, 1 in 20 beyond the cells) with box, sphere, closest-ray and all-pairs queries every 500 operations, and stale handles;
  mixed sets of both kinds through 20,000 operations (static and dynamic adds, removes, moves (refused for static), moves across sides, rebuilds,
  `Optimize`) with every query against brute force every 400; results unchanged across `RebuildStatic` with stale tree entries; two independent sets; the
  documented order; the grid's cost (recorded); and the review's regression test.
- **Runs:** `scripts/test-phase.sh --affected`: `ran 1964 of 3833 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 244 s`, all passed; asan `ran 1602 of 3833 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 159 s`, all passed.
  After the review fix: `spatial_tests` and `spatial_tests_asan` 40 of 40, the gates pass, `ctest -L docs` 7 of 7 (95 samples), `spatial` 185 of 185 in
  the lint, four Windows trees clean.

## Post-implementation code review

`/code-review high` over the day's diff: 1 finding, fixed, with a regression test that failed without the fix (1 and 2 assertion hits where 0 and 1
were expected).

1. **Fixed:** a `SpatialSet` always constructed its `HashGrid` from `grid_cell_size`, so a set using the dynamic tree aborted on a cell size it never
   uses (left at 0, or read from an unset setting). The grid now gets its configured cell only when the set uses a grid. Test: `SpatialSet: a set with a
   dynamic tree does not check the grid's cell size it never uses`.
