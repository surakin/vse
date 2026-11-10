# Phase 18 / Day 239 — Static BVH: Build & Traverse

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Day 238.

The structure for geometry that does not move: built once, queried a great deal.

## Tasks

- [x] **`spatial::StaticBvh`** (`engine/spatial/static_bvh.h`): `Build(Span<const Item>)` where `Item { Aabb box; u64 key; u32 layer; }`; top-down build with binned surface-area heuristic (16 bins, a leaf of at most 4 items), nodes in one `Vector` in depth-first order (cache-friendly, 32 bytes a node), the items reordered into leaf order; `Clear()`, `NodeCount()`, `Depth()`, `Bounds()`.
- [x] **Determinism:** the same items in the same order build the same tree (ties broken by index, never by address or hash order); the tree depends on the items, not on the thread count.
- [x] **Traversal core:** an explicit-stack traversal that takes a visitor for "node box passes?" and "leaf item passes?", closest child first for rays; no allocation and no recursion; a stack of fixed depth (64) with a counted refusal for a deeper tree (a degenerate input is cut by the build instead).
- [x] **Tests** (`tests/spatial/static_bvh_test.cpp`): every item is reachable once; the root box encloses all; a box overlap query equals a brute-force scan on 1,000 random scenes (fixed seeds: random boxes, clusters, many identical boxes, a long line, a single item, none); a ray query equals the brute-force closest hit; degenerate inputs (zero-size boxes, boxes sharing a face, 100,000 identical boxes build in bounded depth); the SAH cost of the built tree is within 10 percent of the best of 5 random-restart builds on a sample scene; build of 100k boxes recorded (target under 50 ms); tree quality metric recorded.
- [~] **Runs (CLAUDE.md "Tests run by phase"; done on the Windows PC, so tsan, Clang and GCC wait for the Linux machine):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [~] **Two machines (CLAUDE.md "Windows is a supported platform"; done on the Windows PC, the open half is Linux):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "The static BVH".
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a static scene builds deterministically into a compact tree whose queries equal a brute-force scan on every test scene.

## Notes

- **Done on the Windows PC** (2026-11-10); the Linux build (Clang, GCC, unity on and off) and tsan wait for the Linux machine (`KNOWN-ISSUES.md`).
- **`engine/spatial/static_bvh.h` / `.cpp`:** `spatial::Item` (box, `SpatialKey`, layer), `StaticBvh` with `Build(Span<const Item>)`, `Clear`, `NodeCount`,
  `ItemCount`, `Depth`, `Bounds`, `Nodes`, `Items`, `FirstItem(node)` and `SahCost()` (the quality metric), and the two traversals: `Traverse(node_test,
  item_visit)` with a tri-state node test (`NodeVisit::Reject`, `Descend`, `AcceptAll`: a node entirely inside a query hands over its whole item range)
  and `TraverseRay(ray, max_t, item_visit)`, nearest child first, the visitor allowed to shorten the ray; both on a fixed stack of 64 entries
  (`kTraversalStackDepth`), no allocation, no recursion, a counted refusal (`TraversalStats::refused`) for a subtree that would not fit.
- **The node is 32 bytes** (the box and two `u32`): a leaf keeps its first item and its count with a leaf bit; an inner node keeps its right child (the
  left child is the next node) and its subtree's item count; its first item is its leftmost leaf's, found by walking left children (`FirstItem`, at most
  the depth). The library became a static library (`spatial`, `_asan`, `_tsan`).
- **The build:** top-down, binned SAH on the longest centroid axis (16 bins), leaves of at most 4 items, a stable partition (ties keep the items' order),
  so the same items in the same order build the same tree byte for byte (tested). A node whose items share one centroid, or which would push the tree
  past 60 levels (the level plus the halvings its items need), is split in the middle of its range instead, so every tree fits the traversal stack:
  a pure SAH build on geometrically spaced items (each 1.3 times farther) would go one level per item; the test builds 300 of them within the bound.
- **Not done here, deferred:** the parallel build of the spec's open points (section 6: split the recursion over the job scheduler with the sequential
  order of partitions). The day's tasks did not list it, the single-threaded build meets its target, and the tree is the same for any thread count
  because there is one thread. Filed in `KNOWN-ISSUES.md` for Day 245 (the sync system) or Day 248 (benchmarks) to take up if a scene load needs it.
- **Numbers** (100,000 random boxes, 4 to 20 wide, 2 to 40 tall, on a 4000-unit cube): 67,295 nodes, depth 19, SAH cost 169.8, 61.5 bytes per item
  nodes included (limit 64, spec section 4). Build time: 410 ms in the Debug build; in Release (`build-msvc-release`, MSVC 14.51, Ryzen AI 7 350) first
  66 ms, then 39 to 41 ms after the bins were computed once per node (they were computed three times) and boxes grown with plain min and max instead of
  `Union`'s empty checks (target under 50 ms; the tree, its SAH cost and node count unchanged).
- **Tests** (`tests/spatial/static_bvh_test.cpp`, 7 cases): 1,000 random scenes of six kinds (random boxes, clusters, many identical boxes, a long line,
  a single item, none), each with every item reached once (by descending and by `AcceptAll` at the root), the root and every node enclosing their items,
  subtree counts consistent, 8 box queries equal to a brute-force scan and 8 rays whose closest hit equals the brute-force closest; degenerate inputs
  (2,000 points, a 12 x 12 x 12 grid of touching cubes, the geometric line); 100,000 identical boxes in depth 16; determinism (two builds and a rebuild
  byte-equal); the SAH cost within 10 percent of the best of five builds of the same items shuffled; the 100k build recorded; and a box that is not
  finite refused (review, below).
- **Docs:** `docs/guide/spatial.md` "The static BVH" (a runnable sample: a box query and a closest-hit ray), the reference regenerated, `spatial` 38 of
  38 in the lint; the docs samples now link the `spatial` library (`tools/docgen/CMakeLists.txt`).
- **Runs:** `scripts/test-phase.sh --affected` (everything: CMake files changed): `ran 1941 of 3787 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 237 s`, all passed; asan
  `ran 1579 of 3787 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 125 s`, two DX12 tests failed with `D3D12CreateDevice` returning `DXGI_ERROR_DEVICE_REMOVED` at their start (the known
  Phase 13 Day 173 flake, filed again); both passed 3 times in 3 on rerun. After the review fix: `spatial_tests` and `spatial_tests_asan` 17 of 17, the
  gates pass, `ctest -L docs` 7 of 7 (92 samples), four Windows trees clean, the Release build 41 ms.

## Post-implementation code review

`/code-review high` over the day's diff: 1 finding, fixed, with a regression test that failed without the fix (3 of 3 cases).

1. **Fixed:** `Build` checked only that an item's box was not empty. A box with a NaN corner is not "empty" (every comparison is false), the build's box
   growing skips NaN, so such an item sat in a leaf whose box did not contain it: no query could return it and the root no longer enclosed every item;
   an infinite corner made every ancestor's box infinite. `Build` now aborts on a box that is not finite, as it does on an empty one. Test:
   `StaticBvh: Build refuses a box that is not finite` (NaN, plus and minus infinity).
