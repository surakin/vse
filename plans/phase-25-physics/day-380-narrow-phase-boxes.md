# Phase 25 / Day 380 — Narrow Phase II: Box-Box and Capsule-Box

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 379.

The pairs that need clipping, producing stable multi-point manifolds.

## Tasks

- [ ] Box-box by the separating axis test: 15 axes (3 + 3 face normals, 9 edge cross products), the axis of least penetration with a small bias favouring faces over edges (so a resting box does not flicker between feature types), edge-edge contact as a single closest-point pair.
- [ ] Face contacts by clipping the incident face against the reference face's side planes (Sutherland-Hodgman on a 4-gon) and reducing to at most 4 points that keep the largest area; feature ids from the clipped vertex origin.
- [ ] Capsule-box: reduce to segment-box (closest points by clamping and the separating-axis refinement), one or two contact points for a capsule lying on a face.
- [ ] Degenerate input: zero-extent boxes are refused at shape validation (Day 375), near-parallel edges handled by the SAT epsilon from the spec, identical boxes (full overlap) return a deterministic axis.
- [ ] Tests: a box on a plane's worth of box gives 4 contacts at the corners of the overlap with depth equal to the penetration; a tilted box gives 2 then 1; edge-edge crossing; capsule lying on a box face; capsule standing on an edge; a 10^5-pair random symmetry and separation test as on Day 379; feature ids are equal between two steps of a slowly sliding box.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Boxes and capsules against boxes give stable manifolds of up to four points whose ids persist while a box slides, and the random symmetry and separation tests pass.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
