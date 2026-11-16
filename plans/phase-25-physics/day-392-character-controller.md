# Phase 25 / Day 392 — The Character Controller

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 391.

A capsule a player or a creature walks with: slopes, steps, ground, platforms.

## Tasks

- [ ] `CharacterController` component (capsule radius and height, step height, maximum slope angle, skin width, gravity scale, `push_dynamic` strength), moved by a requested velocity each step and writing its resulting position to the `Transform`; the body is kinematic so dynamic bodies are pushed, never the reverse.
- [ ] Move algorithm: collide and slide with up to a fixed number of iterations using the shape-cast of Day 391, project the remaining motion along the contact planes (crease handling for two planes), a skin width that keeps the capsule from sticking to walls.
- [ ] Ground detection: a downward cast, the ground normal and slope angle, `on_ground`, `ground_body`; slope limit (too steep slopes are walls); sticking to ground when walking down slopes within a limit; jumping by an upward velocity with a coyote-time-free base (gameplay timers are not in the engine).
- [ ] Steps: step-up by the three-cast method (up, forward, down) for ledges up to `step_height`; step-down.
- [ ] Moving platforms: the ground body's velocity at the contact point is added to the character's motion; a kinematic platform that moves into the character pushes it.
- [ ] Events: began/ended ground contact and hit-wall callbacks as C-API-friendly data (the event plumbing of Day 393).
- [ ] Tests: walking on flat ground, a ramp below and above the limit, stairs of 15 cm steps with a 20 cm step height and not with 10, a capsule in a corridor with a crease corner, a lift moving up and a conveyor moving sideways, pushing a crate with mass 10, not being pushed by one; results independent of the frame rate (the controller runs in the fixed step).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A capsule walks, climbs steps and slopes, rides platforms and pushes crates, in the fixed step and without catching on geometry.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
