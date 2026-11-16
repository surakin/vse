# Phase 25 / Day 383 — Compound Shapes, Filtering and Triggers

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 382.

Several shapes on one body, finer filtering, and volumes that sense without pushing.

## Tasks

- [x] `CompoundShape`: up to 64 child shapes each with a local transform, material and its own group/mask; the compound's bounds, mass (sum of the children's, by density) and inertia (the parallel-axis theorem) computed once and cached; the compound's centre of mass becomes the body origin and the children are offset, so rotation is about the true centre.
- [x] Broad phase for compounds: the compound has one entry in the dynamic tree, and the narrow phase tests child bounds against the other side's bounds before running a child pair; contacts carry the child index so warm starting and events can tell children apart.
- [x] Filtering: `group`/`mask` words after the layer matrix (a pair collides if `(a.group & b.mask) != 0 && (b.group & a.mask) != 0`); one-way platforms are out of scope and not built; per-pair filter callbacks are not provided (determinism).
- [x] Triggers: a collider flagged `is_trigger` generates begin/end events but no contacts; trigger-versus-trigger pairs are configurable and off by default.
- [x] Changing a collider's shape or a compound's children at run time rebuilds the bounds, mass properties and the tree entry, and clears the affected contact caches.
- [x] Tests: a table made of a box top and four capsule legs has the analytic centre of mass and inertia; a compound dropped on a plane rests on its legs; child indices in manifolds; the group/mask truth table; a trigger reports begin and end as a body passes through and never changes the body's velocity; the shape-change rebuild.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A compound body rests and rotates about its true centre of mass, filtering behaves as the truth table says, and triggers sense without pushing.

## Outcome

`CompoundShape` (up to 64 children with local transforms, materials, group and mask; mass, centre and principal inertia computed once with `PrincipalAxes`), `CompoundLibrary` in `PhysicsWorld::Compounds()`, `ShapeKind::Compound` (the world's `RecomputeMass` puts the body's centre of mass at the compound's), `Collide(..., compounds)` colliding each child (or pair of children) and merging with the shared `MergeContacts` (child indices in the feature ids), `Collider::group/mask` with `GroupsCollide` applied in the broad phase, and trigger pairs (`BroadPhaseConfig::trigger_pairs`, off by default; begin/end events carry the `trigger` flag). Tests: refusals, a table's analytic mass/centre/inertia and a split rotated box equal to the whole box, a table on a floor (four points, distinct child ids, both orders), the group/mask truth table (4096 cases) and the broad phase, a trigger passed by a body (begin and end once, velocity untouched), the shape-change rebuild, and 20,000 random contacts hashing to `33059fc50145b4c9` under clang and GCC, unity and not, Debug and Release.
Runs: plain `ran 3501 of 9363 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2881 of 9363 ...`, tsan `ran 2469 of 9363 ...`, no failure; `ctest -L docs` 8 of 8; docgen lint at zero. Pages touched: `docs/guide/physics.md`, `docs/reference/physics.md`.

## Post-implementation code review

`/code-review high`, 1 finding, declined and recorded: begin and end events are filtered by group and mask at the time they happen, so a mask changed later can leave a pair in `Pairs()` that never had a begin event.
