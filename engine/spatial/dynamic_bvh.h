#pragma once

//! The dynamic bounding-volume hierarchy (Phase 18 Day 240): for items that move, updated at a cost proportional to
//! what moved. An incremental tree of fat boxes (Box2D's `b2DynamicTree` design) with a bulk top-down build and an
//! incremental optimizer that keep its quality up under motion.

#include "containers/array.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "math/geometry.h"
#include "spatial/spatial.h"
#include "spatial/static_bvh.h"
#include "types.h"

namespace vse::spatial
{

/// A handle to an item of a [[spatial::DynamicBvh]]: a slot index and a generation, so a handle kept after its item was
/// removed is refused rather than reaching a later item in the same slot (until the 8-bit generation wraps, after 256
/// reuses of one slot).
using ProxyId = u32;

/// The handle no item has.
inline constexpr ProxyId kInvalidProxy = 0xFFFFFFFFu;

/// The most items a [[spatial::DynamicBvh]] holds at once (the slot index has 24 bits).
inline constexpr u32 kMaxDynamicProxies = 0x00FFFFFFu;

/// How a [[spatial::DynamicBvh]] trades update cost against query cost.
///
/// - Thread-safety: thread-compatible plain data.
struct DynamicBvhConfig
{
    /// The fat margin, as a fraction of the item's largest extent: a leaf stores the box grown by this much on every
    /// side, and a move that stays inside it changes nothing (`spatial.fat_margin`; 0.25 from the spatial design spec,
    /// experiment (b)). An item of zero size gets no margin.
    f32 fat_margin = 0.25f;
    /// How far the fat box also reaches along the last movement, as a multiple of that displacement (Box2D's predictive
    /// margin). 0 turns it off; the default is what the Day 240 measurement chose (`docs/guide/spatial.md`, "The
    /// dynamic BVH").
    f32 predictive = 2.0f;
    /// [[spatial::DynamicBvh::Optimize]] rebuilds the whole tree when its SAH cost has grown past this multiple of the
    /// cost after the last full build.
    f32 rebuild_threshold = 1.3f;
};

/// What a [[spatial::DynamicBvh]] holds and what it has done.
///
/// - Thread-safety: thread-compatible plain data.
struct DynamicBvhStats
{
    u32 proxies = 0;        ///< Items in the tree.
    u32 nodes = 0;          ///< Nodes in use (leaves and inner nodes).
    u32 node_capacity = 0;  ///< Nodes in the pool, used and free.
    u32 height = 0;         ///< The root's height: 0 for one leaf, 0 for an empty tree.
    u64 memory_bytes = 0;   ///< Bytes of the node pool and the item slots (their sizes, not their capacities).
    f32 sah_cost = 0.0f;    ///< The SAH cost now, as [[spatial::StaticBvh::SahCost]] weighs it (1 per inner node, 1 per
                            ///< leaf, by area).
    f32 built_sah_cost = 0.0f;  ///< The SAH cost right after the last full build or rebuild.
    u64 moves = 0;              ///< Calls of `Move` with a live handle.
    u64 moves_inside_fat = 0;   ///< Moves that stayed inside the fat box and changed nothing.
    u64 reinserts = 0;          ///< Moves that took the leaf out and put it back.
    u64 rotations = 0;          ///< Rotations made to keep the tree balanced.
    u64 full_rebuilds = 0;      ///< Full top-down rebuilds (`Build`, `Rebuild`, and those `Optimize` chose).
    u64 partial_rebuilds = 0;   ///< Subtrees rebuilt by `Optimize`.
    u64 stale_refused = 0;      ///< Handles refused because their item had been removed (or never existed).
    u64 refused_full = 0;       ///< Inserts refused because the tree held [[spatial::kMaxDynamicProxies]] items.
};

/// A dynamic bounding-volume hierarchy: one leaf per item, holding the item's fat box; inner nodes hold the box of
/// their two children.
///
/// `Insert` puts a leaf beside the sibling that adds the least surface area to the tree (a branch-and-bound search) and
/// rotates nodes on the way up to keep the tree balanced; `Remove` takes it out the same way; `Move` does nothing while
/// the new box stays inside the stored fat box and re-inserts the leaf otherwise. A tree built only by insertion
/// answers queries 1.8 to 3.3 times slower than a top-down build of the same boxes (the spatial design spec, experiment
/// (a)), so `Build` makes a fresh tree top-down (the binned SAH of [[spatial::StaticBvh]], one item per leaf) and
/// `Optimize` rebuilds a small subtree per call and the whole tree when its quality has degraded. Nodes live in one
/// pool with a free list: after the pool has grown to its working size, `Insert`, `Remove`, `Move` and the queries
/// allocate nothing.
///
/// - Thread-safety: thread-compatible. Changes need exclusive access; the const queries may run on any number of
/// threads at once between changes.
/// - Ownership: owns its nodes and items; it keeps no pointer to what it was given.
class DynamicBvh
{
public:
    /// Makes an empty tree.
    ///
    /// - Param config: margins and the rebuild threshold; copied.
    explicit DynamicBvh(const DynamicBvhConfig& config = DynamicBvhConfig());
    DynamicBvh(const DynamicBvh&) = delete;
    DynamicBvh& operator=(const DynamicBvh&) = delete;

    /// Adds an item.
    ///
    /// - Param box: the item's box (tight); must not be empty, its corners finite.
    /// - Param key: the caller's key.
    /// - Param layer: the caller's layer value.
    /// - Returns: the item's handle, or [[spatial::kInvalidProxy]] when the tree is full (counted in `refused_full`).
    /// - Aborts: `box` is empty or not finite.
    ProxyId Insert(const Aabb& box, SpatialKey key, u32 layer);

    /// Removes an item.
    ///
    /// - Param proxy: its handle.
    /// - Returns: false for a handle whose item is gone (counted in `stale_refused`).
    /// - Fails: as the return says.
    bool Remove(ProxyId proxy);

    /// Gives an item a new box.
    ///
    /// - Param proxy: its handle.
    /// - Param box: the new tight box; must not be empty, its corners finite.
    /// - Returns: whether the tree changed (the leaf was re-inserted); false when the box stayed inside the fat box, or
    /// for a stale handle (counted).
    /// - Aborts: `box` is empty or not finite.
    bool Move(ProxyId proxy, const Aabb& box);

    /// Replaces the whole tree with these items, built top-down.
    ///
    /// - Param items: the items; copied.
    /// - Param proxies: receives each item's handle, in the order of `items`; at least as long as `items`.
    /// - Aborts: `proxies` is too short, more than [[spatial::kMaxDynamicProxies]] items, or a box empty or not finite.
    void Build(Span<const Item> items, Span<ProxyId> proxies);

    /// Rebuilds the tree top-down from the items it holds; their handles and fat boxes stay.
    void Rebuild();

    /// Improves the tree a little: rebuilds one subtree of about `budget` leaves (a different one each call), or the
    /// whole tree when its SAH cost has grown past `rebuild_threshold` times the cost after the last full build. Costs
    /// one pass over the nodes to measure the tree, plus the rebuild; returns at once, costing nothing, when nothing
    /// was inserted, removed or re-inserted since the last call (a quiet scene).
    ///
    /// - Param budget: about how many leaves one call rebuilds; 0 only measures.
    void Optimize(u32 budget);

    /// Removes every item and node; every handle becomes stale.
    void Clear();

    /// Returns whether `proxy` names a live item.
    ///
    /// - Fails: never; a stale or invalid handle gives false.
    bool IsLive(ProxyId proxy) const;
    /// Returns the item of a live handle (its tight box, key and layer).
    ///
    /// - Aborts: `proxy` is not live.
    /// - Ownership: the reference is valid until the next `Insert`, `Build` or `Clear` (they may move the item slots).
    const Item& ItemOf(ProxyId proxy) const;
    /// Returns the fat box a live handle's leaf stores.
    ///
    /// - Aborts: `proxy` is not live.
    /// - Ownership: the reference is valid until the next change of the tree (any call that is not const).
    const Aabb& FatBox(ProxyId proxy) const;

    /// Returns the number of items.
    u32 ProxyCount() const { return proxy_count_; }
    /// Returns the root's height (0 for a single leaf and for an empty tree).
    u32 Height() const;
    /// Returns the box of the whole tree (fat boxes), or [[Aabb::Empty]] when it is empty.
    Aabb Bounds() const;
    /// Returns the tree's SAH cost now (one pass over the nodes).
    f32 SahCost() const;
    /// Returns the counts and the quality measures; `sah_cost` is measured now (one pass over the nodes).
    DynamicBvhStats Stats() const;

    /// Checks every invariant: parent and child links agree, each node's box encloses its children and each leaf's fat
    /// box its tight box, heights are right, every live item has exactly one leaf, and the free list holds exactly the
    /// nodes not in the tree.
    ///
    /// - Returns: whether all hold.
    /// - Fails: returns false at the first broken invariant.
    bool Validate() const;

    /// Visits the tree depth first, as [[spatial::StaticBvh::Traverse]]: `node_test(const Aabb&)` sees the inner boxes
    /// and each leaf's fat box and returns a [[spatial::NodeVisit]]; `item_visit(ProxyId, const Item&)` sees each
    /// accepted leaf's item (with its tight box) and returns false to stop.
    ///
    /// - Param node_test: the node test.
    /// - Param item_visit: the item visitor.
    /// - Returns: what the traversal did.
    /// - Fails: never; a subtree that does not fit the stack is skipped and counted in `refused`.
    template <class NodeTest, class ItemVisit>
    TraversalStats Traverse(NodeTest&& node_test, ItemVisit&& item_visit) const;

    /// Visits the leaves a ray reaches, nearest child first, as [[spatial::StaticBvh::TraverseRay]]:
    /// `item_visit(ProxyId, const Item&, f32& max_t)` may lower `max_t` and returns false to stop.
    ///
    /// - Param ray: the ray ([[Ray::Make]]).
    /// - Param max_t: the farthest parameter to consider.
    /// - Param item_visit: the item visitor.
    /// - Returns: what the traversal did.
    /// - Fails: never; as [[spatial::DynamicBvh::Traverse]].
    template <class ItemVisit> TraversalStats TraverseRay(const Ray& ray, f32 max_t, ItemVisit&& item_visit) const;

private:
    static constexpr u32 kNull = 0xFFFFFFFFu;
    struct Node
    {
        Aabb box;            // inner: the children's box; leaf: the item's fat box
        u32 parent = kNull;  // free: the next free node
        u32 child1 = kNull;  // kNull for a leaf
        u32 child2 = kNull;  // leaf: the proxy slot
        i32 height = -1;     // leaf 0; free -1
        bool IsLeaf() const { return child1 == kNull; }
    };
    struct Proxy
    {
        Item item;
        u32 node = kNull;  // free: the next free slot
        u32 state = 0;     // the generation (0-255, bumped on removal) in the low byte, kLiveBit when the slot holds an
                           // item
        u32 Generation() const { return state & 0xFFu; }
        bool Live() const { return (state & kLiveBit) != 0; }
    };
    static constexpr u32 kLiveBit = 0x100u;

    u32 AllocateNode();
    void FreeNode(u32 node);
    void InsertLeaf(u32 leaf);
    void RemoveLeaf(u32 leaf);
    u32 Balance(u32 node);
    void Refit(u32 from);
    i32 SlotOf(ProxyId proxy) const;  // -1 for a stale or invalid handle
    Aabb FatOf(const Aabb& tight, const Vec3& displacement) const;
    void RebuildFrom(bool fat_ready);
    void RebuildSubtree(u32 subtree);
    u32 BuildRange(u32 begin, u32 end, u32 level);

    DynamicBvhConfig config_;
    Vector<Node> nodes_;
    Vector<Proxy> proxies_;
    u32 root_ = kNull;
    u32 free_node_ = kNull;
    u32 free_proxy_ = kNull;
    u32 proxy_count_ = 0;
    u32 optimize_cursor_ = 0;
    u32 changes_ = 0;  // inserts, removes and re-inserts since the last Optimize or full build
    f32 built_cost_ = 0.0f;
    DynamicBvhStats counters_;
    // Build scratch, kept so a rebuild after the first allocates nothing: leaf slots being partitioned, their fat boxes
    // and centroids by slot, partition buffers, and the stack of a subtree walk.
    Vector<u32> order_;
    Vector<Aabb> fat_;
    Vector<Vec3> centroids_;
    Vector<u32> scratch_;
    Vector<u8> bins_;
    Vector<u32> walk_;
};

template <class NodeTest, class ItemVisit>
TraversalStats DynamicBvh::Traverse(NodeTest&& node_test, ItemVisit&& item_visit) const
{
    TraversalStats stats;
    if (root_ == kNull)
    {
        return stats;
    }
    // An entry's high bit says its subtree was accepted whole: its nodes are not tested.
    constexpr u32 kAccepted = 0x80000000u;
    Array<u32, kTraversalStackDepth> stack;
    u32 top = 0;
    stack[top++] = root_;
    while (top != 0)
    {
        const u32 entry = stack[--top];
        const u32 index = entry & ~kAccepted;
        const Node& node = nodes_[index];
        bool accepted = (entry & kAccepted) != 0;
        if (!accepted)
        {
            ++stats.nodes_tested;
            const NodeVisit visit = node_test(node.box);
            if (visit == NodeVisit::Reject)
            {
                continue;
            }
            accepted = visit == NodeVisit::AcceptAll;
        }
        if (node.IsLeaf())
        {
            const Proxy& proxy = proxies_[node.child2];
            ++stats.items_visited;
            const ProxyId id = node.child2 | (proxy.Generation() << 24);
            if (!item_visit(id, proxy.item))
            {
                stats.stopped = true;
                return stats;
            }
            continue;
        }
        if (top + 2 > kTraversalStackDepth)
        {
            ++stats.refused;
            continue;
        }
        const u32 flag = accepted ? kAccepted : 0u;
        stack[top++] = node.child2 | flag;
        stack[top++] = node.child1 | flag;
    }
    return stats;
}

template <class ItemVisit>
TraversalStats DynamicBvh::TraverseRay(const Ray& ray, f32 max_t, ItemVisit&& item_visit) const
{
    TraversalStats stats;
    if (root_ == kNull)
    {
        return stats;
    }
    f32 t_root = 0.0f;
    ++stats.nodes_tested;
    if (!Intersect(ray, nodes_[root_].box, max_t, &t_root))
    {
        return stats;
    }
    Array<u32, kTraversalStackDepth> stack;
    Array<f32, kTraversalStackDepth> enter;
    u32 top = 0;
    stack[top] = root_;
    enter[top++] = t_root;
    while (top != 0)
    {
        --top;
        const u32 index = stack[top];
        if (enter[top] > max_t)
        {
            continue;
        }
        const Node& node = nodes_[index];
        if (node.IsLeaf())
        {
            const Proxy& proxy = proxies_[node.child2];
            ++stats.items_visited;
            if (!item_visit(node.child2 | (proxy.Generation() << 24), proxy.item, max_t))
            {
                stats.stopped = true;
                return stats;
            }
            continue;
        }
        f32 t1 = 0.0f;
        f32 t2 = 0.0f;
        stats.nodes_tested += 2;
        const bool hit1 = Intersect(ray, nodes_[node.child1].box, max_t, &t1);
        const bool hit2 = Intersect(ray, nodes_[node.child2].box, max_t, &t2);
        const u32 pushes = (hit1 ? 1u : 0u) + (hit2 ? 1u : 0u);
        if (top + pushes > kTraversalStackDepth)
        {
            ++stats.refused;
            continue;
        }
        if (hit1 && hit2)
        {
            const bool second_first = t2 < t1;  // the nearer child is pushed last, so it is visited first
            stack[top] = second_first ? node.child1 : node.child2;
            enter[top++] = second_first ? t1 : t2;
            stack[top] = second_first ? node.child2 : node.child1;
            enter[top++] = second_first ? t2 : t1;
        }
        else if (hit1)
        {
            stack[top] = node.child1;
            enter[top++] = t1;
        }
        else if (hit2)
        {
            stack[top] = node.child2;
            enter[top++] = t2;
        }
    }
    return stats;
}

}  // namespace vse::spatial
