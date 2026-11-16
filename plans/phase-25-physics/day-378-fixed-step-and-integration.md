# Phase 25 / Day 378 — The Fixed-Step Driver and Integration

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 377.

Time stepping that is the same on every machine, and bodies that fall.

## Tasks

- [ ] `PhysicsWorld::Update(real_dt)`: an accumulator advances in fixed steps (`physics.step_hz`, default 60, a setting through `settings::Registry`), at most `physics.max_substeps` (default 4) per call; a longer frame drops the remainder and counts it in the stats (no spiral of death).
- [ ] `Step()`: apply forces and gravity to velocities, damping, (broad phase, narrow phase, solve are later days), integrate positions with semi-implicit Euler, orientation update by quaternion with normalisation using `Sqrt` only; kinematic bodies move by their target.
- [ ] Interpolated write-back: the `Transform` of each dynamic body is the blend of the last two states by the accumulator fraction, so rendering at any frame rate shows smooth motion; `physics.interpolate` can turn it off; teleports skip interpolation.
- [ ] Gravity: a world vector and a per-body scale; forces and torques accumulate and clear each step; `ApplyImpulse` acts at once.
- [ ] A system `PhysicsStepSystem` in the ECS schedule with declared reads and writes (reads `RigidBody`, `Collider`, `Transform` of kinematic bodies; writes `Transform` of dynamic ones), so the Phase 17 scheduler orders it against other systems.
- [ ] Settings: `physics.step_hz`, `physics.max_substeps`, `physics.gravity`, `physics.interpolate` declared with env and flag names; `config_access_check` stays green.
- [ ] Tests: a sphere in free fall matches the closed form for position within the semi-implicit Euler error bound and exactly matches a reference implementation of the same recurrence; the accumulator (a 100 ms frame runs the capped substeps, the remainder is dropped and counted); interpolation at fraction 0, 0.5, 1; a spinning body keeps a unit quaternion over 10^5 steps; the state hash of a 600-step run is stable across runs and thread counts (committed constant).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Bodies fall under gravity at a fixed rate with smooth interpolated rendering, and a committed state hash for a 600-step run holds on every run.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
