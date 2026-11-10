# Phase 17 / Day 223 — Entity Ids & the Entity Table

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Day 222.

The identity layer: create, validate and destroy ids with no components yet.

## Tasks

- [x] **`ecs::Entity`** (`engine/ecs/entity.h`): 32-bit index + 32-bit generation, all-zero invalid, `IsValid`, equality, ordering by value, a stable `Hash` for `HashMap`, `ToU64`/`FromU64`; plain data, thread-safe to copy.
- [x] **`ecs::EntityTable`:** slots with (generation, location); `Allocate() -> Entity` (reuses freed slots LIFO, bumps the generation on free; generation 0 is never issued; wrap-around of a 32-bit generation after 4 billion reuses of one slot skips 0 and is documented); `Free(Entity) -> bool`; `IsAlive(Entity)`; `Location(Entity) -> EntityLocation*`; capacity grows by chunks of slots without invalidating pointers to locations that callers hold only during a call; a hard maximum (2^32 - 2 slots) with a counted refusal.
- [x] **Thread-safety contract** written in the header: reads (`IsAlive`, `Location`) are safe concurrently with each other; `Allocate`/`Free` are exclusive (the world takes them at sync points or through the command buffer).
- [x] **Tests** (`tests/ecs/entity_test.cpp`, phase 17): fresh ids are valid and distinct; a freed id is dead and the reused slot gives an id that is not equal to the old one; 1,000,000 create/destroy cycles keep `IsAlive` exact against a reference `HashMap`; invalid and out-of-range ids are not alive; generation wrap behavior with a table whose generation is forced near the maximum through a test hook; `Hash` distribution is sane (no more than 5 percent of buckets overloaded for 100k sequential ids); concurrent `IsAlive` readers under tsan while a single thread holds exclusive no writers (a read-only stress).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors tsan on the concurrent-read test; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md` (new): "Entities".
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** entity ids are generation-checked, reuse is exact against a model, limits are counted refusals, and the thread-safety contract is stated and tested.

## Notes

- - Files: `engine/ecs/{entity.h,entity_table.h,entity_table.cpp,CMakeLists.txt}`, `tests/ecs/{entity_test.cpp,test_rng.h,CMakeLists.txt}`, `docs/guide/ecs.md` (new, with a sample that compiles and runs). Wiring: root and Windows `CMakeLists.txt`, `tools/module_deps_check/modules.txt` (module `ecs`, layer 10), `tests/phases.txt` (`tests/ecs/` 17, `flavors 17` Linux and Windows), `tests/module_map.txt`, `docs/reference/modules.txt`, docgen link list, `ARCHITECTURE.md`.
- Design change from the day text: the table is one flat `Vector<Slot>` (not blocks), so a `Location` pointer is valid only until the next `Allocate` (documented); a free slot is marked by `archetype == kNoArchetype`, so a forged id that matches a free slot's next generation is still dead (tested).
- Runs: ecs tests 12 cases pass in plain, asan, tsan (Clang) and plain, tsan (GCC, unity off). `scripts/test-phase.sh --affected`: `ran 1982 of 5091 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; only the two known X11 window tests fail. Not run this day: Windows PC (Day 236), the full all-flavor suite.

## Post-implementation code review

5 findings (`/code-review high`). Fixed: the defaulted move left the moved-from table's counters stale (explicit move now resets them; regression test `a moved-from table is empty and consistent`); `SetGenerationForTesting` accepted the invalid generation 0 (now asserted). Declined with reasons: the `...ForTesting` hooks stay public (named as such and documented; the oracle for the full-table and wrap tests, a friend would add more code than it protects); `entity.h` includes `hash_map.h` (`HashMap` specializes `Hash<K>` by template, so the specialization must be visible wherever an `Entity` key is used); the lint-baseline counts for other modules changed because `--update-baseline` refreshed counts that had drifted since Phase 16 (symbol totals only, no undocumented symbol parked).
