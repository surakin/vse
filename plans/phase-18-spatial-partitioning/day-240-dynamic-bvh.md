# Phase 18 / Day 240 — Dynamic BVH: Insert, Remove, Move

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Day 239.

The structure for what moves: updated every frame at a cost proportional to what actually moved.

## Tasks

- [x] **`spatial::DynamicBvh`** (`engine/spatial/dynamic_bvh.h`): `Insert(box, key, layer) -> ProxyId`, `Remove(proxy)`, `Move(proxy, new_box) -> bool` (returns whether the tree changed), `Query...` with the same traversal core as Day 239; nodes in a pool with a free list (no per-node allocation), proxy ids are stable `u32` handles (generation-checked: a stale id is refused, counted).
- [x] **Fat margins:** leaves store the box expanded by the margin (`spatial.fat_margin`, a **fraction of the item's largest extent, default 0.25**, per structure at creation; Day 237 experiment (b)); `Move` is a no-op while the new box stays inside the stored fat box, and re-inserts otherwise (with the margin applied along the direction of movement too, the Box2D approach, amended by experiment (b)); the tight box is kept beside the fat one so queries can reject on it exactly where the caller wants exactness.
- [x] **Balance:** insertion chooses the sibling by minimum surface-area increase (branch and bound), then refits upward with tree rotations; **an insert-built tree queries 1.8x to 3.3x slower than a rebuilt one (Day 237 experiment (a)), so the tree also has a bulk top-down `Build(items)` (the Day 239 binned SAH, one item per leaf; used on a scene load and as the full rebuild, 24 ms per 100,000) and `Optimize(budget)` rebuilds a fraction of the tree per call (a rotating subtree) or the whole tree when the quality metric has degraded past a threshold the day sets by test;** the quality metric from Day 239 is reported by `Stats()`.
- [x] **Tests** (`tests/spatial/dynamic_bvh_test.cpp`): insert N, queries equal brute force; random insert/remove/move for 100,000 operations against a model (a flat `Vector` of boxes) with the tree's invariants checked (`Validate()`: parent/child links, boxes enclose children, free list intact, no leaf lost) every 500 operations; a stale proxy id is refused; moves inside the fat box change nothing (counted); a scene where everything moves every frame for 1,000 frames keeps the quality metric within 2x of a fresh build (with `Optimize`); allocation: steady-state `Move` and queries allocate nothing; move cost per object recorded.
- [~] **Runs (CLAUDE.md "Tests run by phase"; done on the Windows PC, so tsan, Clang and GCC wait for the Linux machine):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [~] **Two machines (CLAUDE.md "Windows is a supported platform"; done on the Windows PC, the open half is Linux):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "The dynamic BVH".
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** moving objects update the tree incrementally, the tree stays balanced under heavy motion, and every query equals a brute-force scan.

## Notes

- **Done on the Windows PC** (2026-11-10); the Linux build (Clang, GCC, unity on and off) and tsan wait for the Linux machine (`KNOWN-ISSUES.md`).
- **`engine/spatial/dynamic_bvh.h` / `.cpp`** (after Box2D's `b2DynamicTree`): `ProxyId` (24-bit slot, 8-bit generation; `kInvalidProxy`), `Insert`,
  `Remove`, `Move` (returns whether the tree changed), `Build(items, handles)` (top-down, one item per leaf), `Rebuild()` (handles and fat boxes stay),
  `Optimize(budget)`, `Clear`, `IsLive`, `ItemOf`, `FatBox`, `ProxyCount`, `Height`, `Bounds`, `SahCost`, `Stats()` (`DynamicBvhStats`: counts, the
  quality now and after the last full build, moves inside the fat box, re-inserts, rotations, rebuilds, stale handles refused, memory) and `Validate()`;
  the same two traversals as the static tree, the item visitor also given the handle. Nodes (40 bytes) in one pool with a free list; slots (48 bytes)
  with their own free list.
- **Insertion:** the sibling of least total cost by branch and bound (the new parent's area plus every ancestor's growth; a subtree is skipped when the
  leaf's own area plus the growth so far cannot beat the best), then Box2D's AVL rotations on the way up from the new parent. **Removal:** the sibling
  takes the parent's place, then the same refit.
- **Fat boxes:** `fat_margin` is a fraction of the item's largest extent (0.25, the spec's experiment (b)); `predictive` reaches along the last
  displacement times that factor; the tight box is kept in the slot. A move that stays inside the stored fat box changes nothing, unless the stored box
  is no longer inside the fresh fat box grown by four margins (Box2D's "huge box" rule: an item that moved fast and stopped, or shrank).
- **The predictive margin, decided by measurement (the spec's open point):** 10,000 agents (1 x 2 x 1, steps of 0.1) for 60 frames, Release on the
  Windows PC. Straight lines: 0 gives 15.8 % re-inserted per move and 339 ns per move, 2 gives 11.3 % and 255 ns, 4 gives 9.6 % and 221 ns; nodes tested
  per 10-unit query 32.9, 32.9 and 33.1. A new direction every frame: 0 gives 2.6 % and 64 ns, 2 gives 2.3 % and 71 ns, queries 32.4 against 32.5.
  **Default 2** (it pays for straight movers and costs almost nothing for jittering ones); the guide says when to use 0 or 4.
- **Quality upkeep:** `Optimize(budget)` rebuilds one subtree of height at most log2(budget), chosen each call by the bits of a counter (its low bit
  alternates, so calls spread over the tree), and the whole tree when the SAH cost exceeds `rebuild_threshold` times the cost after the last full build.
  **Threshold 1.3, set by the test:** 1,000 items all moving for 1,000 frames with `Optimize(64)` a frame stayed within 1.13 of a fresh build's SAH cost
  by partial rebuilds alone (the test requires 2.0); 1.3 is the net if a scene degrades faster. The binned-SAH split is shared with the static tree
  (`engine/spatial/internal/binned_sah.h`, with `kMaxBuildDepth`; the static tree's output is unchanged: 67,295 nodes, SAH 169.796).
- **Tests** (`tests/spatial/dynamic_bvh_test.cpp`, 10 cases): 2,000 inserts against brute force (box and closest ray, height near log2); a bulk build
  against brute force and better than the insert-built tree, `Rebuild` keeping the handles, a second `Build` replacing everything; 100,000 random
  inserts, removes, small steps, jumps and resizes against a flat model with `Validate()` and queries every 500 operations, `Optimize` and `Rebuild`
  interleaved; stale handles (after removal, after the slot's reuse, after `Clear`, `kInvalidProxy`, a slot never used); a move inside the fat box; the
  1,000-frame quality run; no allocation in a frame of 2,000 re-inserting moves and 100 queries (the `tests/ecs` allocation counter); 128 bytes per item;
  a quiet `Optimize`; the recorded measurement above. The tests link `tests/ecs/alloc_counter.cpp`.
- **Runs:** `scripts/test-phase.sh --affected`: `ran 1950 of 3805 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 238 s`, all passed; asan `ran 1588 of 3805 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 126 s`, all passed.
  After the review fixes: `spatial_tests` and `spatial_tests_asan` 27 of 27, the gates pass, `ctest -L docs` 7 of 7 (93 samples), `spatial` 81 of 81 in
  the lint, four Windows trees clean.

## Post-implementation code review

`/code-review high` over the day's diff: 3 findings, all fixed; the first two have regression tests that failed without the fixes (136 bytes per item;
11 and 12 rebuilds where 1 was expected).

1. **Fixed:** the item slot was 56 bytes (a `bool` after two `u32` padded it to the 8-byte alignment of the key), so 100,000 items took 136 bytes each,
   over the spec's limit of 128 for the dynamic tree (pass condition 6). The live flag now rides in the generation word: 48-byte slots, 128 bytes per
   item. New `DynamicBvhStats::memory_bytes`; test `DynamicBvh: 100,000 items take at most 128 bytes each, tight boxes included`.
2. **Fixed:** `Optimize` measured the whole tree (a pass over every node) and rebuilt a subtree on every call, even when nothing had changed. The sync
   system (Day 245) calls it each frame, and a quiet frame at 100,000 entities may cost about 0.047 ms (pass condition 2): it now returns at once when
   nothing was inserted, removed or re-inserted since its last call. Test `DynamicBvh: Optimize does nothing, and costs no pass over the nodes, when
   nothing changed`.
3. **Fixed (documentation):** `ItemOf` and `FatBox` return references that a later `Insert` (or any change) may move; their comments now say so.
