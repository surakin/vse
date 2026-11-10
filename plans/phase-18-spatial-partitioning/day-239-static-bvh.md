# Phase 18 / Day 239 — Static BVH: Build & Traverse

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Day 238.

The structure for geometry that does not move: built once, queried a great deal.

## Tasks

- [ ] **`spatial::StaticBvh`** (`engine/spatial/static_bvh.h`): `Build(Span<const Item>)` where `Item { Aabb box; u64 key; u32 layer; }`; top-down build with binned surface-area heuristic (16 bins, a leaf of at most 4 items), nodes in one `Vector` in depth-first order (cache-friendly, 32 bytes a node), the items reordered into leaf order; `Clear()`, `NodeCount()`, `Depth()`, `Bounds()`.
- [ ] **Determinism:** the same items in the same order build the same tree (ties broken by index, never by address or hash order); the tree depends on the items, not on the thread count.
- [ ] **Traversal core:** an explicit-stack traversal that takes a visitor for "node box passes?" and "leaf item passes?", closest child first for rays; no allocation and no recursion; a stack of fixed depth (64) with a counted refusal for a deeper tree (a degenerate input is cut by the build instead).
- [ ] **Tests** (`tests/spatial/static_bvh_test.cpp`): every item is reachable once; the root box encloses all; a box overlap query equals a brute-force scan on 1,000 random scenes (fixed seeds: random boxes, clusters, many identical boxes, a long line, a single item, none); a ray query equals the brute-force closest hit; degenerate inputs (zero-size boxes, boxes sharing a face, 100,000 identical boxes build in bounded depth); the SAH cost of the built tree is within 10 percent of the best of 5 random-restart builds on a sample scene; build of 100k boxes recorded (target under 50 ms); tree quality metric recorded.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "The static BVH".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a static scene builds deterministically into a compact tree whose queries equal a brute-force scan on every test scene.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
