# Phase 25 / Day 384 — Contact Solver I: Sequential Impulse

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 383.

Resolve contacts: bodies stop interpenetrating, slide with friction and bounce with restitution.

## Tasks

- [ ] Contact constraint rows built from manifolds each step: normal row and two friction rows per point, effective mass from inverse mass and inverse inertia, restitution target velocity with a velocity threshold (below it no bounce), friction by a pyramid approximation clamped by the normal impulse.
- [ ] Sequential impulse iterations (`physics.velocity_iterations`, default 8) in a fixed row order (sorted by pair, then point); accumulated-impulse clamping (never apply a negative total normal impulse); kinematic and static bodies have zero inverse mass.
- [ ] Warm starting: the contact cache keyed by `(pair, feature_id)` over the `PairTracker`'s kept pairs carries last step's impulses forward, with a matching distance threshold; the cache is cleared for teleported bodies; the cache is ordered storage, not a hash iterated in table order.
- [ ] Position integration after the velocity solve (semi-implicit) and a first position correction by Baumgarte-style bias velocity (`physics.contact_bias`, default per the spec) with a penetration slop; the better scheme comes on Day 385.
- [ ] Material combine rules from the Day 375 materials; friction and restitution of the pair come from the two colliders' materials.
- [ ] Tests: a ball dropped on a plane bounces to the height restitution says, within 3 %; a box on a slope with friction below and above the critical angle stays or slides, with the sliding acceleration matching the closed form within 3 %; two equal masses collide head-on and exchange velocities (elastic) or stick (inelastic); momentum and energy bookkeeping for isolated pairs; the solver's output is independent of the order in which the pair list was built (shuffled input gives the same state hash).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Bodies collide, bounce, slide and come to rest on closed-form results, and the solve is independent of input order.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
