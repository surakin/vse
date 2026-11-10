# Spatial partitioning: what is near what

Status: in progress (Phase 18). Built: the shared shapes (Day 238), the static and dynamic bounding-volume hierarchies (Days 239-240), the query vocabulary
(Day 241), the hash grid and the spatial set (Day 242), the pair tracker (Day 243), the components, the `SpatialWorld` resource and the entity lifecycle (Day 244), moves and the parallel sync (Day 245), entity-facing and batched
queries (Day 246). Next: the renderer's culling (Days 247-249).

**Audience:** engine programmers who write a system that asks "what is near this box, ray or frustum" (physics, culling, audio, gameplay). Read
[Math](foundation-math.md) first.

The spatial module (`engine/spatial`, namespace `spatial`) answers proximity questions once for every system, with exact, tested structures, instead of
each system keeping a private grid. Design, the experiments that chose the structures, and the benchmark the phase must pass:
`docs/superpowers/specs/2026-11-10-spatial-design.md`. The structures are generic over a 64-bit key (`spatial::SpatialKey`) and know nothing of the ECS;
the ECS tier that keeps them in step with entities comes later in the phase.

## Shapes

Every structure and query works on the shapes of `math/geometry.h` (namespace `engine_placeholder`, part of the foundation), which the renderer uses for
culling too: they moved there from `renderer/bounds.h` and `renderer/frustum.h`, which keep `renderer::Aabb`, `renderer::Frustum` and the rest as aliases,
so renderer code did not change.

| Shape | Type | Notes |
|---|---|---|
| axis-aligned box | `Aabb` | `min`/`max` corners; `Aabb::Empty()` is the box whose union with any box is that box; `box.IsEmpty()` |
| sphere | `Sphere` (the renderer's `BoundingSphere` is the same type) | `center`, `radius` |
| box and sphere of one object | `Bounds` | the renderer tests the sphere first |
| plane | `Plane` | `dot(normal, p) + d = 0`, inside where the value is not negative; `Plane::FromPointNormal` |
| ray | `Ray` | make it with `Ray::Make(origin, direction)`, which keeps the inverse direction for the box test |
| view volume | `Frustum` | six planes from a view-projection matrix (depth 0..1, as `renderer::PerspectiveVulkan` builds) |

The tests between them:

| I need | Use |
|---|---|
| do two shapes share a point | `Overlaps(box, box)`, `Overlaps(box, sphere)`, `Overlaps(sphere, sphere)`; exact |
| may a shape be in the view | `Overlaps(frustum, box)`, `Overlaps(frustum, sphere)`; conservative (see below) |
| is a point or a box inside | `Contains(box, point)`, `Contains(outer, inner)`, `Contains(sphere, point)` |
| where does a ray hit | `Intersect(ray, box, max_t, &t)`, `Intersect(ray, sphere, max_t, &t)`, `Intersect(ray, plane, max_t, &t)` |
| combine boxes | `Union(a, b)`, `Union(box, point)`, `Intersection(a, b)` (empty when they miss), `Expand(box, margin)` |
| measure a box | `SurfaceArea`, `Volume`, `Center`, `Distance2(point, box)` (squared, 0 inside) |
| a box after a transform | `TransformAabb(box, matrix)`: exactly the box of the eight transformed corners |

```cpp compile run standalone
#include "math/geometry.h"
#include "os/console.h"

using namespace engine_placeholder;

int main() {
    const Aabb crate = Aabb::FromMinMax(Vec3(-1, 0, -1), Vec3(1, 2, 1));
    Sphere player;
    player.center = Vec3(1.5f, 1, 0);
    player.radius = 0.6f;
    os::Print("player touches crate: %s\n", Overlaps(crate, player) ? "yes" : "no");

    f32 t = 0.0f;
    const Ray shot = Ray::Make(Vec3(-10, 1, 0), Vec3(1, 0, 0));
    if (Intersect(shot, crate, 100.0f, &t)) os::Print("shot hits at x = %.1f\n", shot.At(t).x);

    const Aabb both = Union(crate, Aabb::FromMinMax(Vec3(3, 0, 3), Vec3(4, 1, 4)));
    os::Print("union: %.0f %.0f %.0f to %.0f %.0f %.0f, area %.0f\n", both.min.x, both.min.y, both.min.z, both.max.x, both.max.y, both.max.z, SurfaceArea(both));
    return 0;
}
```

```output
player touches crate: yes
shot hits at x = -1.0
union: -1 0 -1 to 4 2 4, area 90
```

### Rules

- **Boxes and spheres are closed.** Shapes that only touch overlap, and a ray that grazes a face or runs along an edge hits it. A box with `min` above `max`
  on some axis is empty: it overlaps nothing, a ray never hits it, and a union ignores it.
- **The frustum tests are conservative.** `Overlaps(frustum, shape)` is false only when the shape is entirely beyond one of the six planes, so a shape just
  outside a corner of the view volume can be reported as inside. That is the right error for culling (draw a little too much, never too little); a caller
  that needs the exact answer tests further.
- **Rays.** `t` is measured in units of the direction's length (a unit direction gives distances). A ray that starts inside a box or sphere hits it at
  `t = 0`. A direction component of zero is handled exactly (the ray is inside that slab or never), so axis-aligned rays need no special care. Only
  `max_t` and later count: pass a large value for an unbounded ray.
- **`TransformAabb` is exact for the box, not for the object.** It returns the box of the transformed box's corners; a rotated object's true extent can
  be smaller. Recompute from the vertices when a tight box matters.

## The static BVH

`spatial::StaticBvh` (`spatial/static_bvh.h`) is for things that do not move: built once from a list of `spatial::Item`s (a box, your 64-bit key and a
layer), then queried any number of times, from any number of threads at once. Rebuild it when the set of items changes; for things that move, the dynamic
tree (Day 240) is the one to use.

```cpp compile run standalone
#include "containers/vector.h"
#include "os/console.h"
#include "spatial/static_bvh.h"

using namespace engine_placeholder;
using namespace engine_placeholder::spatial;

int main() {
    // A row of ten crates, keys 0 to 9, two units apart.
    Vector<Item> items;
    for (u32 i = 0; i < 10; ++i) {
        Item item;
        item.box = Aabb::FromMinMax(Vec3(2.0f * i, 0, 0), Vec3(2.0f * i + 1, 1, 1));
        item.key = i;
        items.Push(item);
    }
    StaticBvh tree;
    tree.Build(Span<const Item>(items.Data(), items.Size()));

    // Which crates overlap a box from x = 5 to x = 9?
    const Aabb query = Aabb::FromMinMax(Vec3(5, 0, 0), Vec3(9, 1, 1));
    os::Print("overlapping:");
    tree.Traverse([&](const Aabb& node) { return Overlaps(node, query) ? NodeVisit::Descend : NodeVisit::Reject; },
                  [&](const Item& item) {
                      if (Overlaps(item.box, query)) os::Print(" %llu", static_cast<unsigned long long>(item.key));
                      return true;
                  });
    os::Print("\n");

    // The first crate a ray from the left hits: lower the limit to each hit, so farther leaves are skipped.
    const Ray ray = Ray::Make(Vec3(-5, 0.5f, 0.5f), Vec3(1, 0, 0));
    SpatialKey first = 0;
    f32 best = -1.0f;
    tree.TraverseRay(ray, 100.0f, [&](const Item& item, f32& max_t) {
        f32 t = 0.0f;
        if (Intersect(ray, item.box, max_t, &t) && (best < 0.0f || t < best)) {
            best = t;
            first = item.key;
            max_t = t;
        }
        return true;
    });
    os::Print("ray hits crate %llu at t = %.1f\n", static_cast<unsigned long long>(first), best);
    return 0;
}
```

```output
overlapping: 2 3 4
ray hits crate 0 at t = 5.0
```

How it is built and what that buys:

- **Top-down, binned surface-area heuristic.** Each node is split where the expected cost of a query is lowest, judged on 16 bins along the node's longest
  axis, down to leaves of at most 4 items. A query then tests few nodes: about 0.7 us for a 40-unit box in a city of 100,000 buildings (design spec,
  experiment (a)).
- **Compact and ordered.** Nodes are 32 bytes in one array in depth-first order, and the items are stored in leaf order, so all the items under a node are
  one contiguous range. A traversal that finds a node entirely inside its query answers `NodeVisit::AcceptAll` and gets every item below it without
  further tests. 100,000 items take about 62 bytes each, nodes included.
- **Deterministic.** The same items in the same order build the same tree, byte for byte; ties are broken by the items' order.
- **Bounded depth.** Items with the same center, and inputs that would make the tree very deep (sizes growing geometrically along a line), are split in the
  middle instead, so every tree fits the traversal's fixed stack of 64 entries and no traversal allocates.
- **Cost.** Building 100,000 boxes takes about 40 ms in a Release build on the Windows PC (the build is single-threaded).

The two traversals are the building blocks the query functions of Day 241 are made of: `Traverse` takes a node test (reject, descend, or accept the whole
subtree) and an item visitor; `TraverseRay` visits the leaves a ray reaches, nearest first, and lets the visitor shorten the ray. Both return
`TraversalStats` (nodes tested, items visited).

## The dynamic BVH

`spatial::DynamicBvh` (`spatial/dynamic_bvh.h`) is for things that move. `Insert` returns a `spatial::ProxyId` (a slot and a generation: a handle kept
after `Remove` is refused, not mistaken for a later item); `Move` gives the item a new box; `Remove` takes it out. The queries are the same two traversals
as the static tree's, and the item visitor also receives the handle.

```cpp compile run standalone
#include "os/console.h"
#include "spatial/dynamic_bvh.h"

using namespace engine_placeholder;
using namespace engine_placeholder::spatial;

int main() {
    DynamicBvh tree;
    const ProxyId player = tree.Insert(Aabb::FromMinMax(Vec3(0, 0, 0), Vec3(1, 2, 1)), 1, 0);
    const ProxyId crate = tree.Insert(Aabb::FromMinMax(Vec3(5, 0, 0), Vec3(6, 1, 1)), 2, 0);

    // A step of 0.1 stays inside the fat box (a quarter of the largest extent on every side): nothing in the tree changes.
    const bool changed = tree.Move(player, Aabb::FromMinMax(Vec3(0.1f, 0, 0), Vec3(1.1f, 2, 1)));
    os::Print("small step changed the tree: %s\n", changed ? "yes" : "no");
    // A jump leaves it: the leaf is taken out and put back.
    os::Print("jump changed the tree: %s\n", tree.Move(player, Aabb::FromMinMax(Vec3(4.5f, 0, 0), Vec3(5.5f, 2, 1))) ? "yes" : "no");

    const Aabb near_crate = Aabb::FromMinMax(Vec3(4, 0, 0), Vec3(7, 1, 1));
    tree.Traverse([&](const Aabb& node) { return Overlaps(node, near_crate) ? NodeVisit::Descend : NodeVisit::Reject; },
                  [&](ProxyId, const Item& item) {
                      if (Overlaps(item.box, near_crate)) os::Print("near the crate: key %llu\n", static_cast<unsigned long long>(item.key));
                      return true;
                  });

    tree.Remove(crate);
    os::Print("removed handle still live: %s\n", tree.IsLive(crate) ? "yes" : "no");
    return 0;
}
```

```output
small step changed the tree: no
jump changed the tree: yes
near the crate: key 2
near the crate: key 1
removed handle still live: no
```

How it stays fast while things move:

- **Fat boxes.** A leaf stores the item's box grown by `fat_margin` (a fraction of the item's largest extent, 0.25 by default, `spatial.fat_margin`) and,
  along the last movement, `predictive` times that displacement (2 by default). A move that stays inside the stored box costs a comparison; only a move
  out of it re-inserts the leaf. The tight box is kept beside the fat one: the item visitor gets the tight box, so a query can be exact. A stored box
  that has become far larger than needed (an item that moved fast and stopped) is shrunk by a re-insert. An item of zero size gets no margin, so each of
  its moves re-inserts it.
- **Good insertion.** A new leaf goes beside the node that adds the least total surface area (a branch-and-bound search), and rotations on the way up
  keep the tree balanced (the height stays near 1.44 log2 of the count).
- **Quality upkeep.** A tree built only by insertions answers queries 1.8 to 3.3 times slower than a top-down build of the same boxes (the spec's
  experiment (a)). `Build(items, handles)` makes a fresh tree top-down (use it on a scene load); `Optimize(budget)` rebuilds one subtree of about
  `budget` leaves per call, a different one each time, and the whole tree once its SAH cost has grown past `rebuild_threshold` (1.3) times the cost
  after the last full build. Call it once a frame: with 1,000 items all moving for 1,000 frames and `Optimize(64)` each frame, the tree stayed within
  1.13 times the SAH cost of a fresh build.
- **No allocation in the steady state.** Nodes live in one pool with a free list; once it has grown to its working size, moves and queries allocate
  nothing (tested with an allocation counter). 100,000 items take 128 bytes each, nodes, fat and tight boxes included. `Optimize` on a quiet frame
  (nothing inserted, removed or re-inserted since its last call) returns at once. `Validate()` checks every invariant, for tests.

**Choosing the predictive margin (Day 240, Release, the Windows PC).** 10,000 agents of 1 x 2 x 1 for 60 frames, each step 0.1:

| Motion | `predictive` | Re-inserted per move | Cost per move | Nodes tested per 10-unit query |
|---|---|---|---|---|
| straight lines | 0 | 15.8 % | 339 ns | 32.9 |
| straight lines | 2 | 11.3 % | 255 ns | 32.9 |
| straight lines | 4 | 9.6 % | 221 ns | 33.1 |
| a new direction every frame | 0 | 2.6 % | 64 ns | 32.4 |
| a new direction every frame | 2 | 2.3 % | 71 ns | 32.5 |

Prediction pays where motion keeps its direction and costs almost nothing where it does not, so the default is 2; set it to 0 for items that never move
far in one direction (jittering particles), 4 for fast straight movers.

## Queries

`spatial/query.h` asks the same questions of every structure (both trees now, the spatial set of Day 242 later), as free functions:

| I need | Call | Results |
|---|---|---|
| what overlaps a box, sphere or frustum | `Overlap(tree, shape, filter, visit)` or `OverlapKeys(tree, shape, filter, order, &keys)` | each item; a frustum is conservative |
| what a ray hits | `Raycast(tree, ray, max_t, filter, mode, order, &hits)` | `RayMode::Closest`, `Any` (line of sight) or `All`; `RayHit { key, t }` |
| where a moving box first touches something | `SweepBox(tree, box, motion, filter, &hit)` | `t` is the fraction of `motion`; 0 when already touching |
| the k nearest items | `Nearest(tree, point, k, max_distance, filter, &out)` | `NearestHit { key, distance }`, always nearest first |
| every overlapping pair | `ForEachPair(tree, mask_a, mask_b, filter, visit)` or `PairKeys(...)` | each pair once, the smaller key first |

```cpp compile run standalone
#include "containers/vector.h"
#include "os/console.h"
#include "spatial/query.h"

using namespace engine_placeholder;
using namespace engine_placeholder::spatial;

bool NotTheShooter(SpatialKey key, void*) { return key != 1; }

int main() {
    DynamicBvh tree;
    tree.Insert(Aabb::FromMinMax(Vec3(0, 0, 0), Vec3(1, 2, 1)), 1, 0); // the shooter, layer 0 (characters)
    tree.Insert(Aabb::FromMinMax(Vec3(4, 0, 0), Vec3(5, 2, 1)), 2, 0); // an enemy
    tree.Insert(Aabb::FromMinMax(Vec3(8, 0, 0), Vec3(9, 3, 1)), 3, 1); // a wall, layer 1 (scenery)

    // The first thing a shot from the shooter hits, ignoring the shooter itself.
    QueryFilter shot;
    shot.filter = &NotTheShooter;
    Vector<RayHit> hits;
    Raycast(tree, Ray::Make(Vec3(0.5f, 1, 0.5f), Vec3(1, 0, 0)), 100.0f, shot, RayMode::Closest, Order::Traversal, &hits);
    os::Print("shot hits key %llu at %.1f\n", static_cast<unsigned long long>(hits[0].key), hits[0].t);

    // Only scenery: the same ray with a mask of layer 1.
    QueryFilter scenery;
    scenery.mask = 1u << 1;
    Raycast(tree, Ray::Make(Vec3(0.5f, 1, 0.5f), Vec3(1, 0, 0)), 100.0f, scenery, RayMode::Closest, Order::Traversal, &hits);
    os::Print("scenery hit: key %llu\n", static_cast<unsigned long long>(hits[0].key));

    // The two items nearest a point, nearest first.
    Vector<NearestHit> near;
    Nearest(tree, Vec3(6, 1, 0.5f), 2, 50.0f, QueryFilter(), &near);
    os::Print("nearest: %llu (%.1f), %llu (%.1f)\n", static_cast<unsigned long long>(near[0].key), near[0].distance,
              static_cast<unsigned long long>(near[1].key), near[1].distance);
    return 0;
}
```

```output
shot hits key 2 at 3.5
scenery hit: key 3
nearest: 2 (1.0), 3 (2.0)
```

Rules:

- **Layers and filters.** An item's `layer` is a bit index 0 to 31; a `QueryFilter` has a `mask` (one bit per layer, all by default) and an optional
  `bool filter(SpatialKey, void* user)` with its `user` pointer (no `std::function`, no capture: pass what it needs through `user`). An item whose
  layer is 32 or more matches no mask. `ForEachPair` takes two masks instead: a pair counts when one item is in `mask_a` and the other in `mask_b`.
- **Order.** The visitor forms deliver results in traversal order: deterministic for one tree, but it changes when the tree is rebuilt or an item
  moves. The collecting forms take an `Order`: `Order::Ordered` sorts the (small) result: overlaps by key, ray hits by distance then key, pairs by
  first key then second. `Nearest` is always nearest first, and every tie is broken by the smaller key.
- **Stopping.** A visitor returns false to stop; `QueryStats::stopped` then says so. `RayMode::Any` stops at its first hit by design.
- **Bad input.** A query whose input has a NaN (or a ray, sweep or point that is not finite) answers nothing and sets `QueryStats::refused_input`;
  an infinite box is allowed (it finds everything). An empty box, a negative radius or a negative distance finds nothing.
- **Exactness.** Overlap, rays and sweeps test the items' tight boxes (the dynamic tree's fat boxes only steer the search); the frustum test is
  conservative like the renderer's. `SweepBox` is exact for a box against boxes: a ray from the box's center against each box grown by its half extents.
- **Cost and threads.** No query allocates, except to grow the result vector you pass (keep it between queries and it stops growing). Queries are
  `const` and safe from any number of threads at once, as long as nobody changes the structure meanwhile (8 threads are tested against the
  single-threaded answers). `Nearest` is a depth-first search that skips every node farther than the k-th best so far, for a small `k` (each candidate costs up to
  `k` steps); `ForEachPair` runs one overlap
  query per item, so it costs about n times an overlap query.

## The spatial set

`spatial::SpatialSet` (`spatial/spatial_set.h`) is what a system uses: one static tree for what does not move, plus one structure for what does (a
dynamic tree by default, or a hash grid), answering every query of `spatial/query.h` over both. Keep one set per purpose (the renderer's, physics',
audio's), so the render set never holds an invisible trigger and the reverse.

```cpp compile run standalone
#include "containers/vector.h"
#include "os/console.h"
#include "spatial/query.h"

using namespace engine_placeholder;
using namespace engine_placeholder::spatial;

int main() {
    SpatialSet world;
    world.AddStatic(Aabb::FromMinMax(Vec3(0, 0, 0), Vec3(10, 3, 1)), 1, 0);              // a wall
    const SetHandle door = world.AddStatic(Aabb::FromMinMax(Vec3(10, 0, 0), Vec3(12, 3, 1)), 2, 0);
    const SetHandle guard = world.AddDynamic(Aabb::FromMinMax(Vec3(5, 0, 2), Vec3(6, 2, 3)), 3, 0);
    world.RebuildStatic(); // the wall and the door go into the static tree

    // The door opens: it moves now, so it becomes dynamic (same handle).
    world.MakeDynamic(door);
    world.Move(door, Aabb::FromMinMax(Vec3(10, 0, 0), Vec3(11, 3, 2)));
    world.Move(guard, Aabb::FromMinMax(Vec3(10, 0, 1), Vec3(11, 2, 2)));

    Vector<SpatialKey> near_door;
    OverlapKeys(world, Aabb::FromMinMax(Vec3(9, 0, 0), Vec3(12, 3, 3)), QueryFilter(), Order::Ordered, &near_door);
    os::Print("near the door:");
    for (usize i = 0; i < near_door.Size(); ++i) os::Print(" %llu", static_cast<unsigned long long>(near_door[i]));
    os::Print("\n");
    os::Print("door is static: %s\n", world.IsStatic(door) ? "yes" : "no");
    return 0;
}
```

```output
near the door: 1 2 3
door is static: no
```

- **Static items** join the static tree at the next `RebuildStatic()` (call it after loading a level or a batch of changes); until then queries scan
  them from a short pending list, and the tree's entries for items removed or made dynamic are skipped, so a query is exact at every moment. The same
  items rebuild the same tree. `Move` refuses a static item: `MakeDynamic` first (the door that opens), `MakeStatic` when it settles.
- **The dynamic side** is a `DynamicBvh` (items of any size; call `Optimize(budget)` once a frame) or, with `SpatialSetConfig::dynamic =
  DynamicKind::Grid`, a `HashGrid` for many items of about one size.
- **Order.** A visitor sees the static tree's items first, then the pending static items in the order they came, then the dynamic side; `Order::Ordered`
  sorts as for every structure.

### The hash grid

`spatial::HashGrid` (`spatial/hash_grid.h`) registers each item in every cubic cell its box touches; the occupied cells are in an open-addressing
hash table keyed by their coordinates. Box and sphere overlaps and the all-pairs query walk the cells and report each result once (from the cell holding
the lowest corner of the overlap: no per-query marks, so concurrent queries stay safe); the frustum, ray, sweep and nearest queries scan the items, so use
a BVH where those matter. An item that would touch more than 64 cells, or lies beyond the cells a key holds (about a million cells from the origin on
each axis), goes to an "oversized" list that every query scans: keep the cell size 2 to 4 times the items' size.

Measured on the Windows PC (Release), 100,000 agents of 1 x 2 x 1 on a 1000 x 1000 plane, cell 4: the all-pairs query takes 7 ms on the grid against
16.6 ms on a static tree of the same boxes (2.4 times faster; the design spec's experiment found 2.1 times), and an update costs 36 ns per moved agent
(a static tree would need a 35 ms rebuild). That is why the grid is kept, and why it is opt-in: on boxes of very different sizes and for rays it loses.

## Pairs for physics

`spatial::PairTracker` (`spatial/pair_tracker.h`) is the contract the physics phase builds on: after each frame's moves it says which pairs of items
**began** and which **ended** overlapping, sorted, so the narrow phase creates and destroys its contacts from events instead of searching every frame.

```cpp compile run standalone
#include "os/console.h"
#include "spatial/pair_tracker.h"

using namespace engine_placeholder;
using namespace engine_placeholder::spatial;

namespace {
void Show(int frame, const PairEvents& events) {
    os::Print("frame %d:", frame);
    for (usize i = 0; i < events.begin.Size(); ++i) os::Print(" begin(%llu,%llu)", static_cast<unsigned long long>(events.begin[i].a), static_cast<unsigned long long>(events.begin[i].b));
    for (usize i = 0; i < events.end.Size(); ++i) os::Print(" end(%llu,%llu)", static_cast<unsigned long long>(events.end[i].a), static_cast<unsigned long long>(events.end[i].b));
    os::Print("\n");
}
} // namespace

int main() {
    SpatialSet world;
    PairTrackerConfig config;
    config.margin = 0.0f; // a pair exists exactly while the boxes overlap
    PairTracker pairs(config);
    PairEvents events;

    const SetHandle a = world.AddDynamic(Aabb::FromMinMax(Vec3(0, 0, 0), Vec3(2, 2, 2)), 1, 0);
    const SetHandle b = world.AddDynamic(Aabb::FromMinMax(Vec3(5, 0, 0), Vec3(7, 2, 2)), 2, 0);
    pairs.Touch(a); // every added item is touched once
    pairs.Touch(b);
    pairs.Update(world, &events);
    Show(1, events);

    world.Move(b, Aabb::FromMinMax(Vec3(1, 0, 0), Vec3(3, 2, 2))); // b moves into a
    pairs.Touch(b);                                                  // only moved items are re-queried
    pairs.Update(world, &events);
    Show(2, events);

    pairs.Remove(2); // b is destroyed: its pairs end in the same update
    world.Remove(b);
    pairs.Update(world, &events);
    Show(3, events);
    return 0;
}
```

```output
frame 1:
frame 2: begin(1,2)
frame 3: end(1,2)
```

- **What you tell it.** `Touch(handle)` for every item added, moved, given another layer or switched between static and dynamic; `Remove(key)` for
  every item removed (its handle is stale by then). `Update(set, &events)` re-queries only the touched items, so a frame in which few things moved costs
  few queries: 100,000 dynamic objects with 1 percent moving take about 4.6 ms a frame in a debug build (the test records the number; Release is faster).
- **The events** are `KeyPair`s with the smaller key first, sorted by (first, second), whatever order the items moved in: two runs of the same inputs give
  the same events, with a tree or with a grid behind the set. A narrow phase that applies them in order keeps a contact list that equals a brute-force
  overlap test every frame (the test does exactly that).
- **Layers and static items.** `PairTrackerConfig::SetMeets(a, b, false)` says that layers `a` and `b` never form pairs (physics layers are an input, not
  hard-coded). Two static items never form a pair: they cannot start to touch.
- **The margin.** With `margin` above 0 a pair is made when two boxes overlap and kept until the boxes, each grown by `margin` times its largest extent,
  stop overlapping, so two objects jittering at the edge of contact do not produce a begin and an end every frame. The pairs then lie between the touching
  pairs and the near pairs (tested); `Recompute(set, &events)` is the reference that makes pairs only where boxes overlap, and equals `Update` when the
  margin is 0.
- **Lifecycle.** Removing an item ends its pairs in the same update. A new key never inherits pairs. A key removed and added back between two updates
  is the same item for pairs if it is touched: pairs that still hold produce no events.

## Putting entities in the spatial world

Systems should not keep their own copy of where things are. Give an entity the components of `spatial/components.h` and the sync system keeps it in
the sets for you.

```cpp compile run standalone
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "os/console.h"
#include "spatial/components.h"
#include "spatial/query.h"
#include "spatial/spatial_sync.h"
#include "spatial/spatial_world.h"

using namespace engine_placeholder;
using namespace engine_placeholder::spatial;

int main() {
    ecs::World world;
    world.SetResource(ecs::TransformReport{});
    world.SetResource(SpatialWorld()); // the sets: render, physics, audio, gameplay
    ecs::Schedule schedule;
    schedule.Add(ecs::MakeTransformSystem("transforms"));
    schedule.Add(MakeSpatialSyncSystem("spatial_sync").InStage(ecs::kPostUpdate));
    schedule.Build();

    SpatialBounds bounds;
    bounds.local = Aabb::FromMinMax(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
    SpatialMembership membership;
    membership.sets = (1u << kRenderSet) | (1u << kPhysicsSet); // in the render set and the physics set
    ecs::LocalTransform at;
    at.position = Vec3(10, 0, 0);
    world.Create(at, ecs::WorldTransform{}, bounds, membership);

    schedule.Run(world, 0.016f);

    const SpatialWorld& spatial = world.Resource<SpatialWorld>();
    Vector<SpatialKey> keys;
    OverlapKeys(*spatial.Set(kRenderSet), Aabb::FromMinMax(Vec3(5, -1, -1), Vec3(15, 1, 1)), QueryFilter(), Order::Ordered, &keys);
    os::Print("render set: %u item\n", static_cast<unsigned>(keys.Size()));
    return 0;
}
```

```output
render set: 1 item
```

- **Components.** `SpatialBounds` (the local box, normally from the mesh), `SpatialMembership` (a bit per set and a layer), the tag `SpatialStatic` (the
  entity does not move: its items go to each set's static tree). The sync adds `WorldBounds` (the box in world space; read it, never set it) and a hidden
  `SpatialProxy` (the handles; it is the sync's). An entity needs `SpatialBounds`, a `WorldTransform` and `SpatialMembership` to be in a set.
- **Sets.** `SpatialWorld` is a resource with four sets that always exist (`kRenderSet`, `kPhysicsSet`, `kAudioSet`, `kGameplaySet`) so independent systems
  agree without coordinating, and up to four more (`CreateSet("crowd", config)`, for example a grid for a crowd); `Find(name)` looks one up.
- **Lifecycle.** An entity joins at the next sync. Removing `SpatialBounds`, `WorldTransform` or `SpatialMembership` takes it out of every set, so does
  destroying it (found through the removal log of `SpatialProxy`, not by scanning). Changing `SpatialMembership` (the sets or the layer) adds the item to the
  new sets and removes it from the old ones; adding or removing `SpatialStatic` moves the items between the static and the dynamic side. After any
  structural change the number of items in a set equals the number of entities that belong in it.
- **Refusals.** A box that is not finite, or is empty, is counted in `SpatialWorldStats::refused_bounds` (looked at again every frame); a set bit that names no
  set is counted in `unknown_set_bits`; a ninth set is refused (`refused_sets`).
- **Frame order.** Register the sync after the transform propagation (a later stage, or later in the same one). A query that runs before the sync in a frame
  sees the previous frame's sets.

## The sync system and the frame order

The sync keeps the sets true with the cost of what changed, not of what exists.

- **Order in a frame.** Propagate the transforms, then run the sync (a later stage, or later in the same one), then any system that queries the sets. A query that runs
  before the sync in a frame sees the previous frame's sets (documented, not an error).
- **Moves.** The sync recomputes `WorldBounds` (`TransformAabb` of `SpatialBounds::local` by the `WorldTransform`) for the chunks whose transform or local box changed,
  and writes a value only when it differs, so `Changed<WorldBounds>` filters in other systems stay exact. It then calls `SpatialSet::Move` for the entities whose box
  differs from their item, in chunk order on one thread: the structures are never written concurrently, so the result does not depend on how jobs were scheduled.
  A static entity does not move; if one does, the set counts a refused move (`SpatialSetStats::static_moves_refused`) and keeps the old box.
- **Parallel.** The box computation is a chunk loop that runs as jobs when the sync system is alone in its wave and the schedule has a job scheduler;
  `SpatialWorld::SetParallel(false)` runs it on the calling thread. Both give the same sets (tested, also under ThreadSanitizer).
- **Static trees.** A set's static tree is rebuilt only in a frame in which its static items changed (an add, a removal, a tag change): a static world costs the
  `Changed` queries and no structure calls (tested: 100 frames, the counters do not move). Queries stay exact between a change and the rebuild.
- **Cost.** Change detection is per chunk (see [ECS](ecs.md)), so a frame in which a few entities of every chunk moved still looks at every row once: 100,000
  entities with 1 percent moving cost about 7.6 ms a frame in a Release build with two sets (transforms included), 10 percent 73 ms, 100 percent 740 ms; most
  of the time is the dynamic tree reinserting boxes that left their margin (about 3.6 to 7 microseconds per moved item and set with a walk of 1.5 units for boxes
  of 1 unit). A crowd of same-size agents belongs in a set made with `DynamicKind::Grid`, whose updates are about four times cheaper (Day 248 measures both).

## Asking questions from gameplay code

`spatial/entity_query.h` asks the questions of `query.h` of a named set and answers in entities.

```cpp compile run standalone
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "os/console.h"
#include "spatial/entity_query.h"
#include "spatial/spatial_sync.h"

using namespace engine_placeholder;
using namespace engine_placeholder::spatial;

struct Health {
    float hp = 10.0f;
};

int main() {
    ecs::World world;
    world.SetResource(ecs::TransformReport{});
    world.SetResource(SpatialWorld());
    ecs::Schedule schedule;
    schedule.Add(ecs::MakeTransformSystem("transforms"));
    schedule.Add(MakeSpatialSyncSystem("spatial_sync").InStage(ecs::kPostUpdate));
    schedule.Build();

    SpatialBounds bounds;
    bounds.local = Aabb::FromMinMax(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
    SpatialMembership membership;
    membership.sets = 1u << kGameplaySet;
    for (int i = 0; i < 5; ++i) {
        ecs::LocalTransform at;
        at.position = Vec3(static_cast<float>(i) * 2.0f, 0, 0);
        world.Create(at, ecs::WorldTransform{}, bounds, membership, Health{});
    }
    schedule.Run(world, 0.016f);
    schedule.Run(world, 0.016f);

    // Everything within 3 units of x = 4 loses health: the spatial query picks the entities, the join fetches their components.
    const Aabb blast = Aabb::FromMinMax(Vec3(1, -3, -3), Vec3(7, 3, 3));
    const usize hit = ForEachIn<Health>(world, world.Resource<SpatialWorld>(), kGameplaySet, blast, QueryFilter(),
                                         [](ecs::Entity, Health& h) { h.hp -= 4.0f; });
    os::Print("%u entities hit\n", static_cast<unsigned>(hit));
    return 0;
}
```

```output
3 entities hit
```

- **Entity forms.** `OverlapEntities`, `RaycastEntity`, `SweepEntity` and `NearestEntities` take the `SpatialWorld`, a set id (`kRenderSet`, ...) and the
  arguments of the key form, and return `ecs::Entity` (a key is `Entity::ToU64`). A hit carries the entity, the ray parameter and the point. The answers are
  those of the last sync; an entity destroyed since can still be listed until the next sync, the same rule as a pointer from `World::Get` (check `Exists`).
- **`ForEachIn<Ts...>(world, spatial, set, region, filter, fn)`** calls `fn(entity, components...)` for the entities the spatial query returns that have all the
  components: a `const T` is read, a plain `T` is written (and the chunk stamped, like `Get`). It visits them in entity id order and `fn` must not change
  the world's structure. (The plan sorted hits by archetype; entity order is simpler and the lookup goes through `Get`.)
- **Batches.** `RaycastBatch` and `OverlapBatch` answer thousands of questions at once; with a job scheduler they split over the workers and the answers
  are in input order, identical to the sequential run (tested, also under ThreadSanitizer). Call them from the main thread outside any job, while no sync runs.

## See also

[Math](foundation-math.md) (vectors, matrices, transforms), [Renderer](renderer.md) (culling with these shapes), [ECS](ecs.md); reference:
[Foundation: Math](../reference/foundation-math.md), [Spatial Partitioning](../reference/spatial.md).
