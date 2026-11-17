# Phase 25 / Day 399 — Joints II: Hinge with Limits and Motors

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 398.

The joint most games use, with its angle limits and drives.

## Tasks

- [x] Hinge (revolute): five rows (three point, two axis alignment); angle measured from the two bodies' reference frames; the angle uses `Atan2` through `lang/scalar.h` (libm, no reimplementation); the use is listed as a non-exact operation in the guide and the hash check of Days 407-411 reports whether it matters.
- [x] Limits: lower and upper angle as one-sided rows that engage at the limit, with softness; motor: target angular velocity with a maximum torque; spring/damper variant by frequency and damping ratio.
- [x] Angle wrapping and the full-turn cases (limits beyond +-pi use an accumulated angle tracked per step, not recomputed from `Atan2` alone).
- [x] Joint frames from authoring data: a joint is described in the frame of body A with the axis and anchor, the world computes body B's local frame at creation (so the pose at creation is the zero angle).
- [x] Tests: a door (hinge to a static frame) swings under gravity and stops at its limits; a motor spins a wheel to the target speed and stalls at the torque cap; a hinge chain (a rope bridge) holds a load; limit stability (a body pushed against a limit for 600 steps does not creep); reaction torque correct for a loaded arm; the full-turn tracking over 10 revolutions.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 410-411, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Hinges swing, stop at limits, are driven by motors and carry loads, tracked correctly through full turns.

## Outcome

`JointType::Hinge` with `MakeHingeJoint(a, b, point, axis)` (frames built so the angle is zero at creation), `JointAngle`, and a joint may now have the world as body A (a hinge to the world takes the world as A so a positive motor speed turns the moving body B the usual way). Rows, in the Day 398 substep loop: angle spring (`hertz`, `damping_ratio`), motor (`enable_motor`, `motor_speed`, `max_motor_torque`, impulse capped by torque times the substep), lower and upper limit rows as one-sided soft rows (speculative before the limit), two alignment rows across the axis (2x2 mass matrix on the error `axis_a x axis_b`), then the point rows. The angle uses `Atan2` through `lang/scalar.h` (twist of frame B relative to frame A about the axis) and is unwrapped against the last step's value in `JointRecord::angle`, so limits beyond plus or minus pi and ten revolutions work; the hinge hash `0xf6f62d959b49c4df` is equal on clang, GCC, non-unity and release (the arc tangent did not matter in this scene). Reaction torque includes the axial impulses. Tests (`tests/physics/hinge_test.cpp`): a door swings and stops at its lower limit (-1.0 within 0.02, never beyond -1.05), a door against a limit does not creep (0.002 over 600 steps), a motor reaches 5 rad/s within 2 % and stalls at its 2 N m cap (omega = T t / I within 3 %), 65 rad over 13 s (10.3 turns) within 1 %, an upper limit of 8 rad (more than a turn) holds, a locked arm carries its weight (3 %) and weight times 0.5 m (5 %), twisting the plank leaves the axes together (z axis above 0.999), an angle spring at 1 Hz oscillates with a period within 3 %, a bridge of 8 hinged planks holds a 40 kg load without moving, and the scene hash. Docs: `docs/guide/physics.md` (hinge paragraph), docgen clean, `ctest -L docs` passes. Runs: plain `ran 3536 of 9468 tests`, asan `ran 2916 of 9468`, tsan `ran 2504 of 9468`.

## Post-implementation code review

`/code-review high`: 3 findings. Fixed: a joint with the world as body A needed the helpers and the connected-pair check to accept a null A (done, covered by the hinge tests). Declined: the alignment rows have no direction when the axes are exactly opposite, and the angle reads the twist about A's axis only, so a swing error above about 45 degrees gives a wrong angle; the alignment rows keep the error small in normal play and only a teleport reaches it (recorded in KNOWN-ISSUES).
