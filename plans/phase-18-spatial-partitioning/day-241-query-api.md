# Phase 18 / Day 241 — Queries: Overlap, Rays, Sweeps, Nearest, Pairs

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 239-240.

One query vocabulary over every structure, with layers, filters and a deterministic order.

## Tasks

- [x] **Query set** as free functions and members on both BVHs (and later the set of Day 242): `Overlap(Aabb|Sphere|Frustum, mask, visitor)`, `Raycast(ray, max_distance, mask, mode)` with `mode` closest / any / all returning `RayHit { u64 key; f32 t; }`, `SweepBox(box, motion, mask)` (a moving box against the tree, first time of impact against the stored boxes), `Nearest(point, k, max_distance, mask)` (best-first with a bounded heap), `ForEachPair(mask_a, mask_b, visitor)` (every pair of items whose boxes overlap, each pair once, `a < b`).
- [x] **Layers and filters:** an item has a `layer` (0..31) and a query a `mask` (bit per layer); the optional filter is a function pointer plus user data (no `std::function`): `bool filter(u64 key, void* user)`; a query visitor may return `false` to stop.
- [x] **Order contract (documented, tested):** results of `Overlap`, `Raycast` (all), `Nearest` and `ForEachPair` are delivered sorted by (distance or key) when the caller asks for `Ordered` (a sort of the small result), and in traversal order otherwise (deterministic for a given tree, but not stable across rebuilds).
- [x] **Tests** (`tests/spatial/query_test.cpp`): each query on each structure equals the brute force on 1,000 random scenes; masks and filters exclude exactly what they should; early stop stops; `ForEachPair` reports each pair once and none missing; a sweep never reports a hit past the true first contact (checked against fine stepping); `Nearest` equals a full sort of distances; queries allocate nothing (allocation counter); a query on an empty structure, with a zero-size box and with NaN input is defined (no hits, counted refusal for NaN).
- [~] **Concurrency (the test runs plain and asan here; its tsan run waits for the Linux machine):** queries are `const` and thread-safe between updates; a tsan test runs 8 threads of mixed queries on one built tree and compares every result with the single-threaded one.
- [~] **Runs (CLAUDE.md "Tests run by phase"; done on the Windows PC, so tsan, Clang and GCC wait for the Linux machine):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (tsan: always, the concurrent-queries test); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [~] **Two machines (CLAUDE.md "Windows is a supported platform"; done on the Windows PC, the open half is Linux):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "Queries".
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** every query kind works the same on both trees, filters and masks are exact, the order is documented, and concurrent readers agree with a single one.

## Notes

- **Done on the Windows PC** (2026-11-10); the Linux build (Clang, GCC, unity on and off) and tsan (which matters here: the 8-thread test) wait for the
  Linux machine (`KNOWN-ISSUES.md`).
- **`engine/spatial/query.h`**: free function templates over any structure with the two traversals (`Traverse`, `TraverseRay`): `Overlap(tree, Aabb |
  Sphere | Frustum, filter, visit)` and `OverlapKeys(..., order, &keys)`; `Raycast(tree, ray, max_t, filter, RayMode::Closest | Any | All, order, &hits)`
  with `RayHit { key, t }`; `SweepBox(tree, box, motion, filter, &hit)` (`t` the fraction of the motion; exact for boxes, a ray from the box's center
  against each box grown by its half extents); `Nearest(tree, point, k, max_distance, filter, &out)` (`NearestHit { key, distance }`, nearest first);
  `ForEachPair(tree, mask_a, mask_b, filter, visit)` and `PairKeys(...)` (each pair once, smaller key first). `QueryFilter { mask; filter; user }` (a
  layer is a bit index 0..31; the filter is a function pointer and a user pointer), `Order::Traversal | Ordered`, `QueryStats` (nodes, items, results,
  stopped, `refused_input`). Internal helpers in `engine/spatial/internal/query_walk.h` (adapters from each tree's visitor, input checks).
- **Where it differs from the task list, and why:** the queries are free functions only, not also members of each tree: one generic set (written
  once over the two traversals the trees already share) serves both trees and the spatial set of Day 242 without copies. The filter is a
  `QueryFilter` (mask plus function and user pointer) rather than a bare mask, so a query takes one argument for both. `Nearest` is a depth-first search
  pruned by the k-th best distance so far rather than a best-first search with a node heap: the trees' traversal core is depth first and exposes no
  node priority queue; the results are the same (it is tested against a full sort), with up to `k` steps per candidate (for small `k`). Ties
  everywhere go to the smaller key, so `Ordered` results are a total order.
- **Bad input:** a NaN in a box, a sphere's center or radius, a ray, a sweep, a point or a distance gives no results and `refused_input`; a ray, sweep
  or point that is infinite too; an infinite query box is allowed (it finds everything). An empty box, a negative radius or distance finds nothing.
- **Tests** (`tests/spatial/query_test.cpp`, 7 cases): 1,000 random scenes (up to 160 items, 4 layers, keys that are not indices), each query on both
  trees against a brute-force scan with a random filter (all, one layer, two layers, a key filter): box, sphere and frustum overlaps, rays in all three
  modes, sweeps, nearest (with and without a distance limit) and pairs with random masks; masks, filters, layer 40 and early stops; pairs once each, in
  order, and between two groups; 300 sweeps against fine stepping (2,000 steps: never later than the first sampled contact, touching at the reported
  time) and a sweep that does not move; empty trees, point queries (a point on a shared face finds both boxes), an infinite box, the empty box, NaN
  in each kind of input, an unbounded ray, a negative limit; no allocation with warm result vectors (the allocation counter); 8 threads x 5 rounds of
  200 mixed queries on one static and one dynamic tree, each compared with its single-threaded checksum. A shared test header
  `tests/spatial/spatial_test_util.h` holds `BoxAt` and `SortKeys` (unity builds merge the test files).
- **Runs:** `scripts/test-phase.sh --affected`: `ran 1959 of 3823 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 238 s`, all passed; asan `ran 1597 of 3823 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 126 s`, all passed.
  The clang-cl unity build then failed on an unused function: `SameKeys` in `query_test.cpp` had a non-const twin in `static_bvh_test.cpp`, and merged
  into one file every call bound to the twin; renamed `SameSortedKeys`, all four trees clean. After the review fixes: `spatial_tests` and
  `spatial_tests_asan` 34 of 34, the gates pass, `ctest -L docs` 7 of 7 (94 samples), `spatial` 118 of 118 in the lint.

## Post-implementation code review

`/code-review high` over the day's diff: 2 findings, both fixed (documentation; no regression test: nothing changes at run time).

1. **Fixed:** the frustum `Overlap`'s comment said a node entirely inside the frustum hands over its items "without testing them"; the node tests below
   it are skipped, but each item's box is still tested. The comment now says so.
2. **Fixed:** `Nearest` keeps its results sorted by insertion (up to `k` steps per candidate, quadratic for a large `k`), and its comment did not say it
   is meant for a small `k`. The comment and the guide now do, and point to an overlap query plus a sort for many results.
