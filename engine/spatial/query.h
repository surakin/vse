#pragma once

//! The query vocabulary (Phase 18 Day 241): overlap with a box, sphere or frustum, ray casts (closest, any, all), box
//! sweeps, the k nearest items and all overlapping pairs, with layer masks, an optional filter and a documented order.
//! Free functions over any structure with the two traversals of
//! [[spatial::StaticBvh]] and [[spatial::DynamicBvh]] (`Traverse` and `TraverseRay`), so the static tree, the dynamic
//! tree and later the spatial set answer the same calls, and so do [[spatial::HashGrid]] and [[spatial::SpatialSet]]:
//! box and sphere overlaps on them use the grid's cells, the all-pairs query on a grid too; the other queries scan a
//! grid's items (a grid is for overlaps and pairs; use a BVH for rays).

#include "algorithm.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "lang/scalar.h"
#include "math/geometry.h"
#include "spatial/dynamic_bvh.h"
#include "spatial/hash_grid.h"
#include "spatial/internal/query_walk.h"
#include "spatial/spatial.h"
#include "spatial/spatial_set.h"
#include "spatial/static_bvh.h"
#include "types.h"

namespace engine_placeholder::spatial
{

/// Which items a query sees: those whose layer bit is in `mask` and, when `filter` is set, for which `filter(key,
/// user)` returns true.
///
/// An item's `layer` is a bit index from 0 to 31; an item whose layer is 32 or more matches no mask.
///
/// - Thread-safety: thread-compatible plain data; the filter is called on the querying thread and must be safe to call
/// from every thread that queries.
/// - Ownership: `user` is borrowed for the query.
struct QueryFilter
{
    /// One bit per layer; all layers by default.
    u32 mask = 0xFFFFFFFFu;
    /// An extra test of the key, or null for none.
    bool (*filter)(SpatialKey key, void* user) = nullptr;
    /// Passed to `filter`.
    void* user = nullptr;

    /// Returns whether the item passes the mask and the filter.
    ///
    /// - Fails: never.
    bool Accepts(const Item& item) const
    {
        return item.layer < 32 && (mask & (1u << item.layer)) != 0 && (filter == nullptr || filter(item.key, user));
    }
};

/// The order of a collecting query's results.
enum class Order : u8
{
    Traversal = 0,  ///< The order the traversal found them: deterministic for one tree, but it changes when the tree is
                    ///< rebuilt.
    Ordered,  ///< In sorted order (overlaps by key, ray hits and nearest items by distance then key, pairs by first key
              ///< then second).
};

/// How a ray cast stops.
enum class RayMode : u8
{
    Closest = 0,  ///< The nearest hit (on equal distance, the smaller key).
    Any,          ///< The first hit found, nearest or not (the cheapest: a line-of-sight test).
    All,          ///< Every hit up to the maximum distance.
};

/// One ray or sweep hit: the item's key and the ray parameter (or the sweep's fraction of the motion) where it was met.
///
/// - Thread-safety: thread-compatible plain data.
struct RayHit
{
    /// The item's key.
    SpatialKey key = 0;
    /// The parameter of the hit along the ray, in units of the direction's length; 0 when the ray starts inside.
    f32 t = 0.0f;
};

/// One result of [[spatial::Nearest]].
///
/// - Thread-safety: thread-compatible plain data.
struct NearestHit
{
    /// The item's key.
    SpatialKey key = 0;
    /// The distance from the query point to the item's box (0 when the point is inside).
    f32 distance = 0.0f;
};

/// Two items whose boxes overlap, the smaller key first.
///
/// - Thread-safety: thread-compatible plain data.
struct KeyPair
{
    /// The smaller key.
    SpatialKey a = 0;
    /// The larger key.
    SpatialKey b = 0;
};

/// What one query did.
///
/// - Thread-safety: thread-compatible plain data.
struct QueryStats
{
    /// Nodes whose box was tested.
    u32 nodes_tested = 0;
    /// Items tested exactly (after the node tests).
    u32 items_tested = 0;
    /// Results delivered.
    u32 results = 0;
    /// Whether the visitor stopped the query early.
    bool stopped = false;
    /// Whether the query was refused because its input was not finite (a NaN or an infinite coordinate): it then has no
    /// results.
    bool refused_input = false;
};

namespace detail_query
{

/// Adds a traversal's counts to a query's (an internal helper of the query functions).
///
/// - Param stats: the query's counts.
/// - Param t: the traversal's.
/// - Fails: never.
/// - Ownership: `stats` is borrowed for the call.
inline void Count(QueryStats* stats, const TraversalStats& t)
{
    stats->nodes_tested += t.nodes_tested;
    stats->stopped = stats->stopped || t.stopped;
}

}  // namespace detail_query

/// Visits every item whose box overlaps `box`, in traversal order.
///
/// - Param tree: the structure ([[spatial::StaticBvh]], [[spatial::DynamicBvh]]).
/// - Param box: the query box; a zero-size box is a point.
/// - Param filter: which items count.
/// - Param visit: `bool(const Item&)`, called for each overlapping item that passes the filter; returns false to stop.
/// - Returns: what the query did.
/// - Fails: an empty box finds nothing; a box with a NaN finds nothing and sets `refused_input` (infinite corners are
/// allowed: a box from minus to plus
///   infinity finds everything).
template <class Tree, class Visit>
QueryStats Overlap(const Tree& tree, const Aabb& box, const QueryFilter& filter, Visit&& visit)
{
    QueryStats stats;
    if (detail_query::HasNaN(box))
    {
        stats.refused_input = true;
        return stats;
    }
    if (box.IsEmpty())
    {
        return stats;
    }
    detail_query::Count(&stats, detail_query::Walk(
                                    tree, [&](const Aabb& node)
                                    { return Overlaps(node, box) ? NodeVisit::Descend : NodeVisit::Reject; },
                                    [&](const Item& item)
                                    {
                                        ++stats.items_tested;
                                        if (!Overlaps(item.box, box) || !filter.Accepts(item))
                                        {
                                            return true;
                                        }
                                        ++stats.results;
                                        return static_cast<bool>(visit(item));
                                    }));
    return stats;
}

/// Visits every item whose box overlaps `sphere`, in traversal order.
///
/// - Param tree: the structure.
/// - Param sphere: the query sphere.
/// - Param filter: which items count.
/// - Param visit: `bool(const Item&)`; returns false to stop.
/// - Returns: what the query did.
/// - Fails: a negative radius finds nothing; a sphere whose center is not finite, or whose radius is a NaN, finds
/// nothing and sets `refused_input`.
template <class Tree, class Visit>
QueryStats Overlap(const Tree& tree, const Sphere& sphere, const QueryFilter& filter, Visit&& visit)
{
    QueryStats stats;
    if (!detail_query::Finite(sphere.center) || detail_query::HasNaN(sphere.radius))
    {
        stats.refused_input = true;
        return stats;
    }
    if (sphere.radius < 0.0f)
    {
        return stats;
    }
    detail_query::Count(&stats, detail_query::Walk(
                                    tree, [&](const Aabb& node)
                                    { return Overlaps(node, sphere) ? NodeVisit::Descend : NodeVisit::Reject; },
                                    [&](const Item& item)
                                    {
                                        ++stats.items_tested;
                                        if (!Overlaps(item.box, sphere) || !filter.Accepts(item))
                                        {
                                            return true;
                                        }
                                        ++stats.results;
                                        return static_cast<bool>(visit(item));
                                    }));
    return stats;
}

/// Visits every item whose box may be inside `frustum` (conservative, as [[Frustum::TestAabb]]), in traversal order.
/// Below a node entirely inside the frustum no node is tested; each item's own box still is (one plane test per item).
///
/// - Param tree: the structure.
/// - Param frustum: the view volume.
/// - Param filter: which items count.
/// - Param visit: `bool(const Item&)`; returns false to stop.
/// - Returns: what the query did.
/// - Fails: a frustum with a plane that is not finite finds nothing and sets `refused_input`.
template <class Tree, class Visit>
QueryStats Overlap(const Tree& tree, const Frustum& frustum, const QueryFilter& filter, Visit&& visit)
{
    QueryStats stats;
    for (const Plane& plane : frustum.planes)
    {
        if (!detail_query::Finite(plane.normal) || !IsFinite(plane.d))
        {
            stats.refused_input = true;
            return stats;
        }
    }
    detail_query::Count(&stats, detail_query::Walk(
                                    tree,
                                    [&](const Aabb& node)
                                    {
                                        switch (frustum.TestAabb(node))
                                        {
                                        case Containment::Outside: return NodeVisit::Reject;
                                        case Containment::Inside: return NodeVisit::AcceptAll;
                                        case Containment::Intersects: break;
                                        }
                                        return NodeVisit::Descend;
                                    },
                                    [&](const Item& item)
                                    {
                                        ++stats.items_tested;
                                        if (!Overlaps(frustum, item.box) || !filter.Accepts(item))
                                        {
                                            return true;
                                        }
                                        ++stats.results;
                                        return static_cast<bool>(visit(item));
                                    }));
    return stats;
}

/// Collects the keys of the items an overlap query finds (any shape `Overlap` takes), in the order asked for.
///
/// - Param tree: the structure.
/// - Param shape: an `Aabb`, a `Sphere` or a `Frustum`.
/// - Param filter: which items count.
/// - Param order: `Traversal`, or `Ordered` for ascending keys.
/// - Param out: receives the keys (cleared first); its capacity is reused, so a warm vector makes the query allocate
/// nothing.
/// - Returns: what the query did.
/// - Fails: as `Overlap` for the shape.
/// - Ownership: `out` is borrowed for the call.
template <class Tree, class Shape>
QueryStats OverlapKeys(const Tree& tree, const Shape& shape, const QueryFilter& filter, Order order,
                       Vector<SpatialKey>* out)
{
    out->Clear();
    const QueryStats stats = Overlap(tree, shape, filter,
                                     [&](const Item& item)
                                     {
                                         out->Push(item.key);
                                         return true;
                                     });
    if (order == Order::Ordered)
    {
        Sort(Span<SpatialKey>(out->Data(), out->Size()), [](SpatialKey a, SpatialKey b) { return a < b; });
    }
    return stats;
}

/// Casts a ray and collects what it hits, as `mode` says.
///
/// - Param tree: the structure.
/// - Param ray: the ray ([[Ray::Make]]).
/// - Param max_t: the farthest parameter that counts (in units of the direction's length).
/// - Param filter: which items count.
/// - Param mode: `Closest` (one hit: the nearest, the smaller key on a tie), `Any` (one hit, the first found) or `All`.
/// - Param order: for `All`: `Traversal`, or `Ordered` by distance then key; the other modes give one hit.
/// - Param hits: receives the hits (cleared first); its capacity is reused.
/// - Returns: what the query did; `results` is the number of hits.
/// - Ownership: `hits` is borrowed for the call.
/// - Fails: a ray that is not finite or a NaN `max_t` finds nothing and sets `refused_input` (an infinite `max_t` is
/// allowed: the ray is unbounded); a negative `max_t` finds nothing.
template <class Tree>
QueryStats Raycast(const Tree& tree, const Ray& ray, f32 max_t, const QueryFilter& filter, RayMode mode, Order order,
                   Vector<RayHit>* hits)
{
    hits->Clear();
    QueryStats stats;
    if (!detail_query::Finite(ray.origin) || !detail_query::Finite(ray.direction) || max_t != max_t)
    {
        stats.refused_input = true;
        return stats;
    }
    if (max_t < 0.0f)
    {
        return stats;
    }
    RayHit best;
    bool found = false;
    detail_query::Count(&stats,
                        detail_query::WalkRay(tree, ray, max_t,
                                              [&](const Item& item, f32& limit)
                                              {
                                                  ++stats.items_tested;
                                                  f32 t = 0.0f;
                                                  if (!Intersect(ray, item.box, limit, &t) || !filter.Accepts(item))
                                                  {
                                                      return true;
                                                  }
                                                  if (mode == RayMode::All)
                                                  {
                                                      RayHit h;
                                                      h.key = item.key;
                                                      h.t = t;
                                                      hits->Push(h);
                                                      return true;
                                                  }
                                                  if (!found || detail_query::Before(t, item.key, best.t, best.key))
                                                  {
                                                      best.key = item.key;
                                                      best.t = t;
                                                      found = true;
                                                      limit = t;  // a closer hit is all that can still count
                                                  }
                                                  return mode != RayMode::Any;
                                              }));
    if (mode == RayMode::All)
    {
        if (order == Order::Ordered)
        {
            Sort(Span<RayHit>(hits->Data(), hits->Size()),
                 [](const RayHit& x, const RayHit& y) { return detail_query::Before(x.t, x.key, y.t, y.key); });
        }
    }
    else if (found)
    {
        hits->Push(best);
    }
    stats.stopped = stats.stopped && mode != RayMode::Any;  // an Any cast stopping at its hit is its normal end
    stats.results = static_cast<u32>(hits->Size());
    return stats;
}

/// Sweeps a box along `motion` and finds the first item it would touch: the item whose box the moving box meets at the
/// smallest fraction of the motion (on a tie, the smaller key). Exact for boxes against boxes (a ray against each box
/// grown by the moving box's half extents).
///
/// - Param tree: the structure.
/// - Param box: the box at the start.
/// - Param motion: the whole movement; zero finds what the box overlaps where it stands.
/// - Param filter: which items count.
/// - Param hit: receives the first contact: the key, and `t` as the fraction of the motion (0 for an item the box
/// already overlaps).
/// - Returns: what the query did; `results` is 1 for a hit, 0 for none.
/// - Fails: an empty box finds nothing; a box or motion that is not finite finds nothing and sets `refused_input`.
/// - Ownership: `hit` is borrowed for the call; it is written only when there is a hit.
template <class Tree>
QueryStats SweepBox(const Tree& tree, const Aabb& box, const Vec3& motion, const QueryFilter& filter, RayHit* hit)
{
    QueryStats stats;
    if ((!detail_query::Finite(box) && !box.IsEmpty()) || !detail_query::Finite(motion))
    {
        stats.refused_input = true;
        return stats;
    }
    if (box.IsEmpty())
    {
        return stats;
    }
    const Vec3 half = box.HalfExtents();
    const bool moves = motion.x != 0.0f || motion.y != 0.0f || motion.z != 0.0f;
    const Ray ray = Ray::Make(box.Center(), moves ? motion : Vec3(1, 0, 0));
    const f32 reach = moves ? 1.0f : 0.0f;
    auto grown = [&](const Aabb& b) { return Aabb::FromMinMax(b.min - half, b.max + half); };
    f32 best_t = reach;
    SpatialKey best_key = 0;
    bool found = false;
    detail_query::Count(
        &stats, detail_query::Walk(
                    tree, [&](const Aabb& node)
                    { return Intersect(ray, grown(node), best_t, nullptr) ? NodeVisit::Descend : NodeVisit::Reject; },
                    [&](const Item& item)
                    {
                        ++stats.items_tested;
                        f32 t = 0.0f;
                        if (!Intersect(ray, grown(item.box), best_t, &t) || !filter.Accepts(item))
                        {
                            return true;
                        }
                        if (!found || detail_query::Before(t, item.key, best_t, best_key))
                        {
                            best_t = t;
                            best_key = item.key;
                            found = true;
                        }
                        return true;
                    }));
    if (found)
    {
        hit->key = best_key;
        hit->t = best_t;
        stats.results = 1;
    }
    return stats;
}

/// Finds the `k` items nearest a point, nearest first (on equal distance, the smaller key): a depth-first search that
/// skips every node farther than the k-th best found so far (or than `max_distance`). The results are kept sorted by
/// insertion, so each candidate costs up to `k` steps: meant for a small `k` (a few dozen); for many results use an
/// overlap query and sort.
///
/// - Param tree: the structure.
/// - Param point: the query point.
/// - Param k: how many items at most; 0 finds nothing.
/// - Param max_distance: items farther than this do not count.
/// - Param filter: which items count.
/// - Param out: receives up to `k` results, always ordered (cleared first); its capacity is reused.
/// - Returns: what the query did.
/// - Ownership: `out` is borrowed for the call.
/// - Fails: a point or distance that is not finite (an infinite `max_distance` is allowed) finds nothing and sets
/// `refused_input`.
template <class Tree>
QueryStats Nearest(const Tree& tree, const Vec3& point, u32 k, f32 max_distance, const QueryFilter& filter,
                   Vector<NearestHit>* out)
{
    out->Clear();
    QueryStats stats;
    if (!detail_query::Finite(point) || max_distance != max_distance)
    {
        stats.refused_input = true;
        return stats;
    }
    if (k == 0 || max_distance < 0.0f)
    {
        return stats;
    }
    // `out` is kept sorted with squared distances while searching (an insertion into a short sorted array: k is small),
    // then turned into distances.
    const f32 max2 = max_distance * max_distance;
    auto bound = [&]() { return out->Size() < k ? max2 : (*out)[out->Size() - 1].distance; };
    detail_query::Count(&stats,
                        detail_query::Walk(
                            tree, [&](const Aabb& node)
                            { return Distance2(point, node) <= bound() ? NodeVisit::Descend : NodeVisit::Reject; },
                            [&](const Item& item)
                            {
                                ++stats.items_tested;
                                const f32 d2 = Distance2(point, item.box);
                                if (d2 > max2 || !filter.Accepts(item))
                                {
                                    return true;
                                }
                                if (out->Size() == k)
                                {
                                    const NearestHit& worst = (*out)[k - 1];
                                    if (!detail_query::Before(d2, item.key, worst.distance, worst.key))
                                    {
                                        return true;
                                    }
                                    out->Pop();
                                }
                                usize at = out->Size();
                                while (at > 0 &&
                                       detail_query::Before(d2, item.key, (*out)[at - 1].distance, (*out)[at - 1].key))
                                {
                                    --at;
                                }
                                NearestHit h;
                                h.key = item.key;
                                h.distance = d2;
                                out->Insert(at, h);
                                return true;
                            }));
    for (usize i = 0; i < out->Size(); ++i)
    {
        (*out)[i].distance = Sqrt((*out)[i].distance);
    }
    stats.results = static_cast<u32>(out->Size());
    return stats;
}

/// Visits every pair of items whose boxes overlap, once each, the smaller key first: a pair counts when one item's
/// layer is in `mask_a` and the other's in `mask_b` (either way round), and both pass the filter's function. An item is
/// never paired with itself. Keys must be unique in the structure.
///
/// - Param tree: the structure.
/// - Param mask_a: the layers of one side.
/// - Param mask_b: the layers of the other side (the same mask pairs a group with itself).
/// - Param filter: its `filter` function applies to both items; its `mask` is not used (the two masks above are).
/// - Param visit: `bool(const Item& a, const Item& b)` with `a.key < b.key`; returns false to stop.
/// - Returns: what the query did; `results` is the number of pairs delivered.
/// - Fails: never.
template <class Tree, class Visit>
QueryStats ForEachPair(const Tree& tree, u32 mask_a, u32 mask_b, const QueryFilter& filter, Visit&& visit)
{
    QueryStats stats;
    auto in = [](u32 mask, const Item& item) { return item.layer < 32 && (mask & (1u << item.layer)) != 0; };
    auto passes = [&](const Item& item) { return filter.filter == nullptr || filter.filter(item.key, filter.user); };
    bool stop = false;
    // For each item of either side, the items its box overlaps with a larger key: each pair is found from its smaller
    // key only.
    detail_query::Count(
        &stats, detail_query::Walk(
                    tree, [](const Aabb&) { return NodeVisit::AcceptAll; },
                    [&](const Item& first)
                    {
                        if ((!in(mask_a, first) && !in(mask_b, first)) || !passes(first))
                        {
                            return true;
                        }
                        const Aabb box = first.box;
                        detail_query::Count(
                            &stats, detail_query::Walk(
                                        tree, [&](const Aabb& node)
                                        { return Overlaps(node, box) ? NodeVisit::Descend : NodeVisit::Reject; },
                                        [&](const Item& second)
                                        {
                                            ++stats.items_tested;
                                            if (second.key <= first.key || !Overlaps(second.box, box))
                                            {
                                                return true;
                                            }
                                            const bool matches = (in(mask_a, first) && in(mask_b, second)) ||
                                                                 (in(mask_b, first) && in(mask_a, second));
                                            if (!matches || !passes(second))
                                            {
                                                return true;
                                            }
                                            ++stats.results;
                                            if (!visit(first, second))
                                            {
                                                stop = true;
                                                return false;
                                            }
                                            return true;
                                        }));
                        return !stop;
                    }));
    stats.stopped = stop;
    return stats;
}

/// Collects the overlapping pairs of `ForEachPair`, in the order asked for.
///
/// - Param tree: the structure.
/// - Param mask_a: the layers of one side.
/// - Param mask_b: the layers of the other side.
/// - Param filter: as for `ForEachPair`.
/// - Param order: `Traversal`, or `Ordered` by first key then second.
/// - Param out: receives the pairs (cleared first); its capacity is reused.
/// - Returns: what the query did.
/// - Fails: never.
/// - Ownership: `out` is borrowed for the call.
template <class Tree>
QueryStats PairKeys(const Tree& tree, u32 mask_a, u32 mask_b, const QueryFilter& filter, Order order,
                    Vector<KeyPair>* out)
{
    out->Clear();
    const QueryStats stats = ForEachPair(tree, mask_a, mask_b, filter,
                                         [&](const Item& a, const Item& b)
                                         {
                                             KeyPair pair;
                                             pair.a = a.key;
                                             pair.b = b.key;
                                             out->Push(pair);
                                             return true;
                                         });
    if (order == Order::Ordered)
    {
        Sort(Span<KeyPair>(out->Data(), out->Size()),
             [](const KeyPair& x, const KeyPair& y) { return x.a < y.a || (x.a == y.a && x.b < y.b); });
    }
    return stats;
}

/// The box overlap of [[spatial::HashGrid]]: through its cells, each item once.
///
/// - Param grid: the grid.
/// - Param box: the query box.
/// - Param filter: which items count.
/// - Param visit: `bool(const Item&)`; returns false to stop.
/// - Returns: what the query did.
/// - Fails: as `Overlap` for a box.
template <class Visit>
QueryStats Overlap(const HashGrid& grid, const Aabb& box, const QueryFilter& filter, Visit&& visit)
{
    QueryStats stats;
    if (detail_query::HasNaN(box))
    {
        stats.refused_input = true;
        return stats;
    }
    if (box.IsEmpty())
    {
        return stats;
    }
    const TraversalStats t = grid.ForEachInRegion(
        box, [&](const Item& item) { return filter.Accepts(item); },
        [&](const Item& item)
        {
            ++stats.results;
            return static_cast<bool>(visit(item));
        });
    stats.nodes_tested = t.nodes_tested;
    stats.items_tested = t.items_visited;
    stats.stopped = t.stopped;
    return stats;
}

/// The sphere overlap of [[spatial::HashGrid]]: through the cells of the sphere's box, each item once.
///
/// - Param grid: the grid.
/// - Param sphere: the query sphere.
/// - Param filter: which items count.
/// - Param visit: `bool(const Item&)`; returns false to stop.
/// - Returns: what the query did.
/// - Fails: as `Overlap` for a sphere.
template <class Visit>
QueryStats Overlap(const HashGrid& grid, const Sphere& sphere, const QueryFilter& filter, Visit&& visit)
{
    QueryStats stats;
    if (!detail_query::Finite(sphere.center) || detail_query::HasNaN(sphere.radius))
    {
        stats.refused_input = true;
        return stats;
    }
    if (sphere.radius < 0.0f)
    {
        return stats;
    }
    const Vec3 r(sphere.radius, sphere.radius, sphere.radius);
    const TraversalStats t = grid.ForEachInRegion(
        Aabb::FromMinMax(sphere.center - r, sphere.center + r),
        [&](const Item& item) { return Overlaps(item.box, sphere) && filter.Accepts(item); },
        [&](const Item& item)
        {
            ++stats.results;
            return static_cast<bool>(visit(item));
        });
    stats.nodes_tested = t.nodes_tested;
    stats.items_tested = t.items_visited;
    stats.stopped = t.stopped;
    return stats;
}

/// The box overlap of [[spatial::SpatialSet]]: the static parts and the dynamic side (through a grid's cells when it is
/// one), each item once.
///
/// - Param set: the set.
/// - Param box: the query box.
/// - Param filter: which items count.
/// - Param visit: `bool(const Item&)`; returns false to stop.
/// - Returns: what the query did.
/// - Fails: as `Overlap` for a box.
template <class Visit>
QueryStats Overlap(const SpatialSet& set, const Aabb& box, const QueryFilter& filter, Visit&& visit)
{
    QueryStats stats;
    if (detail_query::HasNaN(box))
    {
        stats.refused_input = true;
        return stats;
    }
    if (box.IsEmpty())
    {
        return stats;
    }
    const TraversalStats t = set.ForEachInRegion(
        box, [&](const Item& item) { return filter.Accepts(item); },
        [&](const Item& item)
        {
            ++stats.results;
            return static_cast<bool>(visit(item));
        });
    stats.nodes_tested = t.nodes_tested;
    stats.items_tested = t.items_visited;
    stats.stopped = t.stopped;
    return stats;
}

/// The sphere overlap of [[spatial::SpatialSet]], as its box overlap with the sphere's box and the exact sphere test.
///
/// - Param set: the set.
/// - Param sphere: the query sphere.
/// - Param filter: which items count.
/// - Param visit: `bool(const Item&)`; returns false to stop.
/// - Returns: what the query did.
/// - Fails: as `Overlap` for a sphere.
template <class Visit>
QueryStats Overlap(const SpatialSet& set, const Sphere& sphere, const QueryFilter& filter, Visit&& visit)
{
    QueryStats stats;
    if (!detail_query::Finite(sphere.center) || detail_query::HasNaN(sphere.radius))
    {
        stats.refused_input = true;
        return stats;
    }
    if (sphere.radius < 0.0f)
    {
        return stats;
    }
    const Vec3 r(sphere.radius, sphere.radius, sphere.radius);
    const TraversalStats t = set.ForEachInRegion(
        Aabb::FromMinMax(sphere.center - r, sphere.center + r),
        [&](const Item& item) { return Overlaps(item.box, sphere) && filter.Accepts(item); },
        [&](const Item& item)
        {
            ++stats.results;
            return static_cast<bool>(visit(item));
        });
    stats.nodes_tested = t.nodes_tested;
    stats.items_tested = t.items_visited;
    stats.stopped = t.stopped;
    return stats;
}

/// The all-pairs query of [[spatial::HashGrid]]: through its cells, each pair once (the smaller key first), with the
/// masks and filter of `ForEachPair`.
///
/// - Param grid: the grid.
/// - Param mask_a: the layers of one side.
/// - Param mask_b: the layers of the other side.
/// - Param filter: its function applies to both items; its mask is not used.
/// - Param visit: `bool(const Item& a, const Item& b)`; returns false to stop.
/// - Returns: what the query did.
/// - Fails: never.
template <class Visit>
QueryStats ForEachPair(const HashGrid& grid, u32 mask_a, u32 mask_b, const QueryFilter& filter, Visit&& visit)
{
    QueryStats stats;
    auto in = [](u32 mask, const Item& item) { return item.layer < 32 && (mask & (1u << item.layer)) != 0; };
    auto passes = [&](const Item& item) { return filter.filter == nullptr || filter.filter(item.key, filter.user); };
    const TraversalStats t = grid.ForEachOverlappingPair(
        [&](const Item& a, const Item& b)
        {
            const bool matches = (in(mask_a, a) && in(mask_b, b)) || (in(mask_b, a) && in(mask_a, b));
            if (!matches || !passes(a) || !passes(b))
            {
                return true;
            }
            ++stats.results;
            return static_cast<bool>(visit(a, b));
        });
    stats.nodes_tested = t.nodes_tested;
    stats.items_tested = t.items_visited;
    stats.stopped = t.stopped;
    return stats;
}

}  // namespace engine_placeholder::spatial
