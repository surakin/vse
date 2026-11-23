# Tutorial: Ask What Is Near Something

**Goal:** Put boxes in a spatial set, then ask which overlap a region, what a ray hits first, and which are nearest to a point.

**You need:** a built tree. Background: [Spatial partitioning](../guide/spatial.md).

## Steps

1. **Make a set.** `spatial::SpatialSet set;` holds a static tree for things that never move and a dynamic structure for things that do.
2. **Add items.** `set.AddStatic(box, key, layer)` adds a wall; `set.AddDynamic(box, key, layer)` adds something that will move. The `key` is yours (an entity id, an index): queries hand it back.
3. **Build the static tree.** `set.RebuildStatic();` after adding the walls. Queries are exact before it too, but slower.
4. **Overlap.** `spatial::Overlap(set, box, spatial::QueryFilter(), visitor)` calls the visitor with every item whose box touches `box`; return `true` to keep going.
5. **Ray.** `spatial::Raycast(set, ray, max_t, filter, RayMode::Closest, Order::Ordered, &hits)` fills `hits` with the nearest item along the ray.
6. **Nearest.** `spatial::Nearest(set, point, k, max_distance, filter, &out)` gives the `k` closest items, nearest first.
7. **Move something.** `set.Move(handle, new_box)` updates a dynamic item; the next query sees it.

## Complete code

```cpp compile run standalone
#include "containers/vector.h"
#include "math/geometry.h"
#include "log/output.h"
#include "spatial/query.h"
#include "spatial/spatial_set.h"

using namespace vse;

static Aabbd Cube(f64 x) { return Aabbd::FromMinMax(Vec3d(x - 1, -1, -1), Vec3d(x + 1, 1, 1)); }

int main() {
    spatial::SpatialSet set;
    // Step 2: four walls along the x axis (keys 0 to 3) and one mover (key 100).
    for (u32 i = 0; i < 4; ++i) {
        set.AddStatic(Cube(static_cast<f64>(i) * 10.0f), i, 0);
    }
    const spatial::SetHandle mover = set.AddDynamic(Cube(25.0f), 100, 0);
    set.RebuildStatic(); // step 3

    // Step 4: what touches the region around x = 20?
    u32 touching = 0;
    spatial::Overlap(set, Aabbd::FromMinMax(Vec3d(15, -2, -2), Vec3d(27, 2, 2)), spatial::QueryFilter(), [&](const spatial::Item& item) {
        out::Print("overlap: key %u\n", static_cast<u32>(item.key));
        ++touching;
        return true;
    });

    // Step 5: a ray along +x from x = -5 hits the first wall.
    Vector<spatial::RayHit> hits;
    spatial::Raycast(set, Rayd::Make(Vec3d(-5, 0, 0), Vec3d(1, 0, 0)), 100.0f, spatial::QueryFilter(), spatial::RayMode::Closest, spatial::Order::Ordered, &hits);
    out::Print("ray: first hit key %u at t = %.1f\n", static_cast<u32>(hits[0].key), static_cast<double>(hits[0].t));

    // Step 6: the two items nearest to x = 28.
    Vector<spatial::NearestHit> nearest;
    spatial::Nearest(set, Vec3d(28, 0, 0), 2, 1000.0f, spatial::QueryFilter(), &nearest);
    out::Print("nearest: keys %u and %u\n", static_cast<u32>(nearest[0].key), static_cast<u32>(nearest[1].key));

    // Step 7: move the mover far away; it no longer touches the region.
    set.Move(mover, Cube(500.0f));
    u32 after = 0;
    spatial::Overlap(set, Aabbd::FromMinMax(Vec3d(15, -2, -2), Vec3d(27, 2, 2)), spatial::QueryFilter(), [&](const spatial::Item&) {
        ++after;
        return true;
    });
    out::Print("touching before the move: %u, after: %u\n", touching, after);
    return 0;
}
```

```output
overlap: key 2
overlap: key 100
ray: first hit key 0 at t = 4.0
nearest: keys 3 and 100
touching before the move: 2, after: 1
```

## Where next

- [Spatial partitioning](../guide/spatial.md): the ECS tier (`SpatialBounds`, the sync system), pairs for physics, culling for the renderer.
