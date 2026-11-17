# Phase 25 — Physics

**Status: in progress (day files written 2026-11-16; Day 385 done 2026-11-16: the design spec and six spikes; `CURRENT_PHASE` is 25). Next: Day 386.**

**Depends on:** Phase 17 (ECS), Phase 18 (spatial partitioning: the broad phase), Phase 22 (asset pipeline: hulls, collision meshes and physics materials are minted assets), Phases 23-24 (the C API worlds and surfaces), Phase 19 (storage: snapshots), Phase 11 (settings), Phase 2 (jobs).
**Goal:** rigid body physics integrated as ECS components and systems, deterministic to the bit on Linux, Windows and macOS, usable from a C host.
**Definition of done:** gravity, collision detection and collision resolution work correctly for boxes, spheres, capsules, convex hulls, static triangle meshes and compound shapes; joints (ball, distance, fixed, hinge with limits and motors), sleeping and islands, continuous collision and a character controller work; queries (ray, shape cast, overlap) agree with brute force; the step is identical to the bit for every thread count; the reference scenes' state hashes are equal on the three machines (or the difference is explained in `KNOWN-ISSUES.md`); a C host drives it through `engine_c` (bodies, colliders, forces, joints, events, controller, snapshots).

## Days

385. [Design Spec, Spikes and Phase Setup](phase-25-physics/day-385-design-spec-and-setup.md)
386. [Determinism Foundation: Flags, Operation Rules and the State Hash](phase-25-physics/day-386-determinism-foundation.md)
387. [Shapes, Mass Properties and the Physics Components](phase-25-physics/day-387-shapes-mass-and-components.md)
388. [The Physics World: Body and Collider Storage](phase-25-physics/day-388-physics-world-storage.md)
389. [The Broad Phase on Phase 18's Spatial Module](phase-25-physics/day-389-broad-phase.md)
390. [The Fixed-Step Driver and Integration](phase-25-physics/day-390-fixed-step-and-integration.md)
391. [Narrow Phase I: Spheres, Capsules and Sphere-Box](phase-25-physics/day-391-narrow-phase-primitives.md)
392. [Narrow Phase II: Box-Box and Capsule-Box](phase-25-physics/day-392-narrow-phase-boxes.md)
393. [Convex Hulls, GJK and EPA](phase-25-physics/day-393-convex-hulls-gjk-epa.md)
394. [Static Triangle Mesh Colliders](phase-25-physics/day-394-static-triangle-meshes.md)
395. [Compound Shapes, Filtering and Triggers](phase-25-physics/day-395-compound-shapes-filters-triggers.md)
396. [Contact Solver I: Sequential Impulse](phase-25-physics/day-396-contact-solver.md)
397. [Contact Solver II: Stability, Stacks and Tuning](phase-25-physics/day-397-solver-stability.md)
398. [Joints I: Constraint Framework, Ball, Distance and Fixed](phase-25-physics/day-398-joints-framework-ball-distance-fixed.md)
399. [Joints II: Hinge with Limits and Motors](phase-25-physics/day-399-joints-hinge.md)
400. [Sleeping and Islands](phase-25-physics/day-400-sleeping-and-islands.md)
401. [The Parallel Step on the Job System](phase-25-physics/day-401-parallel-step.md)
402. [Continuous Collision Detection](phase-25-physics/day-402-continuous-collision.md)
403. [Ray Casts, Shape Casts and Overlaps](phase-25-physics/day-403-queries.md)
404. [The Character Controller](phase-25-physics/day-404-character-controller.md)
405. [Events, ECS Integration and Debug Draw](phase-25-physics/day-405-events-ecs-integration-debug-draw.md)
406. [Snapshot, Restore and Replay](phase-25-physics/day-406-snapshot-restore-replay.md)
407. [Determinism II on Linux: Hashes, Threads and Flavors](phase-25-physics/day-407-determinism-linux.md)
408. [C API I: Bodies, Colliders, Forces and Queries](phase-25-physics/day-408-c-api-bodies-colliders-queries.md)
409. [C API II: Joints, Events, the Controller and the Sample](phase-25-physics/day-409-c-api-joints-events-controller-sample.md)
410. [Windows: Build, Tests and the Cross-Machine Hashes](phase-25-physics/day-410-windows-physics.md)
411. [macOS: Build, Tests and the Cross-Machine Hashes](phase-25-physics/day-411-macos-physics.md)
412. [Benchmarks, Stress and the Limits](phase-25-physics/day-412-benchmarks-and-stress.md)
413. [Docs, Closing Runs and the Phase Review](phase-25-physics/day-413-docs-closing-runs-and-phase-review.md)

Order reasoning: determinism rules and the build flags come first (386), because every later day's tests assume them; the data model and the world (387-388) precede the **broad phase** (389), which is Phase 18's module used as is, not a new structure; the fixed step (390) makes bodies fall before any collision exists; narrow phase by cost of the geometry (primitives 391, boxes 392, hulls 393, meshes 394, compounds 395); the contact solver (396-397) before joints (398-399), which share its row type; sleeping (400) and the parallel step (401) need the constraint graph; CCD, queries and the controller (402-404) use the shape-cast machinery in that order; events and debug draw (405) and snapshots (406) read the finished simulation; the Linux hash baseline (407) precedes the C API (408-409) so the API is built on a settled core; the PC and the Mac (410-411) check the hashes; benchmarks (412) and the closing day (413) last. A phase cut after Day 405 would still leave a useful engine-internal physics.

## Decisions fixed by the plan (the Day 385 spec confirms or amends them)

- **One module, `engine/physics`, above `spatial`, `ecs` and `jobs`.** No third-party physics library (the zero-convenience-dependency rule); the broad phase is Phase 18's `DynamicBvh`/`StaticBvh`/`PairTracker`, the narrow phase, solver, joints, CCD, queries and controller are ours (`PLAN.md` assumption #29).
- **Determinism across machines is a requirement** (user, 2026-11-16): fixed step, no fast-math, FMA contraction off, ordered merges, no float reductions in thread order, and the step restricted to `+ - * / Sqrt`. **libm is used as is through the engine's wrappers; no math is reimplemented** (user, 2026-11-16); an unavoidable libm use (the hinge angle) that makes the hashes differ is recorded, not worked around.
- **Shapes:** sphere, box, capsule (analytic and SAT), convex hull (minted, GJK/EPA), static triangle mesh (minted, BVH), compound (several shapes on one body).
- **Solver:** sequential impulse with warm starting, split impulses or soft constraints for penetration; contacts and joints share one row type; islands sleep and solve as parallel jobs.
- **C API:** extends Phase 24's world and component model (`ENGINE_COMPONENT_RIGID_BODY`, `_COLLIDER`, `_JOINT`, `_CHARACTER_CONTROLLER`), table additions at the end, events as polled arrays (no callbacks), snapshots through `engine_buffer`; the first-version struct-size rule of `engine_c.h` is honoured for every grown struct (the Phase 24 review found three violations).
- **Units and frame:** metres, kilograms, seconds, +Y up, the engine's right-handed frame, no conversion at the boundary.

## Notes / open questions (settled in the Day 385 spec)

- Step rate and substep cap (plan: 60 Hz, 4), the solver's penetration scheme (split impulses against soft constraints), the contact manifold form, the fat-box margin.
- Whether a very large island needs graph colouring (plan: no, one job per island, a documented limit).
- How hulls and mesh colliders reach a level imported from glTF (plan: minted by `asset_mint` from the same mesh, a flag on `level_spawn`, Day 409).
- Not in this phase: soft bodies, cloth, fluids, vehicles, ragdoll authoring, GPU physics, one-way platforms, per-pair filter callbacks (determinism), deformable or dynamic mesh colliders.
- The Windows and macOS days (410-411) are the real test of the determinism claim; Day 407 only gives the Linux reference. arm64 macOS compiles but cannot be run.
