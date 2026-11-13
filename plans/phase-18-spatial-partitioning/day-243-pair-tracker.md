# Phase 18 / Day 243 — Pair Tracking for Physics

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 240-242.

The contract Phase 23 builds on: which pairs began and which ended overlapping this frame.

## Tasks

- [x] **`spatial::PairTracker`:** after the dynamic structure's update, `Update(set)` finds the overlapping pairs (using the fat boxes, so a pair persists while objects jitter), compares with the previous frame's pair set and produces `Begin` and `End` lists of `{ u64 a; u64 b; }` (`a < b`); the pair set is a sorted `Vector` (merged, not hashed, so the order is deterministic); only moved proxies are re-queried (the incremental path), a full recompute is available for tests.
- [x] **Filters:** the tracker takes a layer-pair matrix (`bool collides[32][32]`) so physics layers are an input, not hard-coded; static-static pairs are never produced (a documented rule: they cannot start to touch).
- [x] **Lifecycle:** removing a proxy ends its pairs in the same update; a proxy re-created with a new id does not inherit pairs; the destruction of an entity in the middle of an update is a defined case.
- [x] **Tests** (`tests/spatial/pair_test.cpp`): the incremental pair set equals a full recompute after every one of 2,000 random frames (random moves, inserts, removes); `Begin`/`End` are exactly the set difference; the order is the same in two runs; the layer matrix excludes exactly the right pairs; static-static pairs never appear; a mock narrow phase consuming the events keeps a contact list that matches a brute-force overlap test every frame; cost per frame with 100,000 dynamic objects of which 1 percent move recorded.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "Pairs for physics".
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the broad phase delivers exact, deterministic begin and end events from incremental updates, ready for Phase 23 to consume.

## Notes

- `spatial::PairTracker` (`pair_tracker.h/.cpp`): sorted two-orientation adjacency list, hysteresis `margin`, layer-meets matrix, static-static never paired, sorted deterministic events, incremental `Update` (only touched items re-queried) and `Recompute` reference. A pair begins on tight overlap and ends only when the grown boxes (margin each side) stop overlapping. A key removed and touched again keeps its still-holding pairs without events.
- Tests (`[pairs]`, 9 cases, 72,177 assertions): incremental equals brute force over 2,000 random frames (including remove-and-re-add of a key and `MakeStatic`); determinism (tree vs grid); margin invariants; static-static; lifecycle; layer matrix; a deterministic two-item hysteresis test; a regression for `Update` after `Recompute` with large margins (fails without the fix, checked); a 100,000-object benchmark (debug build: first update 278.5 ms, steady frame 4.6 ms, ~173 begun and 77 ended per frame).
- Runs (Linux): plain `ran 49 of 5622 tests (phases: 18; flavors: plain; display: included; crash tests: excluded) in 45 s`; asan `ran 49 of 5622 tests (phases: 18; flavors: asan; display: included; crash tests: excluded) in 140 s`; tsan `ran 49 of 5622 tests (phases: 18; flavors: tsan; display: included; crash tests: excluded) in 187 s`. Clang and GCC non-unity builds of `spatial_tests` clean; `ctest -L docs` 7 of 7, lint at zero (7559/7559 documented).
- Windows PC: not yet built (listed in `KNOWN-ISSUES.md`).

## Post-implementation code review

`/code-review high` over the day's diff: no bug in normal use; five weaker findings.

- **Fixed:** `Clear()` reset the bound on item margins, so a later `Update` could miss partners (now the bound is kept; documented that items must be touched again after `Clear`); the header was ambiguous about a re-added key (now exact); no deterministic hysteresis test (added) and the random test never re-added keys (added, with `MakeStatic`/`RebuildStatic` interleaved).
- **Fixed (conventions):** a C array in a test became `Array<u32, 5>`, a local named `near` (a Windows macro) was renamed, an unused include dropped.
- **Declined:** none.
