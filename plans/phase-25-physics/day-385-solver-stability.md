# Phase 25 / Day 385 — Contact Solver II: Stability, Stacks and Tuning

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 384.

Make resting contact and stacking hold, which is what players see.

## Tasks

- [ ] Replace Baumgarte bias by split impulses or soft constraints per the Day 373 spike (a pseudo-velocity solve that corrects penetration without adding energy); `physics.penetration_slop` and `physics.max_correction` per step.
- [ ] Constraint ordering and iteration count tuning: row order by contact depth or by stack height if the spike showed a benefit (deterministic key), `physics.position_iterations`; relaxation iterations after the main solve.
- [ ] Resting-contact fixes: the speculative margin and the restitution threshold stop micro-bounce, an angular damping floor for slow rotation, contact point reduction and persistence so a box keeps its four points.
- [ ] Tuning scenes as tests: a 10-box stack is stable for 3600 steps (no body moves more than 1 cm and none falls), a 5x5 pyramid of boxes the same, a 20-sphere column, a domino chain that falls in order, a heavy box on light boxes (mass ratio 10:1) holds, a stack of hull-shaped bodies; each prints its settle metrics.
- [ ] Stress cases: thin boxes, fast bodies at the 10:1 size/velocity limit documented as the limit before CCD (Day 390), a body wedged between two static planes does not explode (a velocity cap per body, `physics.max_velocity`).
- [ ] Debug counters: iterations used, max penetration, max impulse, active constraints per step in `PhysicsStats`.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A 10-box stack, a 5x5 pyramid and a 10:1 mass ratio stack hold for an hour of simulated time (3600 steps) within the stated bounds, with the metrics recorded.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
