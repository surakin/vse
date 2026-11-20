#pragma once

//! The pair tracker (Phase 18 Day 243): which pairs of items began and which ended overlapping since the last update.
//! The contract physics' narrow phase builds on. Deterministic: the events come out sorted, whatever order the items
//! moved in.

#include "containers/span.h"
#include "containers/vector.h"
#include "containers/array.h"
#include "spatial/query.h"
#include "spatial/spatial_set.h"
#include "types.h"

namespace vse::spatial
{

/// How a [[spatial::PairTracker]] decides which pairs exist.
///
/// A pair is made when two boxes overlap and kept until they have separated by more than their margins: each item's
/// margin is `margin` times its largest extent, and a pair ends when the boxes grown by their margins no longer
/// overlap. The margin keeps a pair from beginning and ending every frame while two objects jitter at the edge of
/// contact (the same idea as the dynamic tree's fat boxes); with a margin of 0 a pair exists exactly while the boxes
/// overlap.
///
/// - Thread-safety: thread-compatible plain data.
struct PairTrackerConfig
{
    /// The end margin, as a fraction of an item's largest extent; 0 or more, finite.
    f32 margin = 0.1f;
    /// For each layer (0 to 31), the layers it meets: bit `j` of entry `i` set means items of layer `i` and layer `j`
    /// form pairs. All set by default.
    Array<u32, 32> meets;

    /// Makes the default configuration: a margin of 0.1 and every pair of layers meeting.
    PairTrackerConfig()
    {
        for (usize i = 0; i < 32; ++i)
        {
            meets[i] = 0xFFFFFFFFu;
        }
    }

    /// Sets whether layers `a` and `b` meet (symmetric).
    ///
    /// - Param a: a layer, 0 to 31.
    /// - Param b: a layer, 0 to 31.
    /// - Param value: whether items of the two layers form pairs.
    /// - Aborts: a layer is 32 or more.
    void SetMeets(u32 a, u32 b, bool value);

    /// Returns whether items of the two layers form pairs; a layer of 32 or more meets nothing.
    ///
    /// - Fails: never.
    bool Meets(u32 a, u32 b) const { return a < 32 && b < 32 && (meets[a] & (1u << b)) != 0; }
};

/// The pairs that began and ended in one update, each with the smaller key first, sorted by (first, second).
///
/// - Thread-safety: thread-compatible plain data.
struct PairEvents
{
    /// Pairs that exist now and did not before.
    Vector<KeyPair> begin;
    /// Pairs that existed before and do not now.
    Vector<KeyPair> end;

    /// Empties both lists (keeps their storage).
    void Clear()
    {
        begin.Clear();
        end.Clear();
    }
};

/// What a [[spatial::PairTracker]] has done.
///
/// - Thread-safety: thread-compatible plain data.
struct PairTrackerStats
{
    u64 updates = 0;  ///< Calls of `Update` and `Recompute`.
    u64 touched = 0;  ///< Items re-queried by `Update`.
    u64 removed = 0;  ///< Keys removed.
    u64 begun = 0;    ///< Pairs begun, in total.
    u64 ended = 0;    ///< Pairs ended, in total.
    usize pairs = 0;  ///< Pairs now.
};

/// Keeps the overlap pairs of a [[spatial::SpatialSet]] between frames and reports what changed.
///
/// The caller says what changed: `Touch` for every item added, moved, given another layer or switched between static
/// and dynamic, and `Remove` for every key removed (the item's handle is stale by then). `Update` re-queries only the
/// touched items against the set, so a frame in which few items moved costs few queries. Two static items never form a
/// pair (they cannot start to touch). A key that is removed and added again between two updates and touched again keeps
/// the pairs that still hold (no events for them) and ends the others; a key that is removed and not touched again
/// loses all its pairs; a new key never inherits pairs.
///
/// - Thread-safety: thread-compatible; `Update` reads the set, which must not change during the call.
/// - Ownership: owns its pair list; the set is borrowed for the call.
class PairTracker
{
public:
    /// Makes an empty tracker.
    ///
    /// - Param config: the margin and the layer matrix; copied.
    /// - Aborts: the margin is negative or not finite.
    explicit PairTracker(const PairTrackerConfig& config = PairTrackerConfig());

    /// Notes that an item was added or changed; it is re-queried at the next `Update`. A stale handle at that time is
    /// skipped.
    ///
    /// - Param handle: the item's handle in the set given to `Update`.
    void Touch(SetHandle handle) { touched_.Push(handle); }

    /// Notes that an item was removed from the set; its pairs end at the next `Update`.
    ///
    /// - Param key: the item's key.
    void Remove(SpatialKey key) { removed_.Push(key); }

    /// Brings the pairs in line with the set for the items touched and removed since the last update.
    ///
    /// - Param set: the set the handles belong to.
    /// - Param events: receives what began and ended (cleared first); not null.
    /// - Aborts: `events` is null.
    /// - Ownership: the set and `events` are borrowed for the call.
    void Update(const SpatialSet& set, PairEvents* events);

    /// Recomputes every pair from scratch, creating pairs only where boxes overlap (no margin), and reports the
    /// difference from the pairs held. The tests use it as the reference; with a margin of 0 it gives the same pairs as
    /// `Update`.
    ///
    /// - Param set: the set.
    /// - Param events: receives the difference (cleared first); not null.
    /// - Aborts: `events` is null.
    /// - Ownership: the set and `events` are borrowed for the call.
    void Recompute(const SpatialSet& set, PairEvents* events);

    /// Forgets every pair and every pending note.
    void Clear();

    /// Returns the number of pairs now.
    usize PairCount() const { return adjacent_.Size() / 2; }

    /// Returns whether the pair exists.
    ///
    /// - Param a: one key.
    /// - Param b: the other key (either order).
    /// - Fails: never.
    bool Has(SpatialKey a, SpatialKey b) const;

    /// Calls `visit(KeyPair)` for every pair, sorted.
    ///
    /// - Param visit: called with each pair, the smaller key first.
    template <class Visit> void ForEachPair(Visit&& visit) const
    {
        for (usize i = 0; i < adjacent_.Size(); ++i)
        {
            if (adjacent_[i].a < adjacent_[i].b)
            {
                visit(adjacent_[i]);
            }
        }
    }

    /// Returns the counts.
    PairTrackerStats Stats() const;

private:
    // The pairs, in both orientations ((x,y) and (y,x)), sorted by (a, b): the neighbors of a key are one contiguous
    // run.
    Vector<KeyPair> adjacent_;
    Vector<SetHandle> touched_;
    Vector<SpatialKey> removed_;
    PairTrackerConfig config_;
    f32 largest_static_margin_ = 0.0f;   // of any static item ever touched
    f32 largest_dynamic_margin_ = 0.0f;  // of any dynamic item ever touched
    PairTrackerStats stats_;
    // Scratch, kept between updates so a steady frame allocates nothing.
    Vector<KeyPair> begin_;
    Vector<KeyPair> end_;
    Vector<KeyPair> merged_;
    Vector<SpatialKey> near_;

    bool Held(SpatialKey a, SpatialKey b) const;
    void Gather(const SpatialSet& set, const Vector<SetHandle>& touched, bool use_margin);
    void Finish(PairEvents* events);
};

}  // namespace vse::spatial
