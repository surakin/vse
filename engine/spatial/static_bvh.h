#pragma once

//! The static bounding-volume hierarchy: for items that do not move, built once top-down with the
//! binned surface-area heuristic and queried a great deal. Generic over a 64-bit key; it knows nothing of the ECS.

#include "containers/array.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "math/geometry.h"
#include "spatial/spatial.h"
#include "types.h"

namespace vse::spatial
{

/// One item of a spatial structure: its box, the caller's key and a layer (a bit index or a mask, the caller's choice;
/// queries filter by it).
///
/// - Thread-safety: thread-compatible plain data.
struct Item
{
    /// The item's box; may have zero size, must not be empty, and its corners must be finite.
    Aabbd box;
    /// The caller's key (an entity id, an index); never interpreted.
    SpatialKey key = 0;
    /// The caller's layer value.
    u32 layer = 0;
};

/// What a traversal does with a node, as a node test answers it.
enum class NodeVisit : u8
{
    Reject = 0,  ///< Skip the node and everything below it.
    Descend,     ///< Test the children (or, at a leaf, hand each item to the item visitor).
    AcceptAll,   ///< Hand every item below the node to the item visitor without testing anything else below (a box
                 ///< entirely inside a query).
};

/// What one traversal did: the node and item counts, and how many subtrees it had to skip because its fixed stack was
/// full.
///
/// - Thread-safety: thread-compatible plain data.
struct TraversalStats
{
    /// Nodes whose box was tested.
    u32 nodes_tested = 0;
    /// Items handed to the item visitor.
    u32 items_visited = 0;
    /// Subtrees not visited because the stack of [[spatial::kTraversalStackDepth]] entries was full (0 for every tree
    /// the build makes).
    u32 refused = 0;
    /// Whether the item visitor stopped the traversal early.
    bool stopped = false;
};

/// The depth of a traversal's fixed stack; the build keeps every tree within it.
inline constexpr u32 kTraversalStackDepth = 64;

/// The most items a leaf holds.
inline constexpr u32 kStaticBvhLeafSize = 4;

/// The number of bins of the surface-area heuristic along a node's longest centroid axis.
inline constexpr u32 kStaticBvhBins = 16;

/// A static bounding-volume hierarchy over [[spatial::Item]]s.
///
/// `Build` makes the tree top-down: each node is split by the binned surface-area heuristic on its longest centroid
/// axis (16 bins), down to leaves of at most 4 items; nodes whose items share one centroid, or that would make the tree
/// deeper than its traversal stack, are split in the middle instead, so the depth stays within
/// [[spatial::kTraversalStackDepth]] for any input. Nodes are 32 bytes in one array in depth-first order (a node's left
/// child is the next node), and the items are kept in leaf order, so the items of any subtree are one contiguous range.
/// The same items in the same order always build the same tree: every tie is broken by the items' order, never by an
/// address.
///
/// - Thread-safety: thread-compatible. `Build` and `Clear` need exclusive access; the const traversals may run on any
/// number of threads at once.
/// - Ownership: owns its nodes and its copy of the items; `Build` copies the span and keeps nothing of it.
class StaticBvh
{
public:
    /// One node: its box, and two numbers whose meaning depends on the kind (use the accessors).
    ///
    /// - Thread-safety: thread-compatible plain data.
    struct Node
    {
        /// The box enclosing every item below the node.
        Aabbd box;
        /// A leaf: the first item. An inner node: the index of the right child (the left one is the next node).
        u32 a = 0;
        /// A leaf: the item count with [[spatial::StaticBvh::kLeafBit]] set. An inner node: the number of items below
        /// it.
        u32 b = 0;

        /// Returns whether the node is a leaf.
        ///
        /// - Fails: never.
        bool IsLeaf() const { return (b & kLeafBit) != 0; }
        /// Returns the number of items below the node.
        ///
        /// - Fails: never.
        u32 ItemCount() const { return b & ~kLeafBit; }
    };
    /// The bit of [[spatial::StaticBvh::Node::b]] that marks a leaf.
    static constexpr u32 kLeafBit = 0x80000000u;

    StaticBvh() = default;
    StaticBvh(const StaticBvh&) = delete;
    StaticBvh& operator=(const StaticBvh&) = delete;

    /// Builds the tree over `items`, replacing any earlier tree.
    ///
    /// - Param items: the items; copied. An empty span gives an empty tree.
    /// - Aborts: more than 2^31 - 1 items, or an item whose box is empty or not finite (a NaN or infinite corner).
    void Build(Span<const Item> items);

    /// Removes every node and item.
    void Clear();

    /// Returns the number of nodes (0 for an empty tree).
    u32 NodeCount() const { return static_cast<u32>(nodes_.Size()); }
    /// Returns the number of items.
    u32 ItemCount() const { return static_cast<u32>(items_.Size()); }
    /// Returns the number of levels: 1 for a tree of one leaf, 0 for an empty tree.
    u32 Depth() const { return depth_; }
    /// Returns the box of the whole tree, or [[AabbT::Empty]] when there are no items.
    Aabbd Bounds() const { return nodes_.Size() != 0 ? nodes_[0].box : Aabbd::Empty(); }
    /// Returns the nodes in depth-first order (node 0 is the root).
    Span<const Node> Nodes() const { return Span<const Node>(nodes_.Data(), nodes_.Size()); }
    /// Returns the items in leaf order.
    Span<const Item> Items() const { return Span<const Item>(items_.Data(), items_.Size()); }

    /// Returns the first item index of the subtree under `node` (its items are this index and the next `ItemCount()`).
    ///
    /// - Param node: a node index below [[spatial::StaticBvh::NodeCount]].
    /// - Aborts: `node` is out of range.
    u32 FirstItem(u32 node) const;

    /// Returns the tree's surface-area-heuristic cost, a quality measure: the expected number of node and item tests of
    /// a query, per the root's area (1 per inner node and 1 per item of a leaf, each weighted by its box's area over
    /// the root's). Lower is better; 0 for an empty tree.
    f64 SahCost() const;

    /// Visits the tree depth first with an explicit stack: `node_test(const Aabbd&)` returns a [[spatial::NodeVisit]]
    /// for each node reached, and `item_visit(const Item&)` is called for each item of an accepted leaf or subtree; it
    /// returns false to stop.
    ///
    /// - Param node_test: the node test.
    /// - Param item_visit: the item visitor.
    /// - Returns: what the traversal did.
    /// - Fails: never; a subtree that does not fit the stack is skipped and counted in `refused` (no tree `Build` makes
    /// has one).
    template <class NodeTest, class ItemVisit>
    TraversalStats Traverse(NodeTest&& node_test, ItemVisit&& item_visit) const;

    /// Visits the leaves a ray reaches, nearest child first: `item_visit(const Item&, f64& max_t)` is called for each
    /// item of each leaf whose box the ray enters before `max_t`; it may lower `max_t` (a closest-hit search) and
    /// returns false to stop.
    ///
    /// - Param ray: the ray ([[RayT::Make]]).
    /// - Param max_t: the farthest parameter to consider.
    /// - Param item_visit: the item visitor.
    /// - Returns: what the traversal did.
    /// - Fails: never; as [[spatial::StaticBvh::Traverse]].
    template <class ItemVisit> TraversalStats TraverseRay(const Rayd& ray, f64 max_t, ItemVisit&& item_visit) const;

private:
    u32 BuildNode(u32 begin, u32 end, u32 level);

    Vector<Node> nodes_;
    Vector<Item> items_;
    // Build scratch: the item indices being partitioned, their centroids, a buffer for the stable partition and the
    // bins.
    Vector<u32> order_;
    Vector<Vec3d> centroids_;
    Vector<u32> scratch_;
    Vector<u8> bins_;  // the bin of each position of order_ during one node's split
    Span<const Item> source_;
    u32 depth_ = 0;
};

template <class NodeTest, class ItemVisit>
TraversalStats StaticBvh::Traverse(NodeTest&& node_test, ItemVisit&& item_visit) const
{
    TraversalStats stats;
    if (nodes_.Size() == 0)
    {
        return stats;
    }
    Array<u32, kTraversalStackDepth> stack;
    u32 top = 0;
    stack[top++] = 0;
    while (top != 0)
    {
        const u32 index = stack[--top];
        const Node& node = nodes_[index];
        ++stats.nodes_tested;
        const NodeVisit visit = node_test(node.box);
        if (visit == NodeVisit::Reject)
        {
            continue;
        }
        if (visit == NodeVisit::AcceptAll || node.IsLeaf())
        {
            const u32 first = node.IsLeaf() ? node.a : FirstItem(index);
            const u32 count = node.ItemCount();
            for (u32 i = 0; i < count; ++i)
            {
                ++stats.items_visited;
                if (!item_visit(items_[first + i]))
                {
                    stats.stopped = true;
                    return stats;
                }
            }
            continue;
        }
        if (top + 2 > kTraversalStackDepth)
        {
            ++stats.refused;
            continue;
        }
        stack[top++] = node.a;     // right, visited second
        stack[top++] = index + 1;  // left, visited first
    }
    return stats;
}

template <class ItemVisit>
TraversalStats StaticBvh::TraverseRay(const Rayd& ray, f64 max_t, ItemVisit&& item_visit) const
{
    TraversalStats stats;
    if (nodes_.Size() == 0)
    {
        return stats;
    }
    f64 t_root = 0.0f;
    ++stats.nodes_tested;
    if (!Intersect(ray, nodes_[0].box, max_t, &t_root))
    {
        return stats;
    }
    // Each entry carries the parameter at which the ray enters the node, so a node beyond a closer hit found meanwhile
    // is skipped when popped.
    Array<u32, kTraversalStackDepth> stack;
    Array<f64, kTraversalStackDepth> enter;
    u32 top = 0;
    stack[top] = 0;
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
            for (u32 i = 0; i < node.ItemCount(); ++i)
            {
                ++stats.items_visited;
                if (!item_visit(items_[node.a + i], max_t))
                {
                    stats.stopped = true;
                    return stats;
                }
            }
            continue;
        }
        const u32 left = index + 1;
        const u32 right = node.a;
        f64 t_left = 0.0f;
        f64 t_right = 0.0f;
        stats.nodes_tested += 2;
        const bool hit_left = Intersect(ray, nodes_[left].box, max_t, &t_left);
        const bool hit_right = Intersect(ray, nodes_[right].box, max_t, &t_right);
        const u32 pushes = (hit_left ? 1u : 0u) + (hit_right ? 1u : 0u);
        if (top + pushes > kTraversalStackDepth)
        {
            ++stats.refused;
            continue;
        }
        // The nearer child is pushed last, so it is visited first; equal entries keep the left child first.
        if (hit_left && hit_right)
        {
            const bool right_first = t_right < t_left;
            stack[top] = right_first ? left : right;
            enter[top++] = right_first ? t_left : t_right;
            stack[top] = right_first ? right : left;
            enter[top++] = right_first ? t_right : t_left;
        }
        else if (hit_left)
        {
            stack[top] = left;
            enter[top++] = t_left;
        }
        else if (hit_right)
        {
            stack[top] = right;
            enter[top++] = t_right;
        }
    }
    return stats;
}

}  // namespace vse::spatial
