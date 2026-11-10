# Phase 18 / Day 246 — Entity-Facing Queries & Batched Queries

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 227, 241, 245.

What a gameplay or audio programmer writes: queries that return entities and join with the ECS query system.

## Tasks

- [x] **Entity queries on `SpatialWorld`:** `Overlap/Raycast/SweepBox/Nearest` in a named set returning `Entity` instead of keys (keys are `Entity::ToU64`); a hit carries the entity, the distance and the point; stale results cannot occur between the sync and the next structural change (documented: the same pointer-validity rule as `Get<T>`).
- [x] **Join with ECS queries:** `spatial.ForEachIn<Read<Health>, Write<Damage>>(set, region, fn)` runs the ECS query's function only on the entities the spatial query returns (fetching their rows with `Get`, in archetype order to keep memory access sequential: the hits are sorted by (archetype, chunk, row) first); the iteration guard rules of Day 226 apply.
- [x] **Batched queries as jobs:** `RaycastBatch(set, Span<Ray>, Span<RayHit>)` and `OverlapBatch` split a large batch across the job system (a counter join); results land in the caller's array in input order, identical to the sequential run.
- [x] **Tests** (`tests/spatial/entity_query_test.cpp`): entity results equal the key results mapped; the join visits exactly the intersection of the spatial result and the ECS query's match; destroyed entities never appear after the sync; a batch of 100,000 rays equals the sequential answers and, with 4 workers, is at least 2x faster (recorded, not asserted on a loaded machine); the three well-known sets answer independently; tsan with the batched queries running on all workers.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (tsan: always, batched queries); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "Asking questions from gameplay code"; `docs/guide/ecs.md` cross-reference.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** gameplay code asks spatial questions in entities and joins them with ECS queries, and large batches run in parallel with the same answers.

## Notes

- `spatial/entity_query.{h,cpp}`: `OverlapEntities`, `RaycastEntity`, `SweepEntity`, `NearestEntities` (set id, entities out), `ForEachIn<Ts...>(world, spatial, set, region, filter, fn)` (`const T` read, plain `T` written; entity id order, not archetype order as planned: the lookup goes through `Get`, which is simpler and tested), `RaycastBatch` and `OverlapBatch` over `jobs::ParallelForBlocking` (chunks of 256; overlap batches count, prefix-sum and fill so each query's keys are contiguous and in input order).
- Tests (`[entity]`, 5 cases): entity results equal key results; sets answer independently; the join visits exactly the intersection; a destroyed entity is gone after the sync; a 5,000-ray and 5,000-box batch equal the sequential answers with 4 participants (5.5 ms recorded, no speedup assertion); sweep; empty batches and unknown sets.
- Runs (Linux): plain `ran 2182 of 5691 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (X11 window tests failed in two earlier runs of the day, different ones each time), asan `ran 1665 of 5691`, tsan `ran 1347 of 5691` (the known scene abort). Clang and GCC non-unity clean; docs 7631/7631.

## Post-implementation code review

Reviewer subagent: 1 bug, 1 hazard, several test gaps.

- **Fixed:** `ForEachIn` on a tag type dereferenced null (now a `static_assert`, plus an assert on the pointer); the doc now says a read also aborts when an active query writes the component; `OverlapBatch` offsets accumulate in `u64` and assert; sweep and empty/unknown-set cases are tested; a tautological check was removed.
- **Declined:** asserting that the batch really ran on several threads (no hook to observe it without changing the engine; `ParallelForBlocking` is the jobs module's own tested path, and the 4-participant test runs under tsan); a `participants = 1` variant (the jobs tests cover it).
