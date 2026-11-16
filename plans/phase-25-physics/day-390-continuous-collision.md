# Phase 25 / Day 390 — Continuous Collision Detection

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 389.

Fast and small bodies no longer tunnel through thin geometry.

## Tasks

- [ ] Time of impact by conservative advancement: advance a pair by `distance / (relative speed bound)` steps until the distance is below a tolerance, using the GJK distance of Day 381 (`Support` interface, so every shape pair is covered), iteration cap and fallback to the end-of-step pose.
- [ ] Which pairs: a body flagged `continuous` (or whose per-step travel exceeds a fraction of its smallest extent, `physics.ccd_threshold`) against static bodies and meshes first, against other dynamic bodies when the option `physics.ccd_dynamic` is on; the swept bounds of Day 377 bring the candidates.
- [ ] Resolution: the body is advanced to the earliest time of impact, its velocity is resolved against that contact by the normal solver on the next step (the substepping alternative is rejected in the spec: a single TOI clamp per body per step); a body clamped repeatedly in one step is stopped with its remaining motion dropped and counted.
- [ ] Mesh targets use the triangle sweep through the BVH (Day 382) with the internal-edge rule so a fast body does not catch on an edge.
- [ ] Tests: a bullet (a small sphere at 500 m/s) does not pass a 1 cm wall, with and without the flag the difference is shown; a fast box against a thin static plane; a car-sized capsule at speed against a mesh fence; a body rolling over a seam between two static boxes is unaffected; CCD off costs nothing (the stats show zero CCD queries); the state hash is thread independent.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** No tested fast body tunnels through thin static geometry, and a world with no continuous bodies pays no CCD cost.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
