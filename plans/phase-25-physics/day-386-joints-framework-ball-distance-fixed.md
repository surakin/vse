# Phase 25 / Day 386 — Joints I: Constraint Framework, Ball, Distance and Fixed

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 385.

Connect bodies: the framework that joints share with contacts, and the first three joint types.

## Tasks

- [ ] `Joint` component and `JointDesc` (two bodies or a body and the world, local anchors and axes in each body's frame, breakable force/torque thresholds, `collide_connected` flag default off); joints are created and destroyed through the world and by ECS entities with a `Joint` component naming two entities.
- [ ] One row type for contacts and joints: a `ConstraintRow` with Jacobian entries for the two bodies, bounds, bias and the accumulated impulse; joints emit rows each step and share the solver loop and warm starting of Day 384.
- [ ] Ball (point-to-point, three rows), distance (min/max/spring variants, one row, a soft spring through frequency and damping ratio), fixed (six rows, optionally soft).
- [ ] Breaking: a joint whose accumulated impulse exceeds its threshold is removed and an event is emitted (the event plumbing comes on Day 393; here a stats counter and a callback-free flag).
- [ ] Joint islands: a joint links its two bodies for the island logic of Day 388 (the data structure exists now: the solver's constraint graph).
- [ ] Tests: a pendulum (ball joint to a static anchor) has the analytic period within 2 % for small angles and conserves energy within 1 % over 10 s; a chain of 10 links hangs straight and settles; a distance joint holds its length under load and a spring joint oscillates at the set frequency within 3 %; a fixed joint holds two bodies rigid under a force until the break threshold; joint reaction forces reported by the world match the load.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A pendulum, a hanging chain, springs and a fixed pair behave per the analytic results and share one solver loop with contacts.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
