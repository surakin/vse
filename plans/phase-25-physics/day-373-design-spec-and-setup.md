# Phase 25 / Day 373 — Design Spec, Spikes and Phase Setup

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Phase 24 closed on Linux; Phases 17, 18, 22.

Decide the open questions with numbers before any physics code, and start the phase.

## Tasks

- [ ] Write `docs/superpowers/specs/2026-11-NN-physics-design.md` (the date of the day): the module layout (`engine/physics`, its layer in `ARCHITECTURE.md` above `spatial`, `ecs` and `jobs`, below nothing but the C API and games), the component set (`RigidBody`, `Collider`, `PhysicsMaterial`, `Joint`), the world resource, the step order (integrate velocities, broad phase, narrow phase, solve, integrate positions, write back, events), and what is deliberately not built (soft bodies, cloth, fluids, vehicles, ragdoll authoring, GPU physics).
- [ ] Spike 1, solver: sequential impulse with warm starting against a position-based alternative on one stacking scene (a 10-box stack and a 5x5 pyramid): record jitter, penetration and cost per step; the spec names the winner (the plan assumes sequential impulse) and the numbers.
- [ ] Spike 2, determinism: one program steps 2000 spheres and boxes under gravity for 600 steps twice with different job-thread counts and prints a hash of every body's state; run it with the FMA contraction flags on and off and with `-O0` against `-O2`; record which differences appear and which rules make the hash equal (fixed step, `-ffp-contract=off`, fixed operation order, ordered merges).
- [ ] Spike 3, cross-machine plan: list every operation of a step that is not IEEE-exact (`Sqrt` is, `Sin`/`Cos`/`Acos` are not); the rule is to keep the step to `+ - * / Sqrt` and use libm only where unavoidable, through the engine's `lang/scalar.h` wrappers, with no reimplemented math (user decision, 2026-11-16); list the unavoidable uses (hinge limit angles, setup) and say how the hash check treats them.
- [ ] Spike 4, contact shape: choose the manifold form (up to 4 points, a normal, per-point depth and a feature id for warm starting) against a single-point-per-frame alternative; a box resting on a box must hold without sinking over 600 steps with either; the spec records why the 4-point form wins or loses.
- [ ] Spike 5, step size: sweep 30, 60, 120 Hz on the stack scene; record the stable range and the default (the plan assumes 60 Hz fixed with an accumulator, a cap of 4 substeps per frame and interpolated write-back).
- [ ] Spike 6, broad-phase fit: feed 5000 moving bodies through `spatial::DynamicBvh` and `spatial::PairTracker` (Phase 18) and measure the pair feed against the narrow phase's cost; the spec states the fat-box margin and the layer matrix, and whether the static world uses `StaticBvh` (the plan assumes yes).
- [ ] Decide and record: units (metres, kilograms, seconds, +Y up, the engine's right-handed frame, no conversion at the C boundary), the default gravity, the sleeping thresholds' form, CCD's scope (against static and dynamic, conservative advancement), what a hull and a mesh collider are as minted assets, what the C API exposes (Days 396-397), the state hash definition.
- [ ] Set `CURRENT_PHASE` to 25 and update `tests/phases.txt` conventions for phase 25; a new `engine/physics/CMakeLists.txt` placeholder is NOT added yet (Day 375 adds the module); `PLAN.md` assumption #29 is checked against the spec and amended if the numbers disagree.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The spec is committed with the six spike results and every decision above stated once, `CURRENT_PHASE` is 25, and a reader can start Day 374 from the spec alone.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
