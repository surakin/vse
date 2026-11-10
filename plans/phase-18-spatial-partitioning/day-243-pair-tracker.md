# Phase 18 / Day 243 — Pair Tracking for Physics

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 240-242.

The contract Phase 20 builds on: which pairs began and which ended overlapping this frame.

## Tasks

- [ ] **`spatial::PairTracker`:** after the dynamic structure's update, `Update(set)` finds the overlapping pairs (using the fat boxes, so a pair persists while objects jitter), compares with the previous frame's pair set and produces `Begin` and `End` lists of `{ u64 a; u64 b; }` (`a < b`); the pair set is a sorted `Vector` (merged, not hashed, so the order is deterministic); only moved proxies are re-queried (the incremental path), a full recompute is available for tests.
- [ ] **Filters:** the tracker takes a layer-pair matrix (`bool collides[32][32]`) so physics layers are an input, not hard-coded; static-static pairs are never produced (a documented rule: they cannot start to touch).
- [ ] **Lifecycle:** removing a proxy ends its pairs in the same update; a proxy re-created with a new id does not inherit pairs; the destruction of an entity in the middle of an update is a defined case.
- [ ] **Tests** (`tests/spatial/pair_test.cpp`): the incremental pair set equals a full recompute after every one of 2,000 random frames (random moves, inserts, removes); `Begin`/`End` are exactly the set difference; the order is the same in two runs; the layer matrix excludes exactly the right pairs; static-static pairs never appear; a mock narrow phase consuming the events keeps a contact list that matches a brute-force overlap test every frame; cost per frame with 100,000 dynamic objects of which 1 percent move recorded.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "Pairs for physics".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the broad phase delivers exact, deterministic begin and end events from incremental updates, ready for Phase 20 to consume.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
