# Phase 25 / Day 379 — Narrow Phase I: Spheres, Capsules and Sphere-Box

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 378.

Contact generation for the pairs that have closed forms, with one manifold type for everything.

## Tasks

- [ ] `ContactManifold`: normal (from A to B), up to 4 points each with position on A, position on B, penetration depth and a `feature_id` (stable across steps for warm starting), plus the pair's combined friction and restitution.
- [ ] Analytic tests: sphere-sphere, sphere-capsule, capsule-capsule (closest points of two segments, parallel and degenerate cases handled without division by a near-zero), sphere-box (closest point on the box, the centre-inside case picks the least-penetration face).
- [ ] Each test returns a manifold or none, is symmetric (swapping the arguments flips the normal and the ids consistently), and uses only `+ - * / Sqrt`.
- [ ] A dispatch table `Collide(shapeA, xformA, shapeB, xformB, manifold*)` keyed by the shape pair, with a documented slot for every pair of shapes (unimplemented ones return `Unsupported` until their day).
- [ ] Speculative margin: contacts are generated up to a small distance (`physics.contact_margin`) so a resting contact is stable and an approaching one is predicted.
- [ ] Tests: per pair, analytic scenes (touching, penetrating by known depths, separated, concentric, parallel capsules, capsule end-to-end, sphere inside box) with the expected normal and depth within 1e-5; a randomised symmetry test (10^5 pairs, `Collide(A,B)` equals the flipped `Collide(B,A)`); a property test that moving B by the returned depth along the normal separates the pair.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Every pair of sphere, capsule and (sphere, box) generates correct, symmetric manifolds, verified against closed forms and a separation property over 10^5 random pairs.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
