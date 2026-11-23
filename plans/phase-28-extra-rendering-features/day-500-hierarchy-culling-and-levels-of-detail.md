# Phase 28 / Day 500 — Hierarchy Culling and Levels of Detail

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 496-499.

Cull whole subtrees and pick a mesh by distance.

## Tasks

- [ ] Subtree cull: walk the array with the frustum: a node whose subtree bound is outside skips `subtree_size` entries by one jump; a node fully inside emits its whole subtree without further tests; partial nodes recurse by the next index. The walk is parallel by root subtrees, with the outputs concatenated in node order (thread-count independent).
- [ ] Level of detail: a node names up to 4 meshes with switch distances (and a hysteresis margin so a node at the boundary does not flicker); beyond the last distance the node (and with `cull_children_beyond` its whole subtree) is skipped; an inner node can carry a simplified stand-in mesh for its whole subtree (a distant tree drawn as one merged mesh instead of trunk and branches) with a distance at which the stand-in replaces the subtree.
- [ ] The output joins Day 497's path: visible nodes become instance records (world transform, tint, data) grouped by mesh and material into runs, so the draw is one instanced draw per (mesh, material, level), not per node; the grouping is a counting sort with a fixed order.
- [ ] The per-frame work is proportional to the visible nodes, not the tree size: a test with a million-node tree and a camera that sees 1,000 of them stays within the recorded budget (a bench line) and the counters (nodes tested, subtrees skipped, nodes emitted) show it.
- [ ] Tests: a brute-force per-node cull (recursive, no skipping) equals the subtree cull as a set; the LOD choice by distance against a table; the hysteresis (moving back and forth across a boundary changes the level once); the stand-in replaces exactly its subtree (no double drawing, checked by counting emitted nodes); equal at 1 and 8 threads.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** Hierarchies cull by subtree and choose levels by distance, matching a brute-force reference, with work proportional to what is visible.

## Notes

- Large worlds (Day 494 spec): the cull takes the render origin as the instance culler does.

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
