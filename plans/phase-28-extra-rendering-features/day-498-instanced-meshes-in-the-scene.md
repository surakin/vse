# Phase 28 / Day 498 — Instanced Meshes in the ECS and the Scene

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 497, Phase 17.

`InstancedMesh` as a component.

## Tasks

- [ ] `scene::InstancedMesh` component (mesh asset, material slots like `MeshRenderer`, an `InstanceSetId` or an owned handle, flags): the instances are not entities; a system or a call fills the set; the entity's own transform is the set's parent transform (instances are in the entity's local space), so moving the entity moves a whole forest without touching the set (the matrix is applied once on the GPU or per chunk).
- [ ] `scene::MakeRenderSyncSystem` registers the set with the renderer, updates the parent transform and the visibility flags, removes it on component removal; `Hidden` hides the set.
- [ ] Bulk fill helpers on the C++ side: `InstancedMesh::AddMany(span of transforms)` and a `Scatter(rule)` helper for tests and demos (a seeded, deterministic scatter on a rectangle or on a terrain, ray-casting the height through Day 491's query: the forest on the terrain).
- [ ] Level data: `asset::Level` references an instanced mesh with its instances inline or as a separate minted instance blob (`asset_mint mint-instances` from a raw list of transforms, deterministic, zero-copy loadable); the level version bumps once, with the current loader only.
- [ ] Saving and loading through `storage::WorldStorage` (the one storage interface): the component stores a reference to the instance blob, not the instances, unless they were edited at run time, in which case the edited chunks are stored.
- [ ] Tests: an entity with 5,000 instances renders; moving the entity moves them; add, remove and set through the component; a level with an instance blob loads zero-copy (no per-instance work at load, measured as a counter); save and load restores edits.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** Instanced meshes are components that spawn from levels, edit at run time and save, with the entity transform applied to the whole set.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
