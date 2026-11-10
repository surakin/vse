# Spatial partitioning: what is near what

Status: in progress (Phase 18). Built: the shared shapes (Day 238), the static bounding-volume hierarchy (Day 239). Next: the dynamic hierarchy, the query
vocabulary, the hash grid, spatial sets, the pair tracker, the ECS tier and the renderer's culling (Days 240-249).

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

## See also

[Math](foundation-math.md) (vectors, matrices, transforms), [Renderer](renderer.md) (culling with these shapes), [ECS](ecs.md); reference:
[Foundation: Math](../reference/foundation-math.md), [Spatial Partitioning](../reference/spatial.md).
