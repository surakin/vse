#pragma once

//! The spatial set (Phase 18 Day 242): what systems use. One static tree for what does not move, plus a dynamic tree or
//! a hash grid for what does, behind the one query vocabulary of `spatial/query.h`; an item keeps its handle when it
//! moves between the two sides.

#include "containers/span.h"
#include "containers/vector.h"
#include "math/geometry.h"
#include "spatial/dynamic_bvh.h"
#include "spatial/hash_grid.h"
#include "spatial/spatial.h"
#include "spatial/static_bvh.h"
#include "types.h"

namespace engine_placeholder::spatial
{

/// The structure a set uses for its moving items.
enum class DynamicKind : u8
{
    Bvh = 0,  ///< A [[spatial::DynamicBvh]]: items of any size; the default.
    Grid,     ///< A [[spatial::HashGrid]]: many items of about one size (crowds, particles); faster pairs and updates,
              ///< slower rays.
};

/// How a [[spatial::SpatialSet]] is made.
///
/// - Thread-safety: thread-compatible plain data.
struct SpatialSetConfig
{
    /// The moving side's structure.
    DynamicKind dynamic = DynamicKind::Bvh;
    /// The dynamic tree's margins and rebuild threshold (for `Bvh`).
    DynamicBvhConfig bvh;
    /// The grid's cell edge (for `Grid`): 2 to 4 times the items' size.
    f32 grid_cell_size = 4.0f;
};

/// A handle to an item of a [[spatial::SpatialSet]] (a slot and a generation, like [[spatial::ProxyId]]); it stays the
/// same when the item moves between the static and the dynamic side.
using SetHandle = u32;

/// The handle no item has.
inline constexpr SetHandle kInvalidSetHandle = 0xFFFFFFFFu;

/// What a [[spatial::SpatialSet]] holds and what it has done.
///
/// - Thread-safety: thread-compatible plain data.
struct SpatialSetStats
{
    u32 static_items = 0;    ///< Live static items, in the tree or waiting.
    u32 pending_static = 0;  ///< Static items added (or made static) since the last `RebuildStatic`: queries scan them.
    u32 stale_tree_entries = 0;    ///< Entries of the static tree whose item has since been removed or made dynamic
                                   ///< (skipped by queries).
    u32 dynamic_items = 0;         ///< Live dynamic items.
    u64 static_rebuilds = 0;       ///< Calls of `RebuildStatic`.
    u64 static_moves_refused = 0;  ///< `Move` calls on a static item (refused: make it dynamic first).
    u64 stale_refused = 0;         ///< Handles refused because their item was gone.
};

/// One static [[spatial::StaticBvh]] plus one dynamic structure ([[spatial::DynamicBvh]] or [[spatial::HashGrid]]),
/// answering every query of `spatial/query.h` over both.
///
/// Static items go into the static tree at the next `RebuildStatic`; until then queries scan them from a short pending
/// list, and tree entries of items removed or made dynamic are skipped, so every query is exact at every moment.
/// `MakeDynamic` and `MakeStatic` move an item across (a door that was baked as static and now opens) without changing
/// its handle. A visitor of the composed traversal sees the static tree first, then the pending static items, then the
/// dynamic side.
///
/// - Thread-safety: thread-compatible. Changes need exclusive access; the const queries may run on any number of
/// threads at once between changes.
/// - Ownership: owns its structures and items.
class SpatialSet
{
public:
    /// Makes an empty set.
    ///
    /// - Param config: the dynamic side's kind and settings; copied.
    /// - Aborts: `dynamic` is `Grid` and the grid cell size is not above 0 and finite (it is not checked for a tree).
    explicit SpatialSet(const SpatialSetConfig& config = SpatialSetConfig());
    SpatialSet(const SpatialSet&) = delete;
    SpatialSet& operator=(const SpatialSet&) = delete;

    /// Adds an item that does not move; it joins the static tree at the next `RebuildStatic`.
    ///
    /// - Param box: its box; not empty, corners finite.
    /// - Param key: the caller's key.
    /// - Param layer: the caller's layer (a bit index 0 to 31 for the query masks).
    /// - Returns: its handle, or [[spatial::kInvalidSetHandle]] when the set holds [[spatial::kMaxDynamicProxies]]
    /// items.
    /// - Aborts: `box` is empty or not finite.
    SetHandle AddStatic(const Aabb& box, SpatialKey key, u32 layer);

    /// Adds an item that moves.
    ///
    /// - Param box: its box; not empty, corners finite.
    /// - Param key: the caller's key.
    /// - Param layer: the caller's layer.
    /// - Returns: its handle, or [[spatial::kInvalidSetHandle]] when the set (or its dynamic structure) is full.
    /// - Aborts: `box` is empty or not finite.
    SetHandle AddDynamic(const Aabb& box, SpatialKey key, u32 layer);

    /// Removes an item.
    ///
    /// - Param handle: its handle.
    /// - Returns: false for a handle whose item is gone (counted).
    /// - Fails: as the return says.
    bool Remove(SetHandle handle);

    /// Gives a dynamic item a new box.
    ///
    /// - Param handle: its handle.
    /// - Param box: the new box; not empty, corners finite.
    /// - Returns: whether the dynamic structure changed; false for a stale handle, and for a static item (refused and
    /// counted: make it dynamic first).
    /// - Aborts: `box` is empty or not finite.
    bool Move(SetHandle handle, const Aabb& box);

    /// Makes a static item dynamic (it leaves the static side at once; its tree entry is skipped until the next
    /// `RebuildStatic`).
    ///
    /// - Param handle: its handle.
    /// - Returns: false for a stale handle; true when the item is now dynamic (also when it already was).
    /// - Fails: as the return says.
    bool MakeDynamic(SetHandle handle);

    /// Makes a dynamic item static (it waits in the pending list until the next `RebuildStatic`).
    ///
    /// - Param handle: its handle.
    /// - Returns: false for a stale handle; true when the item is now static (also when it already was).
    /// - Fails: as the return says.
    bool MakeStatic(SetHandle handle);

    /// Rebuilds the static tree from the live static items, in slot order (the same items give the same tree), and
    /// empties the pending list.
    void RebuildStatic();

    /// Improves the dynamic tree a little ([[spatial::DynamicBvh::Optimize]]); does nothing for a grid.
    ///
    /// - Param budget: about how many leaves one call rebuilds.
    void Optimize(u32 budget);

    /// Removes every item; every handle becomes stale.
    void Clear();

    /// Returns whether a `RebuildStatic` would change what the static tree holds: static items wait in the pending
    /// list, or tree entries are stale. Constant time.
    ///
    /// - Fails: never.
    bool NeedsRebuild() const { return !pending_.Empty() || stale_tree_ != 0; }
    /// Returns whether `handle` names a live item.
    ///
    /// - Fails: never; a stale or invalid handle gives false.
    bool IsLive(SetHandle handle) const;
    /// Returns whether a live item is static.
    ///
    /// - Aborts: `handle` is not live.
    bool IsStatic(SetHandle handle) const;
    /// Returns a live item (its box, key and layer).
    ///
    /// - Aborts: `handle` is not live.
    /// - Ownership: the reference is valid until the next `AddStatic`, `AddDynamic` or `Clear`.
    const Item& ItemOf(SetHandle handle) const;
    /// Returns the number of live items.
    u32 ItemCount() const { return count_; }
    /// Returns the dynamic side's kind.
    DynamicKind Kind() const { return config_.dynamic; }
    /// Returns the counts.
    SpatialSetStats Stats() const;
    /// The static tree (for statistics and debug drawing).
    /// - Ownership: valid for the life of the set.
    const StaticBvh& StaticTree() const { return static_; }
    /// The dynamic tree, empty when the dynamic side is a grid.
    /// - Ownership: valid for the life of the set.
    const DynamicBvh& DynamicTree() const { return dynamic_; }
    /// The grid, empty when the dynamic side is a tree.
    /// - Ownership: valid for the life of the set.
    const HashGrid& Grid() const { return grid_; }

    /// Visits the items as [[spatial::StaticBvh::Traverse]] does, over the static tree, the pending static items and
    /// the dynamic side in that order.
    ///
    /// - Param node_test: `NodeVisit(const Aabb&)`.
    /// - Param item_visit: `bool(SetHandle, const Item&)`; returns false to stop.
    /// - Returns: what the traversal did.
    /// - Fails: never.
    template <class NodeTest, class ItemVisit>
    TraversalStats Traverse(NodeTest&& node_test, ItemVisit&& item_visit) const;

    /// Visits the items a ray reaches over all three parts, as [[spatial::StaticBvh::TraverseRay]] does; a `max_t`
    /// lowered by the visitor carries over.
    ///
    /// - Param ray: the ray.
    /// - Param max_t: the farthest parameter to consider.
    /// - Param item_visit: `bool(SetHandle, const Item&, f32& max_t)`; may lower `max_t`, returns false to stop.
    /// - Returns: what the traversal did.
    /// - Fails: never.
    template <class ItemVisit> TraversalStats TraverseRay(const Ray& ray, f32 max_t, ItemVisit&& item_visit) const;

    /// Visits each item whose box overlaps `region` and passes `exact`, once each, using the grid's cells when the
    /// dynamic side is a grid.
    ///
    /// - Param region: the box to look in.
    /// - Param exact: `bool(const Item&)`, the query's own test (it must imply an overlap with `region`).
    /// - Param visit: `bool(const Item&)`; returns false to stop.
    /// - Returns: what the walk did.
    /// - Fails: never.
    template <class Exact, class Visit>
    TraversalStats ForEachInRegion(const Aabb& region, Exact&& exact, Visit&& visit) const;

    /// As [[spatial::SpatialSet::ForEachInRegion]] with no extra test, passing each item's handle too (what the pair
    /// tracker needs).
    ///
    /// - Param region: the box to look in.
    /// - Param visit: `bool(SetHandle, const Item&)`; returns false to stop.
    /// - Returns: what the walk did.
    /// - Fails: never.
    template <class Visit> TraversalStats ForEachHandleInRegion(const Aabb& region, Visit&& visit) const;

    /// As the one-region form with a region of its own for each side: the static items (tree and pending list) are
    /// looked for in `static_region` and the dynamic items in `dynamic_region`. The pair tracker uses it so that one
    /// huge static item, whose margin is large, does not make every search of a dynamic item reach far among the
    /// dynamic ones.
    ///
    /// - Param static_region: the box to look in for static items.
    /// - Param dynamic_region: the box to look in for dynamic items.
    /// - Param visit: `bool(SetHandle, const Item&)`; returns false to stop.
    /// - Returns: what the walk did.
    /// - Fails: never.
    template <class Visit>
    TraversalStats ForEachHandleInRegion(const Aabb& static_region, const Aabb& dynamic_region, Visit&& visit) const;

private:
    static constexpr u32 kNull = 0xFFFFFFFFu;
    static constexpr u32 kLiveBit = 0x100u;
    static constexpr u32 kStaticBit = 0x200u;
    static constexpr u32 kInTreeBit = 0x400u;
    struct Slot
    {
        Item item;
        ProxyId proxy = kInvalidProxy;  // the dynamic structure's handle, for a dynamic item
        u32 state = 0;                  // the generation in the low byte, kLiveBit, kStaticBit, kInTreeBit
        u32 next_free = kNull;
        u32 Generation() const { return state & 0xFFu; }
        bool Live() const { return (state & kLiveBit) != 0; }
        bool Static() const { return (state & kStaticBit) != 0; }
        bool InTree() const { return (state & kInTreeBit) != 0; }
    };

    i32 SlotOf(SetHandle handle) const;
    SetHandle HandleOf(u32 slot) const { return slot | (slots_[slot].Generation() << 24); }
    u32 TakeSlot(const Aabb& box, SpatialKey key, u32 layer);
    bool InsertDynamic(u32 slot);
    void RemoveDynamic(u32 slot);
    void RemovePending(u32 slot);
    // Whether a static tree entry (keyed by slot) still stands for a live item in the tree.
    bool TreeEntryLive(SpatialKey slot) const
    {
        return slot < slots_.Size() && slots_[static_cast<usize>(slot)].Live() &&
               slots_[static_cast<usize>(slot)].InTree();
    }

    SpatialSetConfig config_;
    StaticBvh static_;
    DynamicBvh dynamic_;
    HashGrid grid_;
    Vector<Slot> slots_;
    Vector<u32> pending_;
    Vector<Item> build_items_;
    u32 free_slot_ = kNull;
    u32 count_ = 0;
    u64 static_rebuilds_ = 0;
    u64 static_moves_refused_ = 0;
    u64 stale_ = 0;
    u32 stale_tree_ = 0;  // tree entries whose item was removed or made dynamic since the last rebuild
};

template <class NodeTest, class ItemVisit>
TraversalStats SpatialSet::Traverse(NodeTest&& node_test, ItemVisit&& item_visit) const
{
    TraversalStats total;
    bool stop = false;
    auto add = [&](const TraversalStats& s)
    {
        total.nodes_tested += s.nodes_tested;
        total.items_visited += s.items_visited;
        total.refused += s.refused;
    };
    // The structures store the slot as the key; the visitor sees the caller's item.
    add(static_.Traverse(node_test,
                         [&](const Item& entry)
                         {
                             if (!TreeEntryLive(entry.key))
                             {
                                 return true;
                             }
                             const u32 slot = static_cast<u32>(entry.key);
                             if (!item_visit(HandleOf(slot), slots_[slot].item))
                             {
                                 stop = true;
                                 return false;
                             }
                             return true;
                         }));
    for (usize i = 0; i < pending_.Size() && !stop; ++i)
    {
        const Slot& s = slots_[pending_[i]];
        ++total.nodes_tested;
        if (node_test(s.item.box) == NodeVisit::Reject)
        {
            continue;
        }
        ++total.items_visited;
        if (!item_visit(HandleOf(pending_[i]), s.item))
        {
            stop = true;
        }
    }
    if (!stop)
    {
        auto dynamic_visit = [&](ProxyId, const Item& entry)
        {
            const u32 slot = static_cast<u32>(entry.key);
            if (!item_visit(HandleOf(slot), slots_[slot].item))
            {
                stop = true;
                return false;
            }
            return true;
        };
        add(config_.dynamic == DynamicKind::Bvh ? dynamic_.Traverse(node_test, dynamic_visit)
                                                : grid_.Traverse(node_test, dynamic_visit));
    }
    total.stopped = stop;
    return total;
}

template <class ItemVisit>
TraversalStats SpatialSet::TraverseRay(const Ray& ray, f32 max_t, ItemVisit&& item_visit) const
{
    TraversalStats total;
    bool stop = false;
    f32 limit = max_t;  // shared by the three parts: a closer hit found in one shortens the ray for the next
    auto add = [&](const TraversalStats& s)
    {
        total.nodes_tested += s.nodes_tested;
        total.items_visited += s.items_visited;
        total.refused += s.refused;
    };
    add(static_.TraverseRay(ray, limit,
                            [&](const Item& entry, f32& t)
                            {
                                if (!TreeEntryLive(entry.key))
                                {
                                    return true;
                                }
                                const u32 slot = static_cast<u32>(entry.key);
                                const bool go_on = item_visit(HandleOf(slot), slots_[slot].item, limit);
                                t = limit;
                                stop = !go_on;
                                return go_on;
                            }));
    for (usize i = 0; i < pending_.Size() && !stop; ++i)
    {
        const Slot& s = slots_[pending_[i]];
        ++total.nodes_tested;
        if (!Intersect(ray, s.item.box, limit, nullptr))
        {
            continue;
        }
        ++total.items_visited;
        stop = !item_visit(HandleOf(pending_[i]), s.item, limit);
    }
    if (!stop)
    {
        auto dynamic_visit = [&](ProxyId, const Item& entry, f32& t)
        {
            const u32 slot = static_cast<u32>(entry.key);
            const bool go_on = item_visit(HandleOf(slot), slots_[slot].item, limit);
            t = limit;
            stop = !go_on;
            return go_on;
        };
        add(config_.dynamic == DynamicKind::Bvh ? dynamic_.TraverseRay(ray, limit, dynamic_visit)
                                                : grid_.TraverseRay(ray, limit, dynamic_visit));
    }
    total.stopped = stop;
    return total;
}

template <class Exact, class Visit>
TraversalStats SpatialSet::ForEachInRegion(const Aabb& region, Exact&& exact, Visit&& visit) const
{
    return ForEachHandleInRegion(region, [&](SetHandle, const Item& item) { return !exact(item) || visit(item); });
}

template <class Visit> TraversalStats SpatialSet::ForEachHandleInRegion(const Aabb& region, Visit&& visit) const
{
    return ForEachHandleInRegion(region, region, visit);
}

template <class Visit>
TraversalStats SpatialSet::ForEachHandleInRegion(const Aabb& static_region, const Aabb& dynamic_region,
                                                 Visit&& visit) const
{
    // The static parts through the tree and the pending list; the dynamic side through its tree or its grid's cells.
    TraversalStats total = static_.Traverse(
        [&](const Aabb& node) { return Overlaps(node, static_region) ? NodeVisit::Descend : NodeVisit::Reject; },
        [&](const Item& entry)
        {
            if (!TreeEntryLive(entry.key))
            {
                return true;
            }
            const u32 slot = static_cast<u32>(entry.key);
            const Item& item = slots_[slot].item;
            return !Overlaps(item.box, static_region) || visit(HandleOf(slot), item);
        });
    if (total.stopped)
    {
        return total;
    }
    for (usize i = 0; i < pending_.Size(); ++i)
    {
        const Item& item = slots_[pending_[i]].item;
        ++total.nodes_tested;
        if (Overlaps(item.box, static_region) && !visit(HandleOf(pending_[i]), item))
        {
            total.stopped = true;
            return total;
        }
    }
    if (config_.dynamic == DynamicKind::Bvh)
    {
        const TraversalStats tree = dynamic_.Traverse(
            [&](const Aabb& node) { return Overlaps(node, dynamic_region) ? NodeVisit::Descend : NodeVisit::Reject; },
            [&](ProxyId, const Item& entry)
            {
                const u32 slot = static_cast<u32>(entry.key);
                const Item& item = slots_[slot].item;
                return !Overlaps(item.box, dynamic_region) || visit(HandleOf(slot), item);
            });
        total.nodes_tested += tree.nodes_tested;
        total.items_visited += tree.items_visited;
        total.refused += tree.refused;
        total.stopped = tree.stopped;
        return total;
    }
    const TraversalStats grid = grid_.ForEachInRegion(
        dynamic_region, [](const Item&) { return true; },
        [&](const Item& entry)
        {
            const u32 slot = static_cast<u32>(entry.key);
            return visit(HandleOf(slot), slots_[slot].item);
        });
    total.nodes_tested += grid.nodes_tested;
    total.items_visited += grid.items_visited;
    total.stopped = grid.stopped;
    return total;
}

}  // namespace engine_placeholder::spatial
