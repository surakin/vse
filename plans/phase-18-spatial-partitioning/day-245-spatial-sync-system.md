# Phase 18 / Day 245 — The Spatial Sync System (Incremental, Parallel)

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 230-232, 244.

Keep the sets true with the cost of what changed, in the frame order the scheduler gives.

## Tasks

- [ ] **`SpatialSync` system** (stage after transform propagation, before the render sync): (1) `WorldBounds` = `Transform(local, WorldTransform)` for entities with a `Changed<WorldTransform>` or `Changed<SpatialBounds>` (written only when different, so downstream `Changed` filters stay exact); (2) joins, leaves and moves proxies from the change lists; (3) a static tree rebuild only when its item set changed (never on moves of dynamic items).
- [ ] **Parallelism through the job system:** the `WorldBounds` computation is a parallel chunk loop (Day 231's `ForEachChunkParallel`); the structure updates are applied on one thread in entity order (the trees are not written concurrently, which keeps them deterministic), batched so the sequential part is small; a parallel static build splits the top levels across jobs and produces the same tree as the sequential build (compared byte for byte in a test).
- [ ] **Frame order contract:** a documented order (propagate, bounds, sync, then queries by any later system); a query before the sync sees the previous frame (documented, not an error); `spatial.parallel=false` runs everything sequentially.
- [ ] **Tests** (`tests/spatial/sync_test.cpp`): after a frame the sets equal a brute-force rebuild from the entities (every set, every frame, 200 random frames of moves, spawns, destroys, reparenting); a static scene costs one `Changed` query and no structure calls (counted) over 100 frames; the parallel path equals the sequential one (tree bytes and query results); with a moved parent every child's proxy moved; the removal of a component whose entity moved in the same frame is correct; tsan on the parallel path; the cost of the sync for 100k entities with 1, 10 and 100 percent moving recorded.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (tsan: always, the parallel path); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "The sync system and the frame order".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the sets follow the entities with the cost of what moved, the parallel path equals the sequential one, and a static world is almost free.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
