# Spatial partitioning: what is near what

Status: in progress (Phase 18). Built: the shared shapes (Day 238). Next: the static and dynamic bounding-volume hierarchies, the query vocabulary, the hash grid,
spatial sets, the pair tracker, the ECS tier and the renderer's culling (Days 239-249).

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

## See also

[Math](foundation-math.md) (vectors, matrices, transforms), [Renderer](renderer.md) (culling with these shapes), [ECS](ecs.md); reference:
[Foundation: Math](../reference/foundation-math.md), [Spatial Partitioning](../reference/spatial.md).
