# Phase 18 — Spatial Partitioning

**Depends on:** Phase 17 (ECS: components, resources, change detection, systems, transforms), Phase 2 (job system), Phase 5 (the renderer's `Aabb`, `Frustum` and `RenderScene`), Phase 10 (containers), Phase 16 (wrappers: all new code uses them).
**Goal:** one generic, ECS-based spatial partitioning system that physics, rendering, audio, gameplay and tools all use, instead of each building its own.
**Definition of done:** static and dynamic bounding-volume hierarchies (and a grid if the measurements justify it) behind one query vocabulary (overlap, ray, sweep, nearest, pairs, with layers and filters); entities join named spatial sets through components and a sync system that costs what moved; the renderer culls through it with output identical to the flat scan and a measured gain; physics gets a deterministic begin/end pair feed; a benchmark meets the pass condition fixed on Day 237.

## Days

237. [Design Spec, Experiments & Phase Setup](phase-18-spatial-partitioning/day-237-design-spec-and-setup.md)
238. [Geometry Primitives (Shared With the Renderer)](phase-18-spatial-partitioning/day-238-geometry-primitives.md)
239. [Static BVH: Build & Traverse](phase-18-spatial-partitioning/day-239-static-bvh.md)
240. [Dynamic BVH: Insert, Remove, Move](phase-18-spatial-partitioning/day-240-dynamic-bvh.md)
241. [Queries: Overlap, Rays, Sweeps, Nearest, Pairs](phase-18-spatial-partitioning/day-241-query-api.md)
242. [Hash Grid & the Spatial Set](phase-18-spatial-partitioning/day-242-hash-grid-and-spatial-set.md)
243. [Pair Tracking for Physics](phase-18-spatial-partitioning/day-243-pair-tracker.md)
244. [ECS Components & the SpatialWorld Resource](phase-18-spatial-partitioning/day-244-ecs-components-and-spatial-world.md)
245. [The Spatial Sync System (Incremental, Parallel)](phase-18-spatial-partitioning/day-245-spatial-sync-system.md)
246. [Entity-Facing Queries & Batched Queries](phase-18-spatial-partitioning/day-246-entity-queries.md)
247. [Renderer Integration: Frustum and Light Culling](phase-18-spatial-partitioning/day-247-renderer-culling.md)
248. [Stats, Debug Draw, Settings, Benchmarks & the Demo](phase-18-spatial-partitioning/day-248-stats-debug-draw-benchmarks-demo.md)
249. [Docs, Windows PC, Closing Full Run & Phase Review](phase-18-spatial-partitioning/day-249-docs-closing-run-and-review.md)

Order reasoning: the shared shapes come first because both the renderer and the new module need them below `ecs`; the static tree before the dynamic one (it is simpler and gives the dynamic tree a reference); queries once both trees exist so one vocabulary is written once; the set and the optional grid after the queries, then the pair tracker, which needs the set; the ECS tier (components, then the sync system, then entity-facing queries) once the structures are exact; the renderer integration after the entity queries because it reuses the sync; stats, benchmarks and the demo last so they measure the finished design. Storage (Phase 19), networking (Phase 20), the asset pipeline (21) and physics (Phase 22) follow; 3D text (Phase 25) needs nothing from this phase, but its text entities become spatial members for free.

**Status: started (2026-11-10): Days 237-242 done (Day 237 on the Linux machine, Days 238-242 on the Windows PC: their Linux build and tsan runs are open in `KNOWN-ISSUES.md`); `CURRENT_PHASE` is 18.** Inserted after the ECS on 2026-11-10 (the user's request); phases 18-30 became 19-31.

## Why a phase of its own

The plan had spatial structures in exactly one place, Phase 22's broad phase ("spatial hashing or a simple BVH to start"), and the renderer culls by scanning a flat list (0.08 us per object). Left alone, physics, the renderer, audio and gameplay would each grow a private structure with its own bugs and its own bounds. One module with exact, tested, deterministic structures, and an ECS tier that keeps them in step with entities, is built once and measured once.

## Key deliverables

- Shared shape types (box, sphere, ray, plane, frustum) below `ecs` and the renderer.
- Static BVH (binned SAH), dynamic BVH (fat margins, rotations, incremental optimization), optional hash grid.
- One query vocabulary with layers, filters and a documented order; thread-safe reads.
- Named spatial sets per purpose (render, physics, audio, gameplay) and a pair tracker for physics.
- ECS components, the `SpatialWorld` resource and an incremental, parallel sync system.
- Entity-facing queries that join with ECS queries, and batched parallel queries.
- Renderer frustum and light culling through the render set, flat scan kept as the reference.
- Stats, debug lines, settings, benchmarks against fixed baselines, a demo.

## Notes / open questions

- The planner's defaults are in the Day 237 task list; experiments there may change the structures kept (the grid) and the margin. The pass-condition numbers are the user's to override.
- Collision shapes and the narrow phase are Phase 22's; this phase deals in boxes and spheres as bounds only.
- Tile-based light lists and GPU-side culling are possible follow-ups (Day 247 records which).
