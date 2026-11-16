# Phase 25 / Day 387 — Joints II: Hinge with Limits and Motors

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 386.

The joint most games use, with its angle limits and drives.

## Tasks

- [ ] Hinge (revolute): five rows (three point, two axis alignment); angle measured from the two bodies' reference frames; the angle uses `Atan2` through `lang/scalar.h` (libm, no reimplementation); the use is listed as a non-exact operation in the guide and the hash check of Days 395-399 reports whether it matters.
- [ ] Limits: lower and upper angle as one-sided rows that engage at the limit, with softness; motor: target angular velocity with a maximum torque; spring/damper variant by frequency and damping ratio.
- [ ] Angle wrapping and the full-turn cases (limits beyond +-pi use an accumulated angle tracked per step, not recomputed from `Atan2` alone).
- [ ] Joint frames from authoring data: a joint is described in the frame of body A with the axis and anchor, the world computes body B's local frame at creation (so the pose at creation is the zero angle).
- [ ] Tests: a door (hinge to a static frame) swings under gravity and stops at its limits; a motor spins a wheel to the target speed and stalls at the torque cap; a hinge chain (a rope bridge) holds a load; limit stability (a body pushed against a limit for 600 steps does not creep); reaction torque correct for a loaded arm; the full-turn tracking over 10 revolutions.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Hinges swing, stop at limits, are driven by motors and carry loads, tracked correctly through full turns.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
