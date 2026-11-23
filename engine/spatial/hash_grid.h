#pragma once

//! The hash grid: for many dynamic items of about the same size (crowds, particles). Kept by the
//! spatial design spec's experiment (a): on 100,000 same-size agents the all-pairs query was 2 times faster than the
//! best BVH and an update a quarter of the cost; it loses on items of very different sizes and on rays, so it is
//! opt-in.

#include "containers/array.h"
#include "containers/hash_map.h"
#include "mem/heap_allocator.h"
#include "containers/vector.h"
#include "math/geometry.h"
#include "spatial/dynamic_bvh.h"
#include "spatial/spatial.h"
#include "spatial/static_bvh.h"
#include "types.h"

namespace vse::spatial
{

/// The most cells one item is registered in; an item that would touch more (much larger than a cell) is kept in the
/// grid's list of oversized items, which every query scans.
inline constexpr u32 kMaxCellsPerItem = 64;

/// What a [[spatial::HashGrid]] holds and what it has done.
///
/// - Thread-safety: thread-compatible plain data.
struct HashGridStats
{
    u32 items = 0;  ///< Items in the grid.
    u32 cells = 0;  ///< Cells ever used (a cell stays once made, empty or not, so a moving crowd allocates nothing).
    u32 occupied_cells = 0;    ///< Cells holding at least one item.
    u64 entries = 0;           ///< Item registrations in cells (an item touching 4 cells counts 4).
    u32 oversized = 0;         ///< Items kept in the oversized list.
    u64 moves = 0;             ///< Calls of `Move` with a live handle.
    u64 moves_same_cells = 0;  ///< Moves that stayed in the same cells (only the box changed).
    u64 stale_refused = 0;     ///< Handles refused because their item was gone.
};

/// A uniform grid of cubic cells over all of space, the occupied cells in an open-addressing hash table keyed by their
/// coordinates; each item is registered in every cell its box touches.
///
/// Box and sphere overlaps and the all-pairs query use the cells (each result reported once, from one canonical cell:
/// the one holding the lowest corner of the item's overlap with the query); the other queries of `spatial/query.h`
/// (frustum, rays, sweeps, nearest) scan the items. Choose the cell size 2 to 4 times the items' size: the design spec
/// measured 17.4, 10.3 and 13.4 ms for the all-pairs query of 100,000 agents of 1 x 2 x 1 at cells of 2, 4 and 8.
///
/// - Thread-safety: thread-compatible. Changes need exclusive access; the const queries may run on any number of
/// threads at once between changes.
/// - Ownership: owns its cells and items.
class HashGrid
{
public:
    /// Makes an empty grid.
    ///
    /// - Param cell_size: the edge of a cell, above 0 and finite.
    /// - Aborts: `cell_size` is not above 0 or not finite.
    explicit HashGrid(f64 cell_size);
    HashGrid(const HashGrid&) = delete;
    HashGrid& operator=(const HashGrid&) = delete;

    /// Adds an item.
    ///
    /// - Param box: its box; not empty, corners finite.
    /// - Param key: the caller's key.
    /// - Param layer: the caller's layer value.
    /// - Returns: its handle (the same kind as [[spatial::DynamicBvh]]'s), or [[spatial::kInvalidProxy]] when the grid
    /// holds
    ///   [[spatial::kMaxDynamicProxies]] items.
    /// - Aborts: `box` is empty or not finite.
    ProxyId Insert(const Aabbd& box, SpatialKey key, u32 layer);

    /// Removes an item.
    ///
    /// - Param proxy: its handle.
    /// - Returns: false for a handle whose item is gone (counted).
    /// - Fails: as the return says.
    bool Remove(ProxyId proxy);

    /// Gives an item a new box.
    ///
    /// - Param proxy: its handle.
    /// - Param box: the new box; not empty, corners finite.
    /// - Returns: whether its cells changed (false when it stayed in the same cells, or for a stale handle, counted).
    /// - Aborts: `box` is empty or not finite.
    bool Move(ProxyId proxy, const Aabbd& box);

    /// Removes every item; the cells stay (empty) and every handle becomes stale.
    void Clear();

    /// Returns whether `proxy` names a live item.
    ///
    /// - Fails: never; a stale or invalid handle gives false.
    bool IsLive(ProxyId proxy) const;
    /// Returns a live handle's item.
    ///
    /// - Aborts: `proxy` is not live.
    /// - Ownership: the reference is valid until the next `Insert` or `Clear`.
    const Item& ItemOf(ProxyId proxy) const;
    /// Returns the number of items.
    u32 ProxyCount() const { return count_; }
    /// Returns the cell edge.
    f64 CellSize() const { return cell_size_; }
    /// Returns the counts (one pass over the cells).
    HashGridStats Stats() const;

    /// Visits each item whose box overlaps `region` and passes `exact(const Item&)`, once each, using the cells the
    /// region touches.
    ///
    /// - Param region: the box to look in (for a sphere query, its bounding box).
    /// - Param exact: `bool(const Item&)`, the query's own test (it must imply an overlap with `region`).
    /// - Param visit: `bool(const Item&)`; returns false to stop.
    /// - Returns: what the walk did (`nodes_tested` counts the cells looked at).
    /// - Fails: never.
    template <class Exact, class Visit>
    TraversalStats ForEachInRegion(const Aabbd& region, Exact&& exact, Visit&& visit) const;

    /// Visits every pair of items whose boxes overlap, once each (the smaller key first), using the cells.
    ///
    /// - Param visit: `bool(const Item& a, const Item& b)` with `a.key < b.key`; returns false to stop.
    /// - Returns: what the walk did.
    /// - Fails: never.
    template <class Visit> TraversalStats ForEachOverlappingPair(Visit&& visit) const;

    /// Visits the items as [[spatial::DynamicBvh::Traverse]] does, by scanning them (the node test sees each item's
    /// box): the generic queries use it.
    ///
    /// - Param node_test: `NodeVisit(const Aabbd&)`.
    /// - Param item_visit: `bool(ProxyId, const Item&)`; returns false to stop.
    /// - Returns: what the scan did.
    /// - Fails: never.
    template <class NodeTest, class ItemVisit>
    TraversalStats Traverse(NodeTest&& node_test, ItemVisit&& item_visit) const;

    /// Visits the items a ray reaches, as [[spatial::DynamicBvh::TraverseRay]] does, by scanning them in slot order
    /// (not nearest first).
    ///
    /// - Param ray: the ray.
    /// - Param max_t: the farthest parameter to consider.
    /// - Param item_visit: `bool(ProxyId, const Item&, f64& max_t)`; may lower `max_t`, returns false to stop.
    /// - Returns: what the scan did.
    /// - Fails: never.
    template <class ItemVisit> TraversalStats TraverseRay(const Rayd& ray, f64 max_t, ItemVisit&& item_visit) const;

private:
    static constexpr u32 kNull = 0xFFFFFFFFu;
    static constexpr u32 kLiveBit = 0x100u;
    static constexpr u32 kOversizedBit = 0x200u;
    struct Cell
    {
        Array<i32, 3> at = {};
        Vector<u32> slots;
    };
    struct Slot
    {
        Item item;
        Array<i32, 3> lo = {};
        Array<i32, 3> hi = {};
        u32 state = 0;  // the generation in the low byte, kLiveBit, kOversizedBit
        u32 next_free = kNull;
        u32 Generation() const { return state & 0xFFu; }
        bool Live() const { return (state & kLiveBit) != 0; }
        bool Oversized() const { return (state & kOversizedBit) != 0; }
    };

    i32 CellOf(f64 v) const;
    static u64 KeyOf(i32 x, i32 y, i32 z);
    bool RangeOf(const Aabbd& box, Array<i32, 3>* lo,
                 Array<i32, 3>* hi) const;  // false: too many cells or out of range
    void Register(u32 slot);
    void Unregister(u32 slot);
    i32 SlotOf(ProxyId proxy) const;
    const Cell* FindCell(i32 x, i32 y, i32 z) const;
    ProxyId IdOf(u32 slot) const { return slot | (slots_[slot].Generation() << 24); }

    f64 cell_size_;
    f64 inverse_;
    HashMap<u64, u32, HeapAllocator> cell_index_;
    Vector<Cell> cells_;
    Vector<Slot> slots_;
    Vector<u32> oversized_;
    u32 free_slot_ = kNull;
    u32 count_ = 0;
    u64 moves_ = 0;
    u64 moves_same_ = 0;
    u64 stale_ = 0;
};

template <class Exact, class Visit>
TraversalStats HashGrid::ForEachInRegion(const Aabbd& region, Exact&& exact, Visit&& visit) const
{
    TraversalStats stats;
    if (region.IsEmpty())
    {
        return stats;
    }
    // The canonical cell of an item for this region: the cell of the lowest corner of their overlap. It is in both
    // ranges, so the item is reported from exactly one of the cells visited.
    auto canonical = [&](const Item& item, const Cell& cell)
    {
        for (u32 k = 0; k < 3; ++k)
        {
            const f64 low = item.box.min[k] > region.min[k] ? item.box.min[k] : region.min[k];
            if (CellOf(low) != cell.at[k])
            {
                return false;
            }
        }
        return true;
    };
    auto visit_cell = [&](const Cell& cell)
    {
        ++stats.nodes_tested;
        for (usize i = 0; i < cell.slots.Size(); ++i)
        {
            const Item& item = slots_[cell.slots[i]].item;
            if (!Overlaps(item.box, region) || !canonical(item, cell) || !exact(item))
            {
                continue;
            }
            ++stats.items_visited;
            if (!visit(item))
            {
                return false;
            }
        }
        return true;
    };
    const Array<i32, 3> lo = {{CellOf(region.min.x), CellOf(region.min.y), CellOf(region.min.z)}};
    const Array<i32, 3> hi = {{CellOf(region.max.x), CellOf(region.max.y), CellOf(region.max.z)}};
    const u64 span = static_cast<u64>(hi[0] - lo[0] + 1) * static_cast<u64>(hi[1] - lo[1] + 1) *
                     static_cast<u64>(hi[2] - lo[2] + 1);
    if (span > cells_.Size())
    {
        // A region wider than the cells in use: walk the cells instead of the coordinates.
        for (usize c = 0; c < cells_.Size(); ++c)
        {
            const Cell& cell = cells_[c];
            if (cell.at[0] < lo[0] || cell.at[0] > hi[0] || cell.at[1] < lo[1] || cell.at[1] > hi[1] ||
                cell.at[2] < lo[2] || cell.at[2] > hi[2])
            {
                continue;
            }
            if (!visit_cell(cell))
            {
                stats.stopped = true;
                return stats;
            }
        }
    }
    else
    {
        for (i32 z = lo[2]; z <= hi[2]; ++z)
        {
            for (i32 y = lo[1]; y <= hi[1]; ++y)
            {
                for (i32 x = lo[0]; x <= hi[0]; ++x)
                {
                    const Cell* cell = FindCell(x, y, z);
                    if (cell != nullptr && !visit_cell(*cell))
                    {
                        stats.stopped = true;
                        return stats;
                    }
                }
            }
        }
    }
    for (usize i = 0; i < oversized_.Size(); ++i)
    {
        const Item& item = slots_[oversized_[i]].item;
        if (!Overlaps(item.box, region) || !exact(item))
        {
            continue;
        }
        ++stats.items_visited;
        if (!visit(item))
        {
            stats.stopped = true;
            return stats;
        }
    }
    return stats;
}

template <class Visit> TraversalStats HashGrid::ForEachOverlappingPair(Visit&& visit) const
{
    TraversalStats stats;
    auto ordered = [&](const Item& a, const Item& b) { return a.key < b.key ? visit(a, b) : visit(b, a); };
    for (usize c = 0; c < cells_.Size(); ++c)
    {
        const Cell& cell = cells_[c];
        const usize n = cell.slots.Size();
        if (n < 2)
        {
            continue;
        }
        ++stats.nodes_tested;
        for (usize i = 0; i + 1 < n; ++i)
        {
            const Item& a = slots_[cell.slots[i]].item;
            for (usize j = i + 1; j < n; ++j)
            {
                const Item& b = slots_[cell.slots[j]].item;
                ++stats.items_visited;
                if (!Overlaps(a.box, b.box))
                {
                    continue;
                }
                // Reported from the cell of the lowest corner of the two boxes' overlap only.
                bool here = true;
                for (u32 k = 0; k < 3 && here; ++k)
                {
                    here = CellOf(a.box.min[k] > b.box.min[k] ? a.box.min[k] : b.box.min[k]) == cell.at[k];
                }
                if (here && !ordered(a, b))
                {
                    stats.stopped = true;
                    return stats;
                }
            }
        }
    }
    // The oversized items against everything (and each other once). The list position of each oversized slot, made once
    // (container audit Day 8: each oversized pair searched the list for it).
    HashMap<u32, u32, HeapAllocator> oversized_at(HeapAllocator::Default());  // slot -> list position, few entries
    if (oversized_.Size() > 1)
    {
        oversized_at.Reserve(oversized_.Size());
        for (usize i = 0; i < oversized_.Size(); ++i)
        {
            oversized_at.Insert(static_cast<u32>(oversized_[i]), static_cast<u32>(i));
        }
    }
    for (usize i = 0; i < oversized_.Size(); ++i)
    {
        const Item& big = slots_[oversized_[i]].item;
        for (usize s = 0; s < slots_.Size(); ++s)
        {
            const Slot& other = slots_[s];
            if (!other.Live() || s == oversized_[i])
            {
                continue;
            }
            if (other.Oversized())
            {
                // Each oversized pair once: from the one earlier in the list.
                if (*oversized_at.Find(static_cast<u32>(s)) < i)
                {
                    continue;
                }
            }
            ++stats.items_visited;
            if (Overlaps(big.box, other.item.box) && !ordered(big, other.item))
            {
                stats.stopped = true;
                return stats;
            }
        }
    }
    return stats;
}

template <class NodeTest, class ItemVisit>
TraversalStats HashGrid::Traverse(NodeTest&& node_test, ItemVisit&& item_visit) const
{
    TraversalStats stats;
    for (usize s = 0; s < slots_.Size(); ++s)
    {
        const Slot& slot = slots_[s];
        if (!slot.Live())
        {
            continue;
        }
        ++stats.nodes_tested;
        if (node_test(slot.item.box) == NodeVisit::Reject)
        {
            continue;
        }
        ++stats.items_visited;
        if (!item_visit(IdOf(static_cast<u32>(s)), slot.item))
        {
            stats.stopped = true;
            return stats;
        }
    }
    return stats;
}

template <class ItemVisit>
TraversalStats HashGrid::TraverseRay(const Rayd& ray, f64 max_t, ItemVisit&& item_visit) const
{
    TraversalStats stats;
    for (usize s = 0; s < slots_.Size(); ++s)
    {
        const Slot& slot = slots_[s];
        if (!slot.Live())
        {
            continue;
        }
        ++stats.nodes_tested;
        if (!Intersect(ray, slot.item.box, max_t, nullptr))
        {
            continue;
        }
        ++stats.items_visited;
        if (!item_visit(IdOf(static_cast<u32>(s)), slot.item, max_t))
        {
            stats.stopped = true;
            return stats;
        }
    }
    return stats;
}

}  // namespace vse::spatial
