# Phase 17 / Day 229 — Change Detection

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Days 227-228.

Skip work on data that did not change, which the renderer sync and the hierarchy depend on.

## Tasks

- [x] **World tick:** `World::Tick()` (u64, incremented by `World::Advance()` once per frame by the scheduler); every column of every chunk has `last_written_tick` and `added_tick` (when the chunk's column last received a row or a write).
- [x] **Marking:** `Write<T>` access marks the chunk's column with the current tick when the iteration begins on that chunk (conservative per chunk, documented: a system that writes one entity marks the whole chunk), `Set<T>`/`Add<T>` and migrations mark the destination; `ForEach` with a `Read<T>` marks nothing.
- [x] **`Changed<T>` filter term:** passes chunks whose `last_written_tick` for T is newer than the query's `since_tick` (kept by the query object and advanced when the system finishes; an explicit `since(tick)` form for tests); `Added<T>` passes chunks where T's `added_tick` is newer; the filters combine with other terms and do not change the data arguments.
- [x] **Cost:** marking is one store per column per chunk visited, measured on the toy benchmark of Day 222 (the overhead on a 1M-entity write system recorded here, target under 2 percent).
- [x] **Tests** (`tests/ecs/change_test.cpp`): a write marks only the written column; a chunk not iterated with write access is not marked; `Changed<T>` returns exactly the chunks written since; a second pass in the same tick sees nothing new after `since` advanced; `Added<T>` sees new entities and migrated-in entities once; a model test that applies random writes to random entities and checks that `Changed<T>` visits a superset of the changed entities and never a chunk with none changed; tick wrap-around of a forced-large tick is correct (the comparison is on a 64-bit counter, the test sets it near the maximum).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime or memory code (always for storage days); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "Change detection" (precision, cost).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** `Changed<T>` and `Added<T>` skip chunks that did not change, with conservative per-chunk precision documented and a measured, small overhead.

## Notes

- Chunk stamps are `u64` (`Chunk::written`/`added`, header 576 bytes instead of 320). `World::Tick()` starts at 1; `Advance()`, `SetTick()`. Terms `Changed<T>`/`Added<T>` (`ecs/query.h`); `Query::Since/SetSince/Finish`.
- Stamps: a `Write`/`OptionalWrite` term stamps the chunk when it is visited (after the filter check); `Set` of an existing component and the non-const `World::Get` stamp the column; a row arriving (create, migration) stamps written and added for every column; a row moved into a gap by `Destroy`/migration stamps the gap chunk's columns written.
- `Query::Finish()` sets `since` to the tick and advances the world, so writes made later in the same frame are not lost (the scheduler of Day 231 calls it per system).
- **Deviation:** the 1M-entity overhead measurement was not taken (no release benchmark exists before Day 235, and a debug timing says nothing about 2 percent). Bound: one 8-byte store per written column per chunk, about 250 stores per 1M-row pass. Filed in `KNOWN-ISSUES.md` for Day 235.
- Open: the Windows build and run ("Two machines").
- Runs: `ran 2046 of 5283 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the two known X11 failures), asan `ran 1529 of 5283`, tsan `ran 1211 of 5283`; GCC and Clang non-unity clean; `ctest -L docs` 7 of 7.

## Post-implementation code review

`/code-review high`: 2 findings, both declined with a documented limit. (1) `Finish` called after other jobs wrote in the same tick would miss those writes: documented (call it right after the pass, when no other job writes); the scheduler's sync point guarantees it. (2) `Count()` ignores the change filters: documented; it counts by archetype.
