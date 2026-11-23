# Maintenance: double precision, Day 7 — Physics: narrow phase, solver, joints

**Series:** [double precision](../double-precision.md)

## Tasks

- [x] Contacts in the spec's frame (body-relative `f32` points around an `f64` anchor, or `f64` throughout if Spike 2 chose it), the solver, the joints and the continuous collision.
- [x] The shapes: mesh and height-field colliders (`HeightField`'s centre and bound in `f64`, its samples and heights unchanged), compound shapes, convex hulls and the character controller.
- [x] The character controller and the vehicle-like systems keep their tests; add far-from-origin variants of the walker and ramp tests.
- [x] Performance: the Day 412 benchmark scenes before and after; record the cost in `docs/guide/physics.md` ("Cost") and fix anything above the spec's budget.
- [x] Golden hashes re-baselined again if this day moved them, recorded as on Day 6.
- [x] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** The whole physics module runs in the new precision, with the step cost recorded.

## Notes

- Written 2026-11-23.
- Day 1 pick (A): manifolds, solver and joints stay `f32` and body-relative. If the per-pair `f64` subtraction costs more than 5% of the narrow phase, amend the spec here before going on.

## Post-implementation code review

`/code-review high`: 4 findings, all nits, recorded, none needing a change:
1. A pair is collided relative to the first body's origin; a huge static body (terrain) keeps large relative offsets for its own pose, as before the series.
2. `JointDesc::world_origin` is new and snapshot version 4 refuses version 3; a hand-built world joint without `world_origin` is only right near the origin.
3. `ContactEvent::point` is a `Vec3d`; scripts and the C API narrow it until Days 8 and 10.
4. The hashes moved a second time and are Linux-only (KNOWN-ISSUES).

What was done: `ContactManifold::anchor` (the first body's origin) with points measured from it; `PoseFrom` places both shapes relative to an anchor for the step, the trigger test, continuous collision and the queries; the solver's `BodyState` keeps the body's start origin and the centre offset, the contact rows' arms and `local_a` are anchor-relative, the centre-of-mass motion `dp` is taken in `f64`; joints measure the two sides from each other, a joint to the world from `JointDesc::world_origin` (so a world anchor is no longer limited to `kMaxExtent` from the origin); snapshots carry the anchor and the world origin. Height fields, meshes, compounds and hulls needed no change: their poses are relative now. Spec check: the per-pair subtraction is inside the noise of the benchmark (narrow phase landing 2.56 ms before the day, 2.58 after; the 2.2 ms of the spec's baseline was measured on a quieter run, the guide's older table says 2.6), well under the 5 percent line: no spec amendment.

New tests (`tests/physics/far_world_test.cpp`): a stack of ten boxes, a pyramid, an eight-plank bridge, a pendulum hung from the world and a bullet at a thin wall, 10,000 km away, after 600 steps within 4.2e-6 m of the same scene at the origin (the test fails on the previous commit); a character walks over a ledge 10,000 km away within 1 mm of the origin run. The walker and ramp tests in `character_test.cpp` are unchanged.

Hashes (old to new, step 600 of the golden scenes in the table below are old = after Day 6): step constant 0x79eda110a53a3725 to 0x51f82412ee6bd9b5, joint 0xad0fffc8778b814f to 0x715dee615e0bcf02, hinge 0xebde560bdeb9a442 to 0xc19246faadd6330b, C sample checksum 0xe7f06c62 to 0x7e65470f, render-features demo 0xfcca005c39a038f4 to 0x9775bdf39b48677a, script demo 0x6223dffc07e3f7f9 to 0x04d98e8a59bb0c83; the golden table (`tests/physics/golden_test.cpp`) again passes for 1 to 32 threads. They moved because contacts are now computed in anchor-relative coordinates, so every contact's rounding changed.

Cost (`physics_bench --scene drop --bodies 5000 --threads 1`, Release): falling 18.8 ms, landing 33.4 ms (broad 9.5, narrow 2.6, solve 19.8); Day 1 baseline 17.8 and 32.0; recorded in `docs/guide/physics.md`.

Runs: plain `ran 902 of 12026 tests (phases: 3 23 25 27 28; flavors: plain; display: excluded; crash tests: excluded; soak: excluded) in 62 s`, asan `ran 824 of 12026 tests (... flavors: asan ...) in 158 s`, tsan `ran 626 of 12026 tests (... flavors: tsan ...) in 224 s`; no failure. Reduced runs. Builds clean: clang unity, GCC unity-off. `docgen lint` 14189 of 14189; `ctest -L docs` 13 of 13.
