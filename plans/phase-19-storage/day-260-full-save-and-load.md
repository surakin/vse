# Phase 19 / Day 260 — Saving & Loading a World (Full)

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Days 256-259; Phase 17 (chunk iteration, entity creation), Phase 18 (spatial components).

The first end-to-end path: a selection of entities from a `World` becomes records, and records become entities again, exactly.

## Tasks

- [ ] **Save:** `WorldStorage::Save(world, selection, options)` walks the matching archetypes chunk by chunk and reads component columns through the registry (not per-entity lookups), encodes each registered, non-derived component, and puts one record per entity (a framed container of the entity's components with a content hash) in one store batch, **records sorted by key before they are put** (Day 250 spike (b): 3.9x-6.2x faster inserts); progress and cancellation honoured between chunks; entities without a key are given one (an option) or reported.
- [ ] **Load:** decode and validate every record into a staging area first, then apply to the world in one step, so a corrupt or cancelled load leaves the world unchanged (all or nothing for the world); entities are created in the deterministic order of the spec, grouped by archetype so chunks are filled in order; references resolved in a second pass (Day 258); `Create` fails if a key exists, `Replace` overwrites, `Merge` adds and keeps.
- [ ] **Derived state rebuilt:** `WorldTransform`, `HierarchyDepth`, `WorldBounds` and the spatial proxies are not stored; after a load the transform and spatial systems run once and the world is consistent (checked by the tests below).
- [ ] **Tests** (`tests/storage/world_roundtrip_test.cpp`): random worlds (50 archetypes, 10,000 entities, strings, arrays, hierarchy of depth 5, references) saved and loaded into an empty world are equal component by component, bit-exact (a world-comparison helper in `tests/support/`); the entity order and archetype layout of two loads of the same store are identical; a spatial set rebuilt after load equals a brute-force rebuild from the entities; `Replace` and `Merge` cases; a store that fails at every write leaves the store unchanged (batch) and a truncated or corrupt store leaves the target world unchanged.
- [ ] **Report:** `Report` counts match what was written and read; `NullMissingReferences` counts are exact.
- [ ] **Guide** `docs/guide/storage.md`: saving and loading a world, with a tagged runnable sample.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a world round trips through a store bit-exact, deterministically, and a failed load changes nothing.
