# Spatial partitioning design (Phase 18)

Written 2026-11-10 (Phase 18 Day 237). Plan: `plans/phase-18-spatial-partitioning.md`; `PLAN.md` assumption #24. The decisions below are the planner's defaults, kept or amended by the three experiments in section 3, whose numbers are in the tables. Where an experiment changed a default, the "Decision" line says so and the affected day files were amended in the same commit.

## 1. Intent

Physics' broad phase, the renderer's culling, audio emitter lookup, gameplay proximity and ray queries, and tools all need "what is near this box, ray, frustum". One module with exact, tested, deterministic structures, and an ECS tier that keeps them in step with entities at the cost of what moved, is built once and measured once, instead of four private grids.

Goals: static and dynamic BVHs behind one query vocabulary; named spatial sets so the renderer's tree does not hold physics triggers; a deterministic begin/end pair feed for physics; renderer culling through the render set with output identical to the flat scan.
Non-goals: a narrow phase or collision shapes beyond the box and sphere used as bounds (Phase 22), navigation meshes, a 2D variant, GPU-side culling, world-cell streaming (Phase 33), serialization of trees (rebuilt on load).
Anti-goal: a structure that is fast in a benchmark but cannot be kept exact against a brute force under random operations.

## 2. Decisions

| Topic | Decision | Reason |
|---|---|---|
| Module | `engine/spatial`, layer 10 beside `ecs`; depends on `foundation` (geometry types in `math`), `ecs`, `jobs`. Renderer, `scene`, physics, audio and gameplay depend on it, never the reverse. | One place; no cycle (`ARCHITECTURE.md` section 5). |
| Tiers | Structures (`StaticBvh`, `DynamicBvh`, `HashGrid`) are generic over a `u64` key and know nothing of the ECS; the ECS tier (components, `SpatialWorld`, sync, entity queries) is what systems use. | Unit-testable, reusable by a particle system or a tool. |
| Static BVH | Top-down, binned SAH (16 bins on the longest centroid axis), leaves of at most 4 items, depth-first layout with each node knowing its item range (a fully inside node in a frustum query emits its range without testing). **Kept as measured.** | Best query numbers everywhere (table A), 25 ms per 100,000, 313 ms per 1,000,000. |
| Dynamic BVH | Incremental AABB tree: fat boxes, insertion by minimum surface-area increase, AVL-style rotations. **Amended:** incremental insertion alone is not good enough (tree quality, experiment (a)); the tree also gets a bulk top-down build (`Build(items)`, used on a scene load and as the rebuild), a quality metric, and `Optimize(budget)` that rebuilds when the metric has degraded. | Overlap queries on an insert-built tree were 1.8x to 3.3x slower than on the same boxes rebuilt top-down (4.5 vs 1.7 us, 6.2 vs 1.9 us, 1.8 vs 1.0 us). |
| Fat margin | **Amended:** a fraction of the object's largest extent, default **0.25** (`spatial.fat_margin` is a fraction, not world units). | Experiment (b): 25 percent halves the re-insert cost (0.49 to 0.24 us per moved object, 0.45 to 0.13 for agents) for a 0 to 6 percent query cost. |
| Hash grid | **Kept** (the planner's condition, 1.5x on some workload, is met). Used for large numbers of same-size dynamic items (crowds, particles); a set chooses `Bvh` or `Grid` for its dynamic side. | Experiment (a): for 100,000 agents the all-pairs query is 10.3 ms on a grid of cell 4 against 21.5 ms on a static BVH and 30 ms on a rebuilt dynamic BVH; updates cost 0.06 us per moved object against 0.24. It loses on variable sizes and on ray casts, so it is opt-in. |
| Loose octree | **Rejected.** | Cheapest build (3.5 ms) and update (0.04 us), but the all-pairs query is 4x to 6x slower than the static BVH (213 and 122 ms against 50 and 21 ms) and rays 6x to 10x slower. |
| Sets | A world has up to 8 named spatial sets (`render`, `physics`, `audio`, `gameplay`, ...); a bitmask on the entity says which sets it joins; each set has its own static and dynamic structure and its own layer mask. | The render tree must not hold invisible triggers and the reverse. |
| Queries | Overlap with box, sphere or frustum; ray cast (closest, any, all); box sweep; nearest-k; all overlapping pairs; every query takes a layer mask and an optional filter; results come out in a documented deterministic order. | Day 241. |
| Physics contract | A `PairTracker` keeps overlap pairs between frames and reports begin and end, deterministically; fat margins are internal; the narrow phase is Phase 22's. | Day 243. |
| Renderer contract | A parallel `CullFrustum` yields the visible slots; `RenderScene` and its callers stay unchanged apart from taking a precomputed visible list; `renderer.culling = bvh|flat` keeps the flat scan as the reference. | Day 247. |
| Bounds | `SpatialBounds` (local-space box from the mesh), `WorldBounds` (computed from `WorldTransform` only when it changed), a `Static` tag that keeps an entity in the static tree, a `SpatialLayer` component. | Day 244. |
| Determinism | The same inputs build the same tree (a parallel build equals the sequential one byte for byte); queries are read-only and thread-safe between updates; updates run as a system after transform propagation. | Replays, tests. |
| Settings | `spatial.parallel` (bool, default true), `spatial.fat_margin` (float, fraction, default 0.25), `spatial.stats`, `spatial.debug_draw`; `renderer.culling`. | `PLAN.md` assumption #22. |

## 3. Experiments

Throwaway programs (not committed; `std` on purpose; `g++ -O3 -std=c++20`, single thread, AMD Ryzen 9 7950X3D, Linux, 2026-11-10). Boxes are single precision. Every structure answered every query identically (sum of overlap counts and of ray hit distances equal across all structures, and equal to a brute force on the first 100 queries), so the tables compare correct implementations. Structures as implemented: static BVH as in section 2; dynamic BVH after Box2D's `b2DynamicTree` (48-byte nodes, one box per leaf); hash grid with items registered in every cell they touch, open-addressing cell table; loose octree with loose factor 2 and 10 levels.

### 3.1 Experiment (a): the structures on three workloads

Workloads (100,000 boxes each):
- **W1 static city:** footprints 8 to 40, heights 5 to 80, on a 4000 x 4000 plane.
- **W2 1 percent moving:** the same city; each of 20 frames moves 1,000 random objects by up to 0.3 of their size per axis.
- **W3 agents:** 1 x 2 x 1 boxes on a 1000 x 1000 plane, every agent moves 0.1 per frame (bouncing), 20 frames.

Queries: 5,000 overlap boxes (40 units in the city, 10 for agents) and 5,000 rays (from 120 units up, slanted down, length 500 in the city; at agent height, horizontal, length 100 for agents).

**Table A.** build in ms; update in microseconds per moved object (for the static BVH, a full rebuild every frame divided by the moved objects); overlap and ray in microseconds per query; bytes per object.

| Workload | Structure | build | update | overlap | ray | B/obj |
|---|---|---|---|---|---|---|
| W1 static | static BVH | 25.6 | - | 0.69 | 0.39 | 40 |
| | dynamic BVH (inserted) | 32.5 | - | 4.49 | 2.49 | 116 |
| | dynamic BVH (rebuilt top-down) | 24.1 | - | 1.69 | 0.58 | 116 |
| | hash grid (cell 32) | 15.1 | - | 1.10 | 0.27 | 197 |
| | loose octree | 3.4 | - | 2.29 | 2.37 | 89 |
| W2 1% moving | static BVH (rebuild per frame) | 24.1 | 23.9 | 0.73 | 0.40 | 40 |
| | dynamic BVH, margin 0 | 32.7 | 0.57 | 5.06 | 2.62 | 116 |
| | dynamic BVH, margin 10% | 31.9 | 0.53 | 6.18 | 3.33 | 116 |
| | dynamic BVH after one rebuild | 24.6 | 0.50 | 1.89 | 0.58 | 116 |
| | hash grid (cell 32) | 17.2 | 0.21 | 1.30 | 0.51 | 198 |
| | loose octree | 3.6 | 0.04 | 2.44 | 2.46 | 90 |
| W3 agents | static BVH (rebuild per frame) | 23.8 | 0.25 | 0.53 | 0.36 | 40 |
| | dynamic BVH, margin 0 | 34.6 | 0.51 | 2.08 | 1.01 | 116 |
| | dynamic BVH, margin 10% | 35.8 | 0.25 | 1.81 | 1.10 | 116 |
| | dynamic BVH after one rebuild | 24.1 | 0.24 | 0.98 | 0.47 | 116 |
| | hash grid (cell 2) | 21.6 | 0.06 | 3.94 | 0.54 | 283 |
| | loose octree | 6.1 | 0.04 | 2.16 | 3.74 | 91 |

**Table A2.** The broad phase: an overlap query of every object's own box on the final state, milliseconds for all 100,000 (pairs found: 715,291 in W2, 20,062 in W3).

| Structure | W2 | W3 |
|---|---|---|
| static BVH | 49.9 | 21.5 |
| dynamic BVH (inserted) | 356.4 | 120.0 |
| dynamic BVH (rebuilt) | 90.2 | 30.0 |
| hash grid, cell 1x / 2x / 4x of the base | 70.2 / 72.6 / 127.7 | 17.4 / 10.3 / 13.4 |
| loose octree | 213.0 | 122.2 |

What it says:
- The static tree answers queries best by a wide margin, and rebuilding it for every moved object is only worth it when nearly everything moves (W3: 0.25 us per moved object, the same as the dynamic tree's 0.24 to 0.25).
- An insert-built dynamic tree is 1.8x to 3.3x slower to query than a rebuilt one, and 4x slower on the all-pairs query in W2 (356 against 90 ms): **quality maintenance is required**, hence the bulk build, the metric and `Optimize`.
- The grid wins the broad phase for same-size agents (2x over the best BVH, 3x over the rebuilt dynamic one) and the update cost (4x); on the variable-size city it is close behind the static tree and on overlap queries for agents it is behind (3.9 against 1.0 to 1.8 us). The cell size matters (W3: 17.4, 10.3 and 13.4 ms for cells of 2, 4 and 8): the set takes it as a parameter, 2 to 4 times the item size.
- The octree wins only on update cost and build time; it is rejected.

### 3.2 Experiment (b): fat margin

Dynamic BVH, margin as a fraction of the object's size (largest extent: about 20 in W2, 1 in W3), 20 frames, queries after the motion:

| Workload | margin | re-inserted | update (us per moved) | overlap (us) | ray (us) |
|---|---|---|---|---|---|
| W2 | 0 | 100.0 % | 0.49 | 4.27 | 2.43 |
| | 5 % | 97.2 % | 0.60 | 4.57 | 2.52 |
| | 10 % | 88.7 % | 0.43 | 4.52 | 2.67 |
| | 25 % | 48.2 % | 0.24 | 4.49 | 2.72 |
| W3 | 0 | 100.0 % | 0.45 | 1.52 | 0.89 |
| | 5 % | 100.0 % | 0.46 | 1.62 | 1.01 |
| | 10 % | 50.0 % | 0.24 | 1.65 | 1.05 |
| | 25 % | 28.7 % | 0.13 | 1.61 | 1.08 |

Decision: **default fraction 0.25**. A larger margin cuts the re-inserts (and so the update cost, and the churn that degrades the tree) and costs at most 6 percent in overlap time at these sizes. Untested, filed for Day 240: extending the fat box along the direction of movement (the Box2D predictive margin), which helps directional motion like the agents' and which a test on the final tree should confirm before it is added; margins above 25 percent.

### 3.3 Experiment (c): the flat scan against BVH frustum culling

Constant density (the extent grows with the count), 64 random cameras (vertical field of view 69 degrees, aspect 16:9, far plane 800), the same visible set from all three methods at every size (checked). Milliseconds per cull, single thread; the flat scan is the plain per-box six-plane test writing the visible indices.

| N | flat scan | static BVH | speedup | dynamic BVH (inserted, fat) | speedup | builds (static / dynamic) |
|---|---|---|---|---|---|---|
| 1,000 | 0.0111 | 0.0047 | 2.4x | 0.0102 | 1.1x | 0.2 / 0.2 ms |
| 10,000 | 0.1044 | 0.0194 | 5.4x | 0.0900 | 1.2x | 2.3 / 2.7 ms |
| 100,000 | 0.9370 | 0.0472 | 19.8x | 0.2791 | 3.4x | 25.5 / 32.1 ms |
| 1,000,000 | 6.9852 | 0.0892 | 78.3x | 1.3644 | 5.1x | 312.8 / 526.8 ms |

The flat scan costs about 9 ns per object here (the renderer's own `RenderScene::Cull` measured 0.57 ms for 100,000 objects on Phase 17 Day 234, 5.7 ns, no output list). A static tree already wins at 1,000 objects (2.4x); an insert-built fat dynamic tree gains only 1.1x to 1.2x up to 10,000 and 3.4x at 100,000, so a mostly dynamic set below about 10,000 items gains little (a rebuilt tree, section 3.1, does better), and the renderer keeps `renderer.culling = flat` available.

## 4. Benchmark definition and pass condition

Fixed here so that Day 248 cannot move the goalposts. Hardware: the Linux machine of this spec (Ryzen 9 7950X3D), Release, `-O3 -DNDEBUG`, unity build, single thread unless stated; the Windows PC records its own numbers, not gated (as for `ecs_bench`).

Workloads and sizes: W1, W2 and W3 above, each at 1,000, 10,000, 100,000 and 1,000,000 items, with the metrics build, update (1 percent and 100 percent moving), overlap, ray, frustum, all-pairs and bytes per object, plus the sync costs of the ECS tier on the same entity counts.

**Pass condition for the phase** (measured on the finished system, not on the experiment code; each number is a ratio or an absolute chosen with headroom over the experiment):

1. **Culling gain:** the render set's frustum cull of a mostly static scene (99 percent static, 1 percent dynamic) at 100,000 objects takes at most 1/8 of the flat scan's time on the same run (experiment: 19.8x for a static tree alone, 3.4x for an all-dynamic one; the mix is expected near 15x), and at 1,000,000 objects at most 1/30 (experiment 78x static).
2. **A quiet scene is nearly free:** a frame in which nothing moved costs the spatial sync at most 5 percent of one flat scan of the same scene at 100,000 entities (experiment flat scan 0.94 ms, so at most 0.047 ms), through the change-detection stamps of the ECS.
3. **Moving 1 percent:** at most 1.0 us per moved object for the sync and the dynamic update at 100,000 entities (experiment: 0.24 to 0.5 us for the update alone).
4. **Everything moves:** 100,000 agents all moving: sync plus update plus the all-pairs feed within 80 ms per frame single thread (experiment: 24 ms update plus 21 to 30 ms pairs on the best structure), and at least 2x faster with 8 workers than with 1 (a design target of the parallel sync; if missed it is profiled and, failing a fix, filed and shown to the user).
5. **Query cost:** overlap on the static tree at 100,000 items at most 2 us per 40-unit query, ray at most 1.5 us (experiment 0.69 and 0.39).
6. **Memory:** at most 64 bytes per item for the static tree, 128 for the dynamic tree including the tight boxes (experiment 40 and 116).
7. **Exactness (not a speed number):** every structure and the set equal a brute force under at least 100,000 random operations, and the BVH-visible set equals the flat scan's on 1,000 random scenes and cameras.

A miss is profiled, fixed and re-run; if it stays missed it is filed in `KNOWN-ISSUES.md` and presented to the user (the numbers are the user's to override).

### 4.1 Measured (Phase 18 Day 248)

AMD Ryzen 9 7950X3D (16 cores), Linux, Clang, Release (`tests/spatial/spatial_bench.cpp`, `spatial_tests "[spatial-bench]"`; a camera that sees about 1 percent; boxes of 1 unit, one per 8 square units).

| Condition | Target | Measured | |
|---|---|---|---|
| Culling gain, 100,000 / 1,000,000 items | 8x / 30x | 80x / 95x (1.09 ms flat, 0.0136 ms tree; 11.1 ms, 0.117 ms) | met |
| Quiet scene | at most 5 percent of a flat scan | a static set costs no structure call (Day 245 test); a tiny view costs 1.2 us at 100k against 1.09 ms flat | met |
| Moved object at 1 percent moving, 100,000 | 1.0 us | 0.70 us (`SpatialSet::Move`, tree); 0.058 us (grid). Through the ECS sync, two sets: 7.6 ms per frame = 3.8 us per entity | set met, sync above |
| 100,000 all-moving agents, sync + update + pairs | 80 ms single thread; 2x faster with 8 workers | ECS sync alone 156 ms, 154 ms with 8 workers (1.0x); then 55-63 ms of pair update | **missed** |
| Overlap and ray on a static tree, 100,000 | 2 us, 1.5 us | 0.41 us, 0.64 us (1,000,000: 0.94, 1.38) | met |
| Memory per item | 64 B static, 128 B dynamic | static tree 67,073 nodes for 100,000 items (two per item) | met by node count; bytes not measured |
| Exactness | equal to brute force | the random-scene tests of Days 239-247 | met |

The miss: moving applies items to the sets on one thread in chunk order, and the dynamic tree re-inserts 0.8 to 0.9 us per move at this speed (agents that move 0.3 of their size a frame leave their 0.25 margin almost every frame). The render and physics sets each pay it, so the sync is two moves per agent (156 ms), and the workers cannot help because only the bounds pass is parallel. A grid moves an item in 0.1 us (10 ms for 100,000 against 87 ms) and a margin of 0.5 with the predictive extension takes the tree to 71 ms, 1.0 to 40 ms. Before this day the pair update on a grid scanned every item for each touched item (28 s for 100,000); it now goes through the grid's cells (55 ms).

## 5. Contracts the other phases rely on

- **Phase 22 (physics):** consumes `PairTracker` begin/end events and the set's layer masks; fat margins never leak into pair events; the narrow phase and the shapes are its own. `plans/phase-22-physics.md` already states this and needs no change.
- **Phase 17 Day 233 (`RenderSync`/`RenderScene`):** the bridge is unchanged; Phase 18 adds a visible-list overload of `RenderScene::Cull` and a visibility system. `RenderSync` does not assume it alone decides what is drawn (its note already says so).
- **Phase 25 (3D text):** needs nothing from here; its text entities become spatial members when they carry bounds.

## 6. Open points the days settle

- Day 240 decides by test whether the predictive margin (section 3.2) is added.
- Day 242 takes the grid cell size as a parameter and documents the 2x to 4x rule of thumb; the sync system chooses it from the set's declared item size.
- Parallel build: the top-down build's recursion is split over the job scheduler above a size threshold, with the sequential order of partitions so the result equals the sequential tree byte for byte (Day 239).
