# Phase 18 / Day 244 — ECS Components & the SpatialWorld Resource

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 228-229, 232, 242; Phase 17 resources and removal tracking.

How entities enter and leave the spatial sets: the ECS half of the module.

## Tasks

- [ ] **Components** (`engine/spatial/components.h`, on the engine's `math` types): `SpatialBounds { Aabb local; }` (local-space box, normally copied from the mesh), `WorldBounds { Aabb box; }` (derived, never set by hand), `SpatialMembership { u8 sets; u8 layer; }` (which sets, which layer), tag `SpatialStatic`, and a hidden `SpatialProxy { u32 id[kMaxSets]; }` (the handles, owned by the sync system).
- [ ] **`SpatialWorld` resource:** holds up to 8 named `SpatialSet`s (`CreateSet(name) -> SetId`), `Set(id)` for queries, `Find(name)`; created with the world through `world.AddResource<SpatialWorld>()`; the well-known set ids `kRenderSet`, `kPhysicsSet`, `kAudioSet`, `kGameplaySet` exist from the start so independent systems agree on them without coordinating.
- [ ] **Lifecycle policies, documented and tested:** an entity with `SpatialBounds`, `WorldTransform` and `SpatialMembership` joins its sets at the next sync; removing any of them leaves the sets; destroying the entity leaves them (through the removal list of Day 228, not by scanning); an entity with a `Parent` uses its world transform; a non-finite bounds is refused and counted; more than 8 sets is refused.
- [ ] **Tests** (`tests/spatial/world_component_test.cpp`): creating entities with the components adds exactly those entities to exactly their sets after a sync; removing a component, destroying an entity and changing membership update the sets (no stale proxy, no leak: the proxy count equals the entity count); a model test of 2,000 random structural changes against a brute-force list; the resource can be created, found and cleared; the `Static` tag routes to the static structure.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "Putting entities in the spatial world"; `docs/guide/ecs.md` gets a pointer.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** entities enter, change and leave the spatial sets through components and a resource, with no stale proxies after any structural change.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
