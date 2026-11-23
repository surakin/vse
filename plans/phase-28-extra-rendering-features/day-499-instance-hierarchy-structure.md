# Phase 28 / Day 499 — The Hierarchical Instance Structure

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 495.

A forest of trees with branches, a city of buildings with parts: a tree of instances in its own packed structure (the user's decision).

## Tasks

- [ ] `renderer::InstanceTree`: nodes in the depth-first array Day 494 chose, each with a local transform, a mesh reference (`MeshId` into a set of up to 256 meshes the tree names), a material slot reference, per-node tint and data, the node's own bound and the subtree bound, `subtree_size` and a `depth`; no pointers, one `Vector` per field (structure of arrays) or one array of records, as the spike chose.
- [ ] Building: `InstanceTreeBuilder` (an in-memory builder that adds a node under a parent id and finishes into the packed form; a root list for several trees; refuses a cycle, a parent that is not yet added and more than 2^22 nodes, by status) and the offline form `asset_mint mint-instance-tree` from a source listing nodes with parent indices; deterministic; hostile-input checks like the other blobs (a parent index that is not smaller, a subtree size that leaves the array).
- [ ] World transforms: `UpdateWorldTransforms(tree)` composes parents into children in one forward pass over the array (parents precede children in depth-first order), parallel by independent root subtrees on the job scheduler with a fixed merge, and only for dirty subtrees (a dirty flag per node propagated by a `SetLocal(node, t)` call to the subtree range, one range write because the subtree is contiguous).
- [ ] Bounds: a leaf's bound is its mesh bound transformed; an inner node's subtree bound is the union of its children's, updated bottom-up for dirty subtrees only; exact (a test checks against a recomputation).
- [ ] Queries and edits: `Subtree(node)` as a range, `Parent(node)` by a stored index, `Find(node id)` stable ids across edits (an id map), `Attach/Detach` of a subtree (a move within the array, `memmove` of the range, ids updated) with a documented cost.
- [ ] Tests: a 5-level tree of 100,000 nodes: world transforms equal a recursive reference computed with a separate function; moving a trunk moves exactly its subtree; bounds equal a recomputation after random edits; attach and detach keep the array valid (`subtree_size` consistency check function run after each op); the builder's refusals; the hostile blobs.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** An instance hierarchy is built, edited and transformed in a packed structure, proven against a recursive reference.

## Notes

- Large worlds (Day 494 spec): a tree root stores an `f64` position and every other node an `f32` offset from its parent; world transforms compose in `f64` at the root only; subtree bounds are offsets from the root's origin.

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
