# Phase 25 / Day 383 — Compound Shapes, Filtering and Triggers

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 382.

Several shapes on one body, finer filtering, and volumes that sense without pushing.

## Tasks

- [ ] `CompoundShape`: up to 64 child shapes each with a local transform, material and its own group/mask; the compound's bounds, mass (sum of the children's, by density) and inertia (the parallel-axis theorem) computed once and cached; the compound's centre of mass becomes the body origin and the children are offset, so rotation is about the true centre.
- [ ] Broad phase for compounds: the compound has one entry in the dynamic tree, and the narrow phase tests child bounds against the other side's bounds before running a child pair; contacts carry the child index so warm starting and events can tell children apart.
- [ ] Filtering: `group`/`mask` words after the layer matrix (a pair collides if `(a.group & b.mask) != 0 && (b.group & a.mask) != 0`); one-way platforms are out of scope and not built; per-pair filter callbacks are not provided (determinism).
- [ ] Triggers: a collider flagged `is_trigger` generates begin/end events but no contacts; trigger-versus-trigger pairs are configurable and off by default.
- [ ] Changing a collider's shape or a compound's children at run time rebuilds the bounds, mass properties and the tree entry, and clears the affected contact caches.
- [ ] Tests: a table made of a box top and four capsule legs has the analytic centre of mass and inertia; a compound dropped on a plane rests on its legs; child indices in manifolds; the group/mask truth table; a trigger reports begin and end as a body passes through and never changes the body's velocity; the shape-change rebuild.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A compound body rests and rotates about its true centre of mass, filtering behaves as the truth table says, and triggers sense without pushing.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
