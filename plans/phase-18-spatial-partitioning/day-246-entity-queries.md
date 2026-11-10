# Phase 18 / Day 246 — Entity-Facing Queries & Batched Queries

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 227, 241, 245.

What a gameplay or audio programmer writes: queries that return entities and join with the ECS query system.

## Tasks

- [ ] **Entity queries on `SpatialWorld`:** `Overlap/Raycast/SweepBox/Nearest` in a named set returning `Entity` instead of keys (keys are `Entity::ToU64`); a hit carries the entity, the distance and the point; stale results cannot occur between the sync and the next structural change (documented: the same pointer-validity rule as `Get<T>`).
- [ ] **Join with ECS queries:** `spatial.ForEachIn<Read<Health>, Write<Damage>>(set, region, fn)` runs the ECS query's function only on the entities the spatial query returns (fetching their rows with `Get`, in archetype order to keep memory access sequential: the hits are sorted by (archetype, chunk, row) first); the iteration guard rules of Day 226 apply.
- [ ] **Batched queries as jobs:** `RaycastBatch(set, Span<Ray>, Span<RayHit>)` and `OverlapBatch` split a large batch across the job system (a counter join); results land in the caller's array in input order, identical to the sequential run.
- [ ] **Tests** (`tests/spatial/entity_query_test.cpp`): entity results equal the key results mapped; the join visits exactly the intersection of the spatial result and the ECS query's match; destroyed entities never appear after the sync; a batch of 100,000 rays equals the sequential answers and, with 4 workers, is at least 2x faster (recorded, not asserted on a loaded machine); the three well-known sets answer independently; tsan with the batched queries running on all workers.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (tsan: always, batched queries); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "Asking questions from gameplay code"; `docs/guide/ecs.md` cross-reference.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** gameplay code asks spatial questions in entities and joins them with ECS queries, and large batches run in parallel with the same answers.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
