# Phase 18 / Day 241 — Queries: Overlap, Rays, Sweeps, Nearest, Pairs

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 239-240.

One query vocabulary over every structure, with layers, filters and a deterministic order.

## Tasks

- [ ] **Query set** as free functions and members on both BVHs (and later the set of Day 242): `Overlap(Aabb|Sphere|Frustum, mask, visitor)`, `Raycast(ray, max_distance, mask, mode)` with `mode` closest / any / all returning `RayHit { u64 key; f32 t; }`, `SweepBox(box, motion, mask)` (a moving box against the tree, first time of impact against the stored boxes), `Nearest(point, k, max_distance, mask)` (best-first with a bounded heap), `ForEachPair(mask_a, mask_b, visitor)` (every pair of items whose boxes overlap, each pair once, `a < b`).
- [ ] **Layers and filters:** an item has a `layer` (0..31) and a query a `mask` (bit per layer); the optional filter is a function pointer plus user data (no `std::function`): `bool filter(u64 key, void* user)`; a query visitor may return `false` to stop.
- [ ] **Order contract (documented, tested):** results of `Overlap`, `Raycast` (all), `Nearest` and `ForEachPair` are delivered sorted by (distance or key) when the caller asks for `Ordered` (a sort of the small result), and in traversal order otherwise (deterministic for a given tree, but not stable across rebuilds).
- [ ] **Tests** (`tests/spatial/query_test.cpp`): each query on each structure equals the brute force on 1,000 random scenes; masks and filters exclude exactly what they should; early stop stops; `ForEachPair` reports each pair once and none missing; a sweep never reports a hit past the true first contact (checked against fine stepping); `Nearest` equals a full sort of distances; queries allocate nothing (allocation counter); a query on an empty structure, with a zero-size box and with NaN input is defined (no hits, counted refusal for NaN).
- [ ] **Concurrency:** queries are `const` and thread-safe between updates; a tsan test runs 8 threads of mixed queries on one built tree and compares every result with the single-threaded one.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (tsan: always, the concurrent-queries test); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "Queries".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** every query kind works the same on both trees, filters and masks are exact, the order is documented, and concurrent readers agree with a single one.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
