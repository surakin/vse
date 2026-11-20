#pragma once

// Internal helpers of the query functions (spatial/query.h, Phase 18 Day 241): input checks, and adapters from each
// structure's item visitor to `bool(const Item&)`. Not public API (the reference covers engine/spatial/*.h only).

#include "lang/scalar.h"
#include "math/geometry.h"
#include "spatial/dynamic_bvh.h"
#include "spatial/hash_grid.h"
#include "spatial/spatial_set.h"
#include "spatial/static_bvh.h"
#include "types.h"

namespace vse::spatial::detail_query
{

inline bool Finite(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}
inline bool Finite(const Aabb& b)
{
    return Finite(b.min) && Finite(b.max);
}
inline bool HasNaN(f32 v)
{
    return v != v;
}
inline bool HasNaN(const Vec3& v)
{
    return HasNaN(v.x) || HasNaN(v.y) || HasNaN(v.z);
}
inline bool HasNaN(const Aabb& b)
{
    return HasNaN(b.min) || HasNaN(b.max);
}

// The structures' item visitors differ (the dynamic tree also passes the handle); these adapt them to `bool(const
// Item&)`.
template <class NodeTest, class Visit> TraversalStats Walk(const StaticBvh& tree, NodeTest&& node_test, Visit&& visit)
{
    return tree.Traverse(node_test, [&](const Item& item) { return visit(item); });
}
template <class NodeTest, class Visit> TraversalStats Walk(const DynamicBvh& tree, NodeTest&& node_test, Visit&& visit)
{
    return tree.Traverse(node_test, [&](ProxyId, const Item& item) { return visit(item); });
}
template <class Visit> TraversalStats WalkRay(const StaticBvh& tree, const Ray& ray, f32 max_t, Visit&& visit)
{
    return tree.TraverseRay(ray, max_t, [&](const Item& item, f32& limit) { return visit(item, limit); });
}
template <class Visit> TraversalStats WalkRay(const DynamicBvh& tree, const Ray& ray, f32 max_t, Visit&& visit)
{
    return tree.TraverseRay(ray, max_t, [&](ProxyId, const Item& item, f32& limit) { return visit(item, limit); });
}

template <class NodeTest, class Visit> TraversalStats Walk(const HashGrid& grid, NodeTest&& node_test, Visit&& visit)
{
    return grid.Traverse(node_test, [&](ProxyId, const Item& item) { return visit(item); });
}
template <class NodeTest, class Visit> TraversalStats Walk(const SpatialSet& set, NodeTest&& node_test, Visit&& visit)
{
    return set.Traverse(node_test, [&](SetHandle, const Item& item) { return visit(item); });
}
template <class Visit> TraversalStats WalkRay(const HashGrid& grid, const Ray& ray, f32 max_t, Visit&& visit)
{
    return grid.TraverseRay(ray, max_t, [&](ProxyId, const Item& item, f32& limit) { return visit(item, limit); });
}
template <class Visit> TraversalStats WalkRay(const SpatialSet& set, const Ray& ray, f32 max_t, Visit&& visit)
{
    return set.TraverseRay(ray, max_t, [&](SetHandle, const Item& item, f32& limit) { return visit(item, limit); });
}

// Whether hit `x` comes before `y`: nearer first, then the smaller key.
inline bool Before(f32 tx, SpatialKey kx, f32 ty, SpatialKey ky)
{
    return tx < ty || (tx == ty && kx < ky);
}

}  // namespace vse::spatial::detail_query
