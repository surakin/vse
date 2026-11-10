# Phase 17 / Day 227 — Queries & Iteration

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Day 226.

Typed queries over archetypes, cached, with per-entity and per-chunk iteration.

## Tasks

- [ ] **Terms:** `Read<T>`, `Write<T>`, `Optional<T>` (the pointer is null when the archetype lacks it), `With<T>`, `Without<T>` (filters, no data); duplicate or contradictory terms (`Read<T>` and `Write<T>` of the same type, `With<T>` and `Without<T>`) are compile errors; at most 16 terms.
- [ ] **`ecs::Query<Terms...>`:** built from a world (`world.Query<Write<Position>, Read<Velocity>, Without<Frozen>>()`); stores the matching archetype list and the archetype-version it was computed at; on use, matches only archetypes created since (incremental), so a query held across frames stays cheap; archetypes with zero entities are skipped at iteration time, not removed from the list.
- [ ] **Iteration:** `ForEach(fn)` where `fn(Entity, A&, const B&, C*)` takes the terms' arguments in order (entity first when the lambda asks for `Entity`); `ForEachChunk(fn)` where `fn(ChunkView&)` offers `Count()`, `Entities()`, `Span<A> Write<A>()`, `Span<const B> Read<B>()`, `Optional` as an empty span; both mark the iteration guard of Day 226 and release it on exit including early return via a returned `bool` (`false` stops); `Count()` and `IsEmpty()` without iterating.
- [ ] **`world.Get`-style random access from inside iteration** is allowed for reads of components not being written in the same query (documented, asserted in debug builds against the declared terms); structural calls are not.
- [ ] **Tests** (`tests/ecs/query_test.cpp`): a query matches exactly the archetypes with all `Read/Write/With` and none of `Without`; `Optional` pointers null/non-null; entities created after the query was built are seen (incremental match); order of visited entities is chunk order, stable between runs; `ForEachChunk` spans have the chunk's count and alias the same memory `ForEach` mutates; early stop; contradictory terms fail to compile (negative-compile tests); 1,000 random worlds (fixed seeds) compared with a brute-force filter over all entities; iteration over 1M entities allocates nothing (allocation counter).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime or memory code (always for storage days); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "Queries" with a tagged sample that compiles and runs.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** queries match and iterate exactly the right entities with no allocation, incrementally maintained, in two forms that agree.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
