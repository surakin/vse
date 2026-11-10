# Phase 17 / Day 226 — The World: Create, Destroy, Add, Remove & Migration

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Day 225.

The user-facing structural API, with archetype migration and a validator.

## Tasks

- [ ] **`ecs::World`** (`engine/ecs/world.h`): `Create() -> Entity` (empty archetype), `Create<A, B, C>(a, b, c)`, `CreateMany(prototype ArchetypeBuilder, count, fn(index, ColumnWriter&))` (bulk path: one chunk fill without per-entity lookups), `Destroy(Entity) -> bool`, `Exists`, `Add<T>(e, value)`, `Remove<T>(e)`, `Has<T>(e)`, `Get<T>(e) -> T*` (null if absent, valid until the next structural change; documented), `Set<T>(e, value)` (adds or overwrites); each returns a status that distinguishes a dead entity from a missing component.
- [ ] **Migration:** `Add`/`Remove` move the row to the target archetype (copy/move columns that exist in both, destroy the dropped component, construct the new one), fix up the entity table for the moved row and for the row swapped into the hole; **archetype graph** edges `(archetype, component, add|remove) -> archetype` cached so repeated transitions cost one lookup; a transition to the same set is a no-op; archetypes are found by `ComponentSet` hash in an `HashMap`, created on first use, and counted.
- [ ] **Iteration guard:** a counter of active iterations; a structural call while it is nonzero hits an `ENGINE_ASSERT` (`Aborts:`) naming the call; `World::IsIterating()`.
- [ ] **`World::Validate() -> ValidationReport`:** a debug/tests-only invariant checker: every alive entity's location points at a row holding that entity; no row's entity is dead or duplicated; chunk counts add up; every archetype's chunks are filled front to back; columns of tags have zero size; used by every randomized test from here on and by `ecs.stats` in debug builds.
- [ ] **Tests** (`tests/ecs/world_test.cpp`): create/destroy/exists; add/remove/set/get with trivial, non-trivial and tag components; the result of `Get` after a migration of the same entity and of a neighbor (row swapped in) is correct; the archetype graph is reused (counter of archetypes stays bounded after 100k random add/remove over 8 component types: at most 2^8 archetypes); `CreateMany` creates the same state as the loop version (compared through `Validate` and value reads); dead-entity and missing-component statuses; the iteration guard death test (`tests/support` death-test helper); a model-based randomized test (fixed seeds, 5 seeds, 100k operations: create, destroy, add, remove, set over 6 types against a reference model `HashMap<Entity, HashMap<ComponentId, value>>`) with `Validate` every 500 operations, under asan.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime or memory code (always for storage days); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the ECS is portable code; it is built and tested on the Windows PC no later than Day 236, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/ecs.md`: "The world" (structural changes, pointer validity, cost table of add/remove).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the world's structural API is correct against a reference model, migration is cached and exact, misuse during iteration is caught, and an invariant validator backs every later test.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
