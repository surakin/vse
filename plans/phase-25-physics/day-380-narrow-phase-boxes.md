# Phase 25 / Day 380 — Narrow Phase II: Box-Box and Capsule-Box

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 379.

The pairs that need clipping, producing stable multi-point manifolds.

## Tasks

- [x] Box-box by the separating axis test: 15 axes (3 + 3 face normals, 9 edge cross products), the axis of least penetration with a small bias favouring faces over edges (so a resting box does not flicker between feature types), edge-edge contact as a single closest-point pair.
- [x] Face contacts by clipping the incident face against the reference face's side planes (Sutherland-Hodgman on a 4-gon) and reducing to at most 4 points that keep the largest area; feature ids from the clipped vertex origin.
- [x] Capsule-box: reduce to segment-box (closest points by clamping and the separating-axis refinement), one or two contact points for a capsule lying on a face.
- [x] Degenerate input: zero-extent boxes are refused at shape validation (Day 375), near-parallel edges handled by the SAT epsilon from the spec, identical boxes (full overlap) return a deterministic axis.
- [x] Tests: a box on a plane's worth of box gives 4 contacts at the corners of the overlap with depth equal to the penetration; a tilted box gives 2 then 1; edge-edge crossing; capsule lying on a box face; capsule standing on an edge; a 10^5-pair random symmetry and separation test as on Day 379; feature ids are equal between two steps of a slowly sliding box.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Boxes and capsules against boxes give stable manifolds of up to four points whose ids persist while a box slides, and the random symmetry and separation tests pass.

## Outcome

`Collide` now covers box-box and capsule-box (`engine/physics/collide_box.cpp`; shared helpers moved to the internal `collide_math.h`, excluded from the docs).
**Box-box:** SAT over 15 axes (parallel edge pairs skipped), a face wins unless an edge axis is better by more than 1e-3 and the first face wins ties within 1e-4; face contacts clip the incident face against the reference face's four side planes (Sutherland-Hodgman), keep the vertices within the margin and reduce more than four to the deepest, the farthest, and the two spanning the most area on either side; ids are (reference box, reference face, incident face, clipped-vertex id); edge contacts are one point from the closest points of the support edges. **Capsule-box:** in the box frame the axis is cut at the slab planes (at most 8 pieces), the distance is minimized in closed form per piece, a nearest point outside gives a sphere-like contact, a flat stretch gives two points, an axis through the box leaves by the face of least penetration of the whole capsule.
Tests: analytic scenes (cube on cube 4 points, shifted, speculative, turned 45 degrees, tilted to 2 then 1 points, edge-edge, identical boxes, small box inside, capsule lying/standing/on an edge/through the box, ids stable while sliding); 50,000 random box pairs against an independent 15-axis oracle (both directions, symmetry of the deepest depth, depth identity); 20,000 random capsule-box pairs against a 400-sample distance oracle; the separation property now covers all nine pairs; the 100,000-pair hash is now `a37013a6ea09b3fe` under clang and GCC, unity and not, Debug and Release.
Runs: plain `ran 3464 of 9182 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 2774 of 9182 ...`, tsan `ran 2432 of 9182 ...`, no failure; `ctest -L docs` 8 of 8; docgen lint at zero. Pages touched: `docs/guide/physics.md`, `docs/reference/physics.md`.

## Post-implementation code review

`/code-review high`, 1 finding, fixed: the feature id of a flat capsule-box contact read the region at the plateau ends, which lie exactly on a slab boundary, so rounding flipped the id from step to step; it is now read at the middle of the stretch, with a regression test (a capsule lying on a face sliding along z keeps both ids). Declined: none. Known limit (recorded in the guide): near a tie between a face and an edge axis the manifold of (A,B) and of (B,A) may pick different features, so the symmetry test compares the deepest depth, not the points.
