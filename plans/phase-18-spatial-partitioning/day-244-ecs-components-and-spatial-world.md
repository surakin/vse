# Phase 18 / Day 244 — ECS Components & the SpatialWorld Resource

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 228-229, 232, 242; Phase 17 resources and removal tracking.

How entities enter and leave the spatial sets: the ECS half of the module.

## Tasks

- [x] **Components** (`engine/spatial/components.h`, on the engine's `math` types): `SpatialBounds { Aabb local; }` (local-space box, normally copied from the mesh), `WorldBounds { Aabb box; }` (derived, never set by hand), `SpatialMembership { u8 sets; u8 layer; }` (which sets, which layer), tag `SpatialStatic`, and a hidden `SpatialProxy { u32 id[kMaxSets]; }` (the handles, owned by the sync system).
- [x] **`SpatialWorld` resource:** holds up to 8 named `SpatialSet`s (`CreateSet(name) -> SetId`), `Set(id)` for queries, `Find(name)`; created with the world through `world.AddResource<SpatialWorld>()`; the well-known set ids `kRenderSet`, `kPhysicsSet`, `kAudioSet`, `kGameplaySet` exist from the start so independent systems agree on them without coordinating.
- [x] **Lifecycle policies, documented and tested:** an entity with `SpatialBounds`, `WorldTransform` and `SpatialMembership` joins its sets at the next sync; removing any of them leaves the sets; destroying the entity leaves them (through the removal list of Day 228, not by scanning); an entity with a `Parent` uses its world transform; a non-finite bounds is refused and counted; more than 8 sets is refused.
- [x] **Tests** (`tests/spatial/world_component_test.cpp`): creating entities with the components adds exactly those entities to exactly their sets after a sync; removing a component, destroying an entity and changing membership update the sets (no stale proxy, no leak: the proxy count equals the entity count); a model test of 2,000 random structural changes against a brute-force list; the resource can be created, found and cleared; the `Static` tag routes to the static structure.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "Putting entities in the spatial world"; `docs/guide/ecs.md` gets a pointer.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** entities enter, change and leave the spatial sets through components and a resource, with no stale proxies after any structural change.

## Notes

- Files: `engine/spatial/components.h` (`SpatialBounds`, `WorldBounds`, `SpatialMembership`, tag `SpatialStatic`, hidden `SpatialProxy` with `kTrackRemoval`), `spatial_world.{h,cpp}` (`SpatialWorld`: the four well-known sets and up to four more, `Find`, `NameOf`, `Clear`, `SpatialWorldStats`), `spatial_sync.{h,cpp}` (`MakeSpatialSyncSystem`). `spatial` now links `ecs` (`allow spatial ecs` and `allow spatial jobs` in `modules.txt`).
- The structural half of the sync is here (the lifecycle tests need a sync to run): join, leave, membership/layer/static-tag reconcile. Day 245 adds box updates for moved entities, the parallel `WorldBounds` computation and the static-tree rebuild policy.
- Design as in `render_sync.cpp`: the proxy is added by a command at the sync point, so a `pending_` list removes the items of an entity destroyed before the proxy landed (test `an entity destroyed by a command applied before the proxy` fails without it, checked); a destroyed entity is found through the removal log of `SpatialProxy`, and the handles of a proxy the sync removes itself are invalidated first so the log entry does nothing twice; an entity missing a component is taken out through three `Without<>` queries (`leaving` flag so one entity is handled once).
- Tests (`[world]`, 8 cases, 5,368 assertions): the resource; join into exactly the named sets; static routing and toggling; component removal, destroy and membership/layer change; destroy-in-the-frame-of-join; refused boxes and unknown set bits; a 2,000-step random structural model against a brute-force list; the reaper case. Mutation checks: removing the removal-log handling fails 2 cases, removing the pending cleanup fails 1.
- Runs (Linux, after the review fixes): `scripts/test-phase.sh --affected` plain `ran 2169 of 5652 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 92 s`, asan `ran 1652 of 5652 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, tsan `ran 1334 of 5652 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)` with the one known abort (the entity-built scene render test, KNOWN-ISSUES.md). Clang and GCC non-unity builds of `spatial_tests` clean; `ctest -L docs` 7 of 7, lint at zero.
- Windows PC: not yet built (KNOWN-ISSUES.md).

## Post-implementation code review

`/code-review high` over the day's diff (a reviewer subagent): 10 findings, one of them "no bug" (declarations and queries correct).

- **Fixed:** (1) a box turning bad after the join aborted in `SpatialSet::AddDynamic`; the edit path now checks and counts `refused_bounds`, keeping the old items (test `a box that turns bad after the join`, fails without the check). (2) A full set left the proxy claiming membership; the proxy now records the handles actually held, `refused_full` counts the refusal, and an incomplete reconcile is retried. (3) A double `Leave` (pending copy and removal log) inflated `items_removed`; counting now uses `Remove`'s return value (test: counters balance). (4) `Add<WorldBounds>` could fail on an existing component; now `Set`. (6) `unknown_set_bits` was counted per look; the proxy remembers the bits it acted on, so it counts once (exact assertion). (7) A layer of 32 or more is refused and counted. (8) `is_static` flipped even when a move failed; it only changes when every move succeeded.
- **Documented, not changed:** (5) a set created after entities named its bit does not pick them up until their membership changes (in `components.h` and `SpatialWorldStats`).
- **Declined:** none. (9, test weaknesses: the new tests cover NaN after join, layers, counters and balance; a capacity test was not added because `SpatialSet` has no small capacity to configure.)
