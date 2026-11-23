# Phase 28 / Day 501 — Hierarchical Instances in the Scene, the C API and Scripts

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 498-500, Phase 24, Phase 27.

Draw trees, and reach them from hosts and scripts.

## Tasks

- [ ] `scene::InstanceHierarchy` component (a tree asset or an in-memory tree handle, the entity transform as the root's parent) and its render sync: the renderer draws it through Day 497 and Day 500; animating a node (a swaying branch) is `SetLocal` from a system, and the dirty-subtree update makes it cost only that subtree.
- [ ] The wind sample: a system that sets the local rotation of branch nodes from a per-node phase and a time, deterministic (a function of tick and node id), as the example and the demo's content.
- [ ] C API: `engine_world_create_instance_set` / `engine_instance_set_add_many` / `set_transform` / `remove` with bulk arrays (a struct of arrays, `struct_size` guarded), `engine_world_create_instance_tree` from a node array (parent indices), `engine_instance_tree_set_local`, destroy; all sized structs and ids, no engine pointers; `abi/engine_api.snapshot` updated.
- [ ] Scripts: `instances.add(set, transform)`, `instances.set(set, id, transform)`, `instances.remove`, `tree.set_local(tree, node, transform)` as `world-write` commands merged at the sync point in order; bulk forms take a table of up to a fixed count per call (the limit in the docs); `docs/reference/script-api.md` regenerated; the determinism contract (the same world for any thread count) is proven by a test that hashes the visible list.
- [ ] Docs: `docs/guide/instancing.md` (the set, the tree, culling, levels of detail, the C API, the costs measured on Day 494 and now); `docs/guide/renderer.md` gets "Instanced meshes"; `docgen lint` zero; `docs/reference` regenerated.
- [ ] Tests: a C host builds a 10,000-instance forest and a tree, renders, checks pixels, edits a branch; a script moves 100 instances and the world hash is equal at 1 to 16 threads; wind animation changes only the swaying subtree's transforms (counter of recomputed nodes).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** Hierarchical instanced meshes are components with animation by dirty subtree, reachable from C and scripts.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
