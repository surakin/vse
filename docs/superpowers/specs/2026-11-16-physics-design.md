# Physics design (Phase 25, Day 373, 2026-11-16)

Rigid body physics as ECS components and systems, deterministic to the bit on Linux, Windows and macOS x86-64, usable from a C host. This spec fixes the
open questions of `plans/phase-25-physics.md` with numbers from six spikes (appendix at the end; sources in `2026-11-16-physics-spikes/`). The plan's
defaults held in every case; where a spike moved a number, it says so.

## 1. Scope and layout

- **One module, `engine/physics`**, layer above `spatial`, `ecs` and `jobs` (and `storage` for snapshots, Day 394); below nothing but `engine_c` and games.
  No third-party physics library. Added on Day 375 (`ARCHITECTURE.md`, `modules.txt`, `tests/module_map.txt`, `BUILD.md` in that commit).
- **Components** (`engine/physics/components.h`, registered like the spatial ones): `RigidBody` (type static/kinematic/dynamic, mass or density, damping,
  flags `ccd`, `lock_rotation`, `can_sleep`), `Collider` (a shape, a local transform, a material, a layer and a mask, a trigger flag), `PhysicsMaterial`
  (friction, restitution, density; held inline in `Collider`, with an optional minted resource for sharing), `Joint` (kind, the two bodies, the
  anchors and axes in each body's frame, limits, motor, break force). A compound body is one `RigidBody` and several `Collider`s on child entities.
- **`PhysicsWorld`** is an ECS resource: body and collider storage as structure of arrays with stable ids (Day 376), the spatial set and pair tracker (Day
  377), the contact cache, the joint list, the island graph, the event arrays and the settings.
- **Step order** (one fixed step): (1) integrate velocities (gravity, forces, damping); (2) update the broad phase (`Move`/`Touch` for bodies that
  moved; `RebuildStatic` if a static changed) and read the pair events; (3) narrow phase on the sorted pair list, giving manifolds; (4) build the islands,
  warm start from the contact cache; (5) solve velocity iterations (contacts and joints, one row type); (6) integrate positions and orientations
  (quaternion `q += 0.5 dt w q`, normalised with `Sqrt`; no transcendentals); (7) CCD for flagged bodies; (8) sleeping update; (9) write back to `Transform`
  (interpolated for display), events (begin/stay/end contact, trigger enter/exit) and the state hash on demand.
- **Not built:** soft bodies, cloth, fluids, vehicles, ragdoll authoring, GPU physics, one-way platforms, per-pair filter callbacks (they would break
  determinism), deformable or dynamic mesh colliders.

## 2. Decisions

| Question | Decision | Evidence |
|---|---|---|
| Solver | **Sequential impulse with warm starting**, 10 velocity iterations by default; contacts and joints share one row type. Penetration by Baumgarte bias with a slop of 0.005 m (beta 0.2); Day 385 may add soft constraints if its tuning scenes ask for it. | Spike 1 |
| Contact manifold | **Up to 4 points**, a normal, a depth per point and a **feature id** per point; the warm-start key is (pair, feature id). | Spike 4 |
| Step | **60 Hz fixed**, accumulator, **at most 4 substeps** per frame, write-back interpolated between the last two steps. `physics.step_hz` (30 to 240) and `physics.max_substeps` are settings. | Spike 5 |
| Broad phase | `spatial::SpatialSet` (a `DynamicBvh` for moving bodies with the Phase 18 defaults: fat margin 0.25, predictive 2.0; a `StaticBvh` for static colliders) and one `PairTracker` with **margin 0.1** and the layer matrix of the physics layers. **`RebuildStatic()` runs before the pair update whenever a static was added, removed or moved**, never per frame otherwise. | Spike 6 |
| Units and frame | Metres, kilograms, seconds, **+Y up**, the engine's right-handed frame, radians; no conversion at the C boundary. Default gravity `(0, -9.81, 0)`. | |
| Sleeping | A body is *resting* when `|v| < 0.05 m/s` and `|w| < 0.05 rad/s` (thresholds are settings); an island sleeps after **0.5 s** with every body resting; a touching awake body, a joint to an awake body, a moved collider or an applied force wakes it. | Day 388 tunes |
| CCD | **Conservative advancement** against static and dynamic bodies, for bodies flagged `ccd` and, automatically, a body whose step displacement exceeds half its smallest extent; speculative contacts are not used. | Day 390 |
| Hulls and meshes | **Minted assets** (`asset_mint mint-hull`, `mint-collision-mesh`): a hull is at most 64 vertices with face planes and an edge list, built by a deterministic quickhull; a collision mesh is a triangle list with a baked `StaticBvh`. A collider names them by resource id. Mesh colliders are static only. | Days 381, 382 |
| Shapes | Sphere, box, capsule (analytic and SAT), convex hull (GJK for distance, EPA for depth), static triangle mesh, compound. | Plan |
| C API | `ENGINE_COMPONENT_RIGID_BODY`, `_COLLIDER`, `_JOINT`, `_CHARACTER_CONTROLLER` through the Phase 24 component calls; table functions for forces, impulses, queries, events (polled arrays, no callbacks), snapshots (`engine_buffer`); every grown struct honours the first-version struct-size rule (Days 396-397). | Plan |
| State hash | FNV-1a 64 over, for every body in id order: position (3), orientation (4), linear velocity (3), angular velocity (3) as their `u32` bit patterns with **-0.0 written as +0.0**, then a sleep byte; a NaN anywhere is an `ENGINE_ENSURE` failure before it is hashed. Padding and pointers are never hashed. | Spike 2 |

## 3. Determinism rules

The step may use only `+ - * / Sqrt` on `f32`, comparisons, min, max, abs, negation and integer arithmetic: the operations IEEE 754 defines exactly (spike
3). Everything else follows:

1. **Fixed step.** The step takes a constant `dt`; the accumulator lives outside the step.
2. **No FMA contraction, no fast-math.** `-ffp-contract=off` on GCC, Clang and clang-cl, MSVC `/fp:precise` with contraction off, never `-ffast-math`,
   `/fp:fast`, `-Ofast`, `-freciprocal-math`; no `rsqrt`/`rcp` estimate instructions or intrinsics (their results differ between CPUs). Day 374 adds the
   flags to the physics targets and a test that fails when a build turns them on.
3. **Ordered everything.** Pairs come from the `PairTracker` sorted by key; the narrow phase writes into per-chunk arrays merged in chunk order; islands are
   ordered by their lowest body id and solved in that order inside each island; a parallel job writes only its own output; no float reduction in thread
   order, no atomic float add, no hash-map iteration order, no sort on a float without an id tie-break.
4. **No libm in the step.** Transcendentals appear only at setup (authored angles turned into sines and cosines, a slope limit into its cosine). Soft
   constraint coefficients and the quaternion update use `+ - * / Sqrt` only.
5. **libm is used as is** through the `lang/scalar.h` wrappers; nothing is reimplemented (the user, 2026-11-16). The reference scenes carry their setup
   constants as literals, so their hashes must be equal on the three machines exactly. One extra scene builds a hinge from degrees through `Sin`/`Cos`/
   `Atan2`; if its hash differs between machines, that is recorded in `KNOWN-ISSUES.md` with the machines and the first differing step, not worked
   around.
6. **arm64** (macOS) compiles and is not run (no machine): its hashes are unchecked, recorded in `KNOWN-ISSUES.md`. Clang contracts to FMA by default on
   arm64, so the flag of rule 2 matters there most.

## 4. Spike results

Throwaway programs (compiled with `g++ -O2`, one core of an x86-64 machine with FMA (32 hardware threads); sources in `2026-11-16-physics-spikes/`). **Limits of the spikes**, stated
so the numbers are not over-read: spikes 1 and 5 use translation-only unit cubes with vertical contact normals (no rotation), so they compare solvers on
the stacking chain only; spike 4 uses real rotation but a plane; spike 2 uses spheres (box and hull contacts are tested by the determinism checks of Days
380-395 under the same rules); spike 6 runs the real Phase 18 code in the Release tree.

### Spike 1: solver, 10-box stack and 5x5 pyramid (55 boxes), 60 Hz, 10 s

| Solver | Iterations | Stack: max depth (m) | Stack: jitter (mean \|v\|, m/s, last 5 s) | Stack: top drift (m) | Stack: ns/step | Pyramid: max depth (m) | Pyramid: jitter | Pyramid: ns/step |
|---|---|---|---|---|---|---|---|---|
| Sequential impulse, warm | 4 | 0.0079 | 0 | 0.020 | 410 | 0.00048 | 0 | 12,306 |
| Sequential impulse, warm | 10 | 0.0029 | 0 | 0.011 | 840 | 0.00001 | 0 | 14,248 |
| Sequential impulse, warm | 20 | 0.0011 | 0 | 0.005 | 1,134 | 0 | 0 | 18,189 |
| Sequential impulse, cold | 10 | 0.0167 | 0.00001 | 0.100 | 737 | | | |
| Position based | 4 | 0.0279 | 0.127 | 0.051 | 495 | 0.0026 | 0.014 | 22,504 |
| Position based | 10 | 0.0098 | 0.045 | 0.021 | 1,037 | 0.00023 | 0.0020 | 48,913 |
| Position based | 20 | 0.0045 | 0.022 | 0.0097 | 1,905 | 0.00001 | 0.00007 | 92,195 |

(The pyramid's cost is dominated by the spike's brute-force pair search, so only the solver rows' ratios mean anything there.) Warm-started sequential
impulse holds the stack with **zero jitter** at 4 iterations, sinks 3.4 to 4 times less than position-based at the same iteration count on the stack, and costs 20 to 40 percent less;
position-based jitters by 2 to 13 cm/s at the same settings, because the velocity is rebuilt from positions. Dropping the warm start costs 6 times the
penetration and 9 times the drift (the contact cache and feature ids of Days 384-385 are not optional). **Winner: sequential impulse with warm starting,
10 iterations** (the 20-iteration row is the budget for tall stacks on request, `physics.solver_iterations`). The plan's assumption holds.

### Spike 4: contact manifold, a unit cube dropped tilted 10 degrees on a plane, 60 Hz, 10 s

| Manifold | Iterations | Max depth (m) | Jitter (\|v\| + 0.1\|w\|) | Rest tilt | Cost (ns/step) |
|---|---|---|---|---|---|
| 1 point (the deepest corner) | 4 | 0.0435 | 0.094 | 0.21 deg | 265 |
| 1 point | 10 | 0.0501 | 0.144 | 0.43 deg | 437 |
| 1 point | 20 | 0.0501 | 0.140 | 0.34 deg | 760 |
| 4 points (every penetrating corner) | 4 | 0.0282 | 0 | 0.82 deg | 663 |
| 4 points | 10 | 0.0242 | 0 | 0.93 deg | 1,383 |
| 4 points | 20 | 0.0241 | 0 | 0.93 deg | 2,586 |

The single point never settles (the cube rocks on its deepest corner: jitter 0.09 to 0.14 for the whole 10 s, it reaches 4 to 5 cm of depth on landing and rests 1.6 cm too low); the four-point
manifold settles at once (jitter exactly 0) and rests within the slop (the 0.9 degree rest tilt is the 0.005 m slop over a 1 m cube; the depth column is
the landing impact). **Winner: up to 4 points with feature ids**, at 2.5 to 3.4 times the cost of one point per contact, which the stability buys many times
over. The plan's assumption holds.

### Spike 5: step size, same 10 s of simulated time, sequential impulse, 10 iterations

| Rate | Stack: max depth | Stack: top drift | Pyramid: max depth | Stack: ns per step |
|---|---|---|---|---|
| 30 Hz | 0.0104 | 0.027 | 0.00003 | 678 |
| 60 Hz | 0.0029 | 0.011 | 0.00001 | 681 |
| 120 Hz | 0.0007 | 0.0028 | 0 | 674 |

Penetration falls about four times for each doubling of the rate (it scales with the square of the step), all three are stable with zero jitter, and the cost per
step does not depend on the rate, so 120 Hz doubles the cost per second for four times less sinking. Trading iterations for rate: 120 Hz with 4 iterations
(0.0022 m; 2 x 406 ns per simulated 1/60 s) costs about the same as 60 Hz with 10 iterations (0.0029 m; 840 ns) and sinks 24 percent less on this scene; 30 Hz with 20 iterations (0.0046 m, 1,118 ns) is worse on both. **Default
60 Hz with 10 iterations; the stable range is 30 to 120 Hz**; the accumulator caps at 4 substeps per frame (a frame longer than 4 steps drops the rest
and counts it). Day 385 revisits the pair (rate, iterations) on the tuning scenes with rotation.

### Spike 2: determinism, 2000 spheres (radius 0.3 to 0.5), 600 steps, SI with friction, grid broad phase, parallel contact generation

Hash (FNV-1a over every position, velocity and angular velocity) of the final state:

| Build | Hash |
|---|---|
| GCC 16 `-O0 -ffp-contract=off` | `89fa37aeca86e653` |
| GCC 16 `-O2 -ffp-contract=off` | `89fa37aeca86e653` |
| GCC 16 `-O3 -ffp-contract=off` | `89fa37aeca86e653` |
| Clang 23 `-O0 -ffp-contract=off` | `89fa37aeca86e653` |
| Clang 23 `-O2 -ffp-contract=off` | `89fa37aeca86e653` |
| GCC `-O2` (no flag, baseline x86-64: no FMA instructions exist) | `89fa37aeca86e653` |
| GCC `-O2 -march=native -ffp-contract=off` | `89fa37aeca86e653` |
| Clang `-O2 -march=native -ffp-contract=off` | `89fa37aeca86e653` |
| GCC `-O2 -march=native` (default contraction: FMA) | `ea2fbf1793111c8b` **different** |
| Clang `-O2 -march=native` (default contraction on: FMA) | `2e405d53dd7f9ba2` **different, and not equal to GCC's** |
| GCC `-O2 -ffp-contract=off -ffast-math` | `d8edd5e0dae65d5a` **different** |

Thread counts with the ordered merge (chunk order): 1, 2, 4, 16 and 32 threads all give `89fa37aeca86e653`. With the contact lists merged in completion order (an
unordered merge under a mutex), nine runs gave nine different hashes (2, 4 and 16 threads, three runs each). So the rules that make the hash equal are
exactly: **contraction off, no fast-math, ordered merges**; optimisation level, compiler (GCC against Clang) and thread count do not matter once those hold. The
step used `+ - * / Sqrt` only. Without the flag, a build for a machine with FMA (`-march=native`, and on arm64 by default) breaks the hash, and the two
compilers break it differently.

### Spike 3: operations of a step that are not IEEE-exact

Exact (correctly rounded by the standard, the same on every IEEE machine): `+ - * /`, `Sqrt`, comparisons, min/max, abs, negation, conversions between integers
and floats within range, `Floor`/`Ceil`/truncation. **Not exact, differ between libm implementations (glibc, the Microsoft CRT, Apple's libm), and so
forbidden in the step:** `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan`, `Atan2`, `Exp`, `Log`, `Pow`, `Cbrt`, and the hardware estimate instructions `rsqrt`/`rcp`.
Where a step seems to need one, the formulation avoids it: the quaternion update is first order plus a normalising `Sqrt`; the soft-constraint coefficients are
closed forms of the stiffness and damping (division and multiplication); the hinge limit compares the half-angle sine taken from the relative quaternion, not an
angle.
**Unavoidable uses, all at setup, not in the step:** converting an authored angle (a hinge limit in degrees, a motor's target angle, a character slope limit) to its
sine, cosine or half-angle sine (`Sin`, `Cos`), building a quaternion from an axis and an angle (`Sin`, `Cos`), and reporting an angle to the host (`Atan2`).
Policy (rule 5 of section 3): the reference scenes use literal constants for these, the one libm scene is hashed separately and its disagreement, if any, is recorded.

### Spike 6: the pair feed on 5000 moving boxes and 2000 static boxes (Release, one thread)

`PairTracker::Update` per frame, 60 frames, each moving box displaced by up to 0.05 per axis (about 3 m/s):

| Tracker margin | Tree fat margin | All 5000 moving: ms | Pairs | Begun per frame | 20 percent moving: ms |
|---|---|---|---|---|---|
| 0 | 0.1 | 3.87 | 6,621 | 316 | 0.87 |
| 0 | 0.25 | 4.38 | 6,621 | 316 | 0.96 |
| 0.1 | 0.1 | 4.69 | 8,720 | 88 | 1.03 |
| 0.1 | 0.25 | 5.30 | 8,720 | 88 | 1.14 |
| 0.2 | 0.1 | 5.68 | 10,591 | 31 | 1.22 |
| 0.2 | 0.25 | 6.44 | 10,591 | 31 | 1.35 |

A margin of 0.1 cuts the begin/end churn from 316 to 88 pairs a frame for 20 percent more time (0.2 cuts it to 31 for 47 percent more); the narrow phase pays per
pair that begins or ends (a manifold is made or dropped), so **0.1 is the default**. The tree's fat margin barely matters at this speed; keep the Phase 18
default (0.25). At 5000 moving bodies the feed takes 4.7 ms single-threaded, against roughly 8,700 pairs times an estimated 0.1 to 0.3 microseconds per box pair
for the narrow phase (about 1 to 3 ms): the feed is the larger cost when everything moves, and **`PairTracker::Update` is single-threaded** (a Phase 18
property). The expected mitigation is sleeping (20 percent moving costs 1 ms) and, if Day 400's benchmark shows the feed as the limit, parallelising the
tracker's gather as a Phase 18 extension (a decision for that day, recorded as a risk here). **The first run measured 31 ms because `RebuildStatic()` was missing:
the 2000 statics sat in the pending list and every query scanned them (78 percent of the time in `Overlaps`); the world must rebuild the static tree after any static
change, and a test (Day 377) must fail when it does not.** The static world uses `StaticBvh` (the plan's assumption holds).

## 5. Changes to the plan

- `plans/phase-25-physics.md` and the day files hold as written. Added: Day 377 gets the `RebuildStatic` test; Day 374's flag test must cover clang-cl and arm64 notes;
  Day 400 benchmarks the pair feed with the 4.7 ms figure above as its reference and names parallelising `PairTracker::Update` as the option.
- `PLAN.md` assumption #29 agrees with this spec and is unchanged except for the state hash definition and the 4-point manifold, added in the same commit.
