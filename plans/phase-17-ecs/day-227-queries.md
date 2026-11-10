# Phase 17 / Day 227 — Queries & Iteration

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Day 226.

Typed queries over archetypes, cached, with per-entity and per-chunk iteration.

## Tasks

- [x] **Terms:** `Read<T>`, `Write<T>`, `Optional<T>` (the pointer is null when the archetype lacks it), `With<T>`, `Without<T>` (filters, no data); duplicate or contradictory terms (`Read<T>` and `Write<T>` of the same type, `With<T>` and `Without<T>`) are compile errors; at most 16 terms.
- [x] **`ecs::Query<Terms...>`:** built from a world (`world.Query<Write<Position>, Read<Velocity>, Without<Frozen>>()`); stores the matching archetype list and the archetype-version it was computed at; on use, matches only archetypes created since (incremental), so a query held across frames stays cheap; archetypes with zero entities are skipped at iteration time, not removed from the list.
- [x] **Iteration:** `ForEach(fn)` where `fn(Entity, A&, const B&, C*)` takes the terms' arguments in order (entity first when the lambda asks for `Entity`); `ForEachChunk(fn)` where `fn(ChunkView&)` offers `Count()`, `Entities()`, `Span<A> Write<A>()`, `Span<const B> Read<B>()`, `Optional` as an empty span; both mark the iteration guard of Day 226 and release it on exit including early return via a returned `bool` (`false` stops); `Count()` and `IsEmpty()` without iterating.
- [x] **`world.Get`-style random access from inside iteration** is allowed for reads of components not being written in the same query (documented, asserted in debug builds against the declared terms); structural calls are not.
- [x] **Tests** (`tests/ecs/query_test.cpp`): a query matches exactly the archetypes with all `Read/Write/With` and none of `Without`; `Optional` pointers null/non-null; entities created after the query was built are seen (incremental match); order of visited entities is chunk order, stable between runs; `ForEachChunk` spans have the chunk's count and alias the same memory `ForEach` mutates; early stop; contradictory terms fail to compile (negative-compile tests); 1,000 random worlds (fixed seeds) compared with a brute-force filter over all entities; iteration over 1M entities allocates nothing (allocation counter).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime or memory code (always for storage days); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "Queries" with a tagged sample that compiles and runs.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** queries match and iterate exactly the right entities with no allocation, incrementally maintained, in two forms that agree.

## Runs

- Clang unity, `ctest -L docs` 7 of 7; `cmake --build build --target docs` output committed; `docgen lint` at zero new findings.
- `scripts/test-phase.sh --affected` plain: `ran 2021 of 5208 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 142 s`; failures: the two known X11 window tests and one flaky timing benchmark (filed in `KNOWN-ISSUES.md`, passes alone).
- asan: `ran 1504 of 5208 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 58 s`, no failures.
- tsan: `ran 1186 of 5208 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 10 s`, no failures (the allocation-count check does not run in this flavor).
- GCC and Clang non-unity `ecs_tests` build clean (warnings are errors).

## Notes

- Terms are `Read`, `Write`, `Optional` (a `const T*`), `OptionalWrite` (a `T*`; my addition, so an optional column can be written), `With`, `Without`. The plan's `Optional<T>` is read-only; writing an optional column is `OptionalWrite<T>`.
- The query is header-only (`engine/ecs/query.h`); `World::Query<Terms...>()` is declared in `world.h` and defined in `query.h`, so a program includes `ecs/query.h` to use it.
- The "archetype version" of the plan is `World::ArchetypeCount()` (archetypes are never removed), so no new counter is needed.
- Random access inside iteration: `World::BeginIteration(const ComponentSet& writes)` records the written components; `World::Get<T>` of one of them fails an assertion. The write set is restored when a nested iteration ends (the guard keeps the previous set).
- Compile-fail cases: `tests/ecs/compile_fail/query_terms.cpp` (10 cases checked at configure time). Allocation counting: `tests/ecs/alloc_counter.cpp` (not in the tsan flavor, where the sanitizer owns `operator new`).
- Not supported yet: running two queries of one world on different threads (the iteration counter is not synchronized); Day 231 makes it safe.
- The Windows PC has not run this day (the two-machines item stays open until Day 236).

## Post-implementation code review

`/code-review high` over the day's diff: 5 findings, 2 fixed, 3 declined.

- Fixed: the `Query` Thread-safety text promised that two queries of one world could run on different threads, but starting an iteration changes the world's unsynchronized iteration counter and write set; the text now says it is not supported until Day 231.
- Fixed: the `World::Query` doc carried an `Aborts:` label for conditions that are compile errors; it now says they do not compile.
- Declined: the write-set test in `World::Get` costs one bit test on every call; it is what catches a read/write overlap inside a query.
- Declined: `ChunkView::Read` of an `Optional` term whose component could not be registered (registry full at 128 types) asserts instead of returning an empty span; the registry-full case is already a counted refusal elsewhere and this is a corner of a corner.
- Declined: a function returning a non-`bool` value never stops `ForEach`; documented as "returns `bool`", and a lambda returning `int` is a user error the signature check cannot tell from a result to ignore.
