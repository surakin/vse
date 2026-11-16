# Phase 25 — Physics

**Status: planned (2026-11-16, the user's request; day files written, Days 373-401). `CURRENT_PHASE` is 24 until Day 373.**

**Depends on:** Phase 17 (ECS), Phase 18 (spatial partitioning: the broad phase), Phase 22 (asset pipeline: hulls, collision meshes and physics materials are minted assets), Phases 23-24 (the C API worlds and surfaces), Phase 19 (storage: snapshots), Phase 11 (settings), Phase 2 (jobs).
**Goal:** rigid body physics integrated as ECS components and systems, deterministic to the bit on Linux, Windows and macOS, usable from a C host.
**Definition of done:** gravity, collision detection and collision resolution work correctly for boxes, spheres, capsules, convex hulls, static triangle meshes and compound shapes; joints (ball, distance, fixed, hinge with limits and motors), sleeping and islands, continuous collision and a character controller work; queries (ray, shape cast, overlap) agree with brute force; the step is identical to the bit for every thread count; the reference scenes' state hashes are equal on the three machines (or the difference is explained in `KNOWN-ISSUES.md`); a C host drives it through `engine_c` (bodies, colliders, forces, joints, events, controller, snapshots).

## Days

373. [Design Spec, Spikes and Phase Setup](phase-25-physics/day-373-design-spec-and-setup.md)
374. [Determinism Foundation: Flags, Operation Rules and the State Hash](phase-25-physics/day-374-determinism-foundation.md)
375. [Shapes, Mass Properties and the Physics Components](phase-25-physics/day-375-shapes-mass-and-components.md)
376. [The Physics World: Body and Collider Storage](phase-25-physics/day-376-physics-world-storage.md)
377. [The Broad Phase on Phase 18's Spatial Module](phase-25-physics/day-377-broad-phase.md)
378. [The Fixed-Step Driver and Integration](phase-25-physics/day-378-fixed-step-and-integration.md)
379. [Narrow Phase I: Spheres, Capsules and Sphere-Box](phase-25-physics/day-379-narrow-phase-primitives.md)
380. [Narrow Phase II: Box-Box and Capsule-Box](phase-25-physics/day-380-narrow-phase-boxes.md)
381. [Convex Hulls, GJK and EPA](phase-25-physics/day-381-convex-hulls-gjk-epa.md)
382. [Static Triangle Mesh Colliders](phase-25-physics/day-382-static-triangle-meshes.md)
383. [Compound Shapes, Filtering and Triggers](phase-25-physics/day-383-compound-shapes-filters-triggers.md)
384. [Contact Solver I: Sequential Impulse](phase-25-physics/day-384-contact-solver.md)
385. [Contact Solver II: Stability, Stacks and Tuning](phase-25-physics/day-385-solver-stability.md)
386. [Joints I: Constraint Framework, Ball, Distance and Fixed](phase-25-physics/day-386-joints-framework-ball-distance-fixed.md)
387. [Joints II: Hinge with Limits and Motors](phase-25-physics/day-387-joints-hinge.md)
388. [Sleeping and Islands](phase-25-physics/day-388-sleeping-and-islands.md)
389. [The Parallel Step on the Job System](phase-25-physics/day-389-parallel-step.md)
390. [Continuous Collision Detection](phase-25-physics/day-390-continuous-collision.md)
391. [Ray Casts, Shape Casts and Overlaps](phase-25-physics/day-391-queries.md)
392. [The Character Controller](phase-25-physics/day-392-character-controller.md)
393. [Events, ECS Integration and Debug Draw](phase-25-physics/day-393-events-ecs-integration-debug-draw.md)
394. [Snapshot, Restore and Replay](phase-25-physics/day-394-snapshot-restore-replay.md)
395. [Determinism II on Linux: Hashes, Threads and Flavors](phase-25-physics/day-395-determinism-linux.md)
396. [C API I: Bodies, Colliders, Forces and Queries](phase-25-physics/day-396-c-api-bodies-colliders-queries.md)
397. [C API II: Joints, Events, the Controller and the Sample](phase-25-physics/day-397-c-api-joints-events-controller-sample.md)
398. [Windows: Build, Tests and the Cross-Machine Hashes](phase-25-physics/day-398-windows-physics.md)
399. [macOS: Build, Tests and the Cross-Machine Hashes](phase-25-physics/day-399-macos-physics.md)
400. [Benchmarks, Stress and the Limits](phase-25-physics/day-400-benchmarks-and-stress.md)
401. [Docs, Closing Runs and the Phase Review](phase-25-physics/day-401-docs-closing-runs-and-phase-review.md)

Order reasoning: determinism rules and the build flags come first (374), because every later day's tests assume them; the data model and the world (375-376) precede the **broad phase** (377), which is Phase 18's module used as is, not a new structure; the fixed step (378) makes bodies fall before any collision exists; narrow phase by cost of the geometry (primitives 379, boxes 380, hulls 381, meshes 382, compounds 383); the contact solver (384-385) before joints (386-387), which share its row type; sleeping (388) and the parallel step (389) need the constraint graph; CCD, queries and the controller (390-392) use the shape-cast machinery in that order; events and debug draw (393) and snapshots (394) read the finished simulation; the Linux hash baseline (395) precedes the C API (396-397) so the API is built on a settled core; the PC and the Mac (398-399) check the hashes; benchmarks (400) and the closing day (401) last. A phase cut after Day 393 would still leave a useful engine-internal physics.

## Decisions fixed by the plan (the Day 373 spec confirms or amends them)

- **One module, `engine/physics`, above `spatial`, `ecs` and `jobs`.** No third-party physics library (the zero-convenience-dependency rule); the broad phase is Phase 18's `DynamicBvh`/`StaticBvh`/`PairTracker`, the narrow phase, solver, joints, CCD, queries and controller are ours (`PLAN.md` assumption #29).
- **Determinism across machines is a requirement** (user, 2026-11-16): fixed step, no fast-math, FMA contraction off, ordered merges, no float reductions in thread order, and the step restricted to `+ - * / Sqrt`. **libm is used as is through the engine's wrappers; no math is reimplemented** (user, 2026-11-16); an unavoidable libm use (the hinge angle) that makes the hashes differ is recorded, not worked around.
- **Shapes:** sphere, box, capsule (analytic and SAT), convex hull (minted, GJK/EPA), static triangle mesh (minted, BVH), compound (several shapes on one body).
- **Solver:** sequential impulse with warm starting, split impulses or soft constraints for penetration; contacts and joints share one row type; islands sleep and solve as parallel jobs.
- **C API:** extends Phase 24's world and component model (`ENGINE_COMPONENT_RIGID_BODY`, `_COLLIDER`, `_JOINT`, `_CHARACTER_CONTROLLER`), table additions at the end, events as polled arrays (no callbacks), snapshots through `engine_buffer`; the first-version struct-size rule of `engine_c.h` is honoured for every grown struct (the Phase 24 review found three violations).
- **Units and frame:** metres, kilograms, seconds, +Y up, the engine's right-handed frame, no conversion at the boundary.

## Notes / open questions (settled in the Day 373 spec)

- Step rate and substep cap (plan: 60 Hz, 4), the solver's penetration scheme (split impulses against soft constraints), the contact manifold form, the fat-box margin.
- Whether a very large island needs graph colouring (plan: no, one job per island, a documented limit).
- How hulls and mesh colliders reach a level imported from glTF (plan: minted by `asset_mint` from the same mesh, a flag on `level_spawn`, Day 397).
- Not in this phase: soft bodies, cloth, fluids, vehicles, ragdoll authoring, GPU physics, one-way platforms, per-pair filter callbacks (determinism), deformable or dynamic mesh colliders.
- The Windows and macOS days (398-399) are the real test of the determinism claim; Day 395 only gives the Linux reference. arm64 macOS compiles but cannot be run.
