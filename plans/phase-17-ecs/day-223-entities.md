# Phase 17 / Day 223 — Entity Ids & the Entity Table

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Day 222.

The identity layer: create, validate and destroy ids with no components yet.

## Tasks

- [ ] **`ecs::Entity`** (`engine/ecs/entity.h`): 32-bit index + 32-bit generation, all-zero invalid, `IsValid`, equality, ordering by value, a stable `Hash` for `HashMap`, `ToU64`/`FromU64`; plain data, thread-safe to copy.
- [ ] **`ecs::EntityTable`:** slots with (generation, location); `Allocate() -> Entity` (reuses freed slots LIFO, bumps the generation on free; generation 0 is never issued; wrap-around of a 32-bit generation after 4 billion reuses of one slot skips 0 and is documented); `Free(Entity) -> bool`; `IsAlive(Entity)`; `Location(Entity) -> EntityLocation*`; capacity grows by chunks of slots without invalidating pointers to locations that callers hold only during a call; a hard maximum (2^32 - 2 slots) with a counted refusal.
- [ ] **Thread-safety contract** written in the header: reads (`IsAlive`, `Location`) are safe concurrently with each other; `Allocate`/`Free` are exclusive (the world takes them at sync points or through the command buffer).
- [ ] **Tests** (`tests/ecs/entity_test.cpp`, phase 17): fresh ids are valid and distinct; a freed id is dead and the reused slot gives an id that is not equal to the old one; 1,000,000 create/destroy cycles keep `IsAlive` exact against a reference `HashMap`; invalid and out-of-range ids are not alive; generation wrap behavior with a table whose generation is forced near the maximum through a test hook; `Hash` distribution is sane (no more than 5 percent of buckets overloaded for 100k sequential ids); concurrent `IsAlive` readers under tsan while a single thread holds exclusive no writers (a read-only stress).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors tsan on the concurrent-read test; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md` (new): "Entities".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** entity ids are generation-checked, reuse is exact against a model, limits are counted refusals, and the thread-safety contract is stated and tested.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
