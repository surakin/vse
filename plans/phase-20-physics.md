# Phase 20 — Physics

**Depends on:** Phase 17 (ECS), Phase 18 (spatial partitioning: the broad phase).
**Goal:** rigid body simulation integrated as ECS components/systems.
**Definition of done:** gravity, collision detection, and collision
resolution work correctly for boxes, spheres, and capsules.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Broad-phase collision: **provided by Phase 18** (`engine/spatial`: the dynamic BVH, layer masks and the `PairTracker` with deterministic begin/end events). This phase consumes the pair feed and owns everything after it.
- Narrow-phase collision for box/sphere/capsule primitive pairs (GJK/EPA
  or analytic tests — decide based on primitive set actually needed).
- Rigid body integration (semi-implicit Euler to start; higher-order
  integrators are a later refinement if stability demands it).
- Constraint/contact resolution (impulse-based to start).
- ECS components: `RigidBody`, `Collider`, plus a `PhysicsSystem` that
  reads/writes `Transform`.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 17 is complete.
- Fixed vs. variable timestep for simulation stability — decide with real
  frame-time data from Phase 5/15, not speculatively.
