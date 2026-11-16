# Phase 25 / Day 392 — The Character Controller

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 391.

A capsule a player or a creature walks with: slopes, steps, ground, platforms.

## Tasks

- [x] `CharacterController` component (capsule radius and height, step height, maximum slope angle, skin width, gravity scale, `push_dynamic` strength), moved by a requested velocity each step and writing its resulting position to the `Transform`; the body is kinematic so dynamic bodies are pushed, never the reverse.
- [x] Move algorithm: collide and slide with up to a fixed number of iterations using the shape-cast of Day 391, project the remaining motion along the contact planes (crease handling for two planes), a skin width that keeps the capsule from sticking to walls.
- [x] Ground detection: a downward cast, the ground normal and slope angle, `on_ground`, `ground_body`; slope limit (too steep slopes are walls); sticking to ground when walking down slopes within a limit; jumping by an upward velocity with a coyote-time-free base (gameplay timers are not in the engine).
- [x] Steps: step-up by the three-cast method (up, forward, down) for ledges up to `step_height`; step-down.
- [x] Moving platforms: the ground body's velocity at the contact point is added to the character's motion; a kinematic platform that moves into the character pushes it.
- [x] Events: began/ended ground contact and hit-wall callbacks as C-API-friendly data (the event plumbing of Day 393).
- [x] Tests: walking on flat ground, a ramp below and above the limit, stairs of 15 cm steps with a 20 cm step height and not with 10, a capsule in a corridor with a crease corner, a lift moving up and a conveyor moving sideways, pushing a crate with mass 10, not being pushed by one; results independent of the frame rate (the controller runs in the fixed step).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A capsule walks, climbs steps and slopes, rides platforms and pushes crates, in the fixed step and without catching on geometry.

## Outcome

`CharacterController` (`physics/character.h`, `character.cpp`): a kinematic capsule moved once per step by `Move` (before `Step`), which sets the body's velocity to the displacement over `dt`. Collide and slide with the Day 391 shape cast (walls flattened to upright normals, crease handling), a validated step-up (up, forward, down), snap-down, ground probe (dynamic bodies count as ground), gravity and `Jump`, platform velocity, `Stop`, and `CharacterEvents`/`CharacterState` as plain data. 12 test cases (flat walk, fall and jump events, ramps at 30 and 65 degrees, stairs with 20 and 10 cm step heights, crease, lift and conveyor, pushing a crate, 60 versus 120 Hz, bad config, stale body, wall normal, stop, second Create). Guide section "The character controller". Plain ran 3574 of 9582 tests (phases: all; flavors: plain) after the review fixes; before them plain ran 3571 of 9573, asan 2951, tsan 2539, all passing. Built and `[character]` run on 4 trees (clang unity debug, gcc, non-unity, release).
Not done: the ECS `CharacterController` component (Day 393 owns the ECS integration), `push_dynamic` strength (the solver pushes by the kinematic velocity), hit-wall callbacks (data only), a kinematic platform that moves into a standing character pushing it.

## Post-implementation code review

6 findings. Fixed: `wall_normal` overwritten by the step-up probe and the vertical slide (now a scratch normal); stale kinematic velocity when `Move` is not called (added `Stop`, test); a second `Create` leaked the first body (it destroys it, test fails without the fix); the 0.2 landing threshold is a named constant; tests added. Declined: stairs with a tread shorter than the radius are refused by the step-up validation; shortening the probe let a 15 cm ledge be climbed with a 10 cm step height, so the limit stays (KNOWN-ISSUES.md).
