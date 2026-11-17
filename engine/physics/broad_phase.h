// The broad phase of the physics module (Phase 25 Day 389): which pairs of colliders may touch, found with Phase 18's
// spatial set and pair tracker.
#pragma once

#include "containers/array.h"
#include "containers/vector.h"
#include "math/geometry.h"
#include "physics/ids.h"
#include "physics/world.h"
#include "spatial/pair_tracker.h"
#include "spatial/spatial_set.h"
#include "types.h"

namespace engine_placeholder::physics
{

/// Which collision layers meet which: 32 layers, each with a word of bits. The matrix is symmetric.
///
/// - Thread-safety: thread-compatible plain data.
struct CollisionFilter
{
    /// For layer `i`, bit `j` is set if layer `i` meets layer `j`.
    Array<u32, 32> meets;

    /// Makes the filter in which every layer meets every other.
    CollisionFilter()
    {
        for (usize i = 0; i < 32; ++i)
        {
            meets[i] = 0xFFFFFFFFu;
        }
    }

    /// Sets whether two layers meet (both directions).
    ///
    /// - Param a: a layer, 0 to 31.
    /// - Param b: a layer, 0 to 31.
    /// - Param value: whether they meet.
    /// - Fails: a layer of 32 or more is ignored.
    void SetMeets(u32 a, u32 b, bool value);

    /// Returns whether two layers meet.
    ///
    /// - Param a: a layer.
    /// - Param b: a layer.
    /// - Returns: true if they meet; false for a layer of 32 or more.
    /// - Fails: never.
    bool Meets(u32 a, u32 b) const { return a < 32 && b < 32 && (meets[a] & (1u << b)) != 0; }
};

/// How the broad phase is set up.
///
/// - Thread-safety: thread-compatible plain data.
struct BroadPhaseConfig
{
    /// The dynamic tree's fat margin, as a fraction of a collider's largest extent (the Day 385 spec's default).
    f32 fat_margin = 0.25f;
    /// The pair tracker's end margin.
    ///
    /// A pair ends only when the boxes grown by this fraction no longer overlap, so a pair at the edge of contact does
    /// not begin and end every step.
    f32 pair_margin = 0.1f;
    /// The length of a step in seconds: a `kBodyContinuous` body's box also covers its motion over this time.
    f32 step_seconds = 1.0f / 60.0f;
    /// A body whose travel in one step exceeds this fraction of its collider's smallest extent counts as continuous
    /// (its box covers the sweep); zero leaves it to `kBodyContinuous`.
    f32 ccd_threshold = 0.0f;
    /// Which layers meet.
    CollisionFilter filter;
    /// Whether two triggers make a pair (Day 395); off by default.
    bool trigger_pairs = false;
};

/// Tells whether a body is swept by continuous collision this step: a dynamic body that is awake and either has
/// `kBodyContinuous` or travels, in one step, more than `ccd_threshold` times its collider's smallest extent.
///
/// - Param body: the body.
/// - Param collider: its collider.
/// - Param step_seconds: the step length.
/// - Param ccd_threshold: the fraction, zero for flag-only.
/// - Returns: true if the body is continuous.
/// - Thread-safety: thread-safe. A pure function.
/// - Ownership: nothing is kept.
/// - Fails: never.
bool IsContinuousBody(const Body& body, const Collider& collider, f32 step_seconds, f32 ccd_threshold);

/// Two colliders whose boxes overlap, the smaller key first.
///
/// - Thread-safety: thread-compatible plain data.
struct ColliderPair
{
    /// The collider with the smaller key.
    ColliderId a;
    /// The other collider.
    ColliderId b;
    /// Whether either collider is a trigger: such a pair reports overlap and never reaches the solver.
    bool trigger = false;
};

/// What the last `Update` did.
///
/// - Thread-safety: thread-compatible plain data.
struct BroadPhaseStats
{
    /// Colliders added to the set.
    u32 added = 0;
    /// Colliders removed from the set.
    u32 removed = 0;
    /// Colliders whose box changed (they were moved or re-added).
    u32 moved = 0;
    /// Pairs that began.
    u32 begun = 0;
    /// Pairs that ended.
    u32 ended = 0;
    /// Pairs now (all of them, including triggers).
    u32 pairs = 0;
    /// The static tree's rebuilds so far.
    u64 static_rebuilds = 0;
    /// Static items waiting outside the tree: zero after every `Update`, or every query scans them.
    u32 pending_static = 0;
};

/// Finds the pairs of colliders that may touch.
///
/// Nothing new is built here: the colliders go into a `spatial::SpatialSet` (the static ones in its tree, the moving
/// ones in its dynamic tree with fat boxes) and a `spatial::PairTracker` keeps the pairs, in sorted order. Each
/// `Update` first brings the set in line with the physics world (adds, removes and moves what changed, rebuilds the
/// static tree when it needs it), then updates the pairs.
///
/// - Thread-safety: thread-compatible; `Update` is single-threaded.
/// - Ownership: owns the set and the tracker; it does not own the world it reads.
class BroadPhase
{
public:
    /// Makes a broad phase.
    ///
    /// - Param config: the margins and the layer matrix; copied.
    explicit BroadPhase(const BroadPhaseConfig& config = BroadPhaseConfig());

    /// Brings the pairs in line with the physics world.
    ///
    /// - Param world: the bodies and colliders; read only.
    /// - Fails: never.
    void Update(const PhysicsWorld& world);

    /// Returns the pairs that may touch now, sorted by key: the work list of the narrow phase.
    ///
    /// A pair is listed when its colliders belong to different bodies and at least one body is dynamic (or one collider
    /// is a trigger).
    ///
    /// - Returns: the pairs.
    /// - Ownership: owned by the broad phase; valid until the next `Update`.
    /// - Fails: never.
    const Vector<ColliderPair>& Pairs() const { return pairs_; }

    /// Returns the pairs that began in the last `Update`, in sorted order (every kind, as the tracker reports them).
    ///
    /// - Returns: the pairs.
    /// - Ownership: owned by the broad phase; valid until the next `Update`.
    /// - Fails: never.
    const Vector<ColliderPair>& Begun() const { return begun_; }

    /// Returns the pairs that ended in the last `Update`, in sorted order.
    ///
    /// - Returns: the pairs.
    /// - Ownership: owned by the broad phase; valid until the next `Update`.
    /// - Fails: never.
    const Vector<ColliderPair>& Ended() const { return ended_; }

    /// Returns what the last `Update` did.
    ///
    /// - Returns: the counts.
    /// - Fails: never.
    BroadPhaseStats Stats() const { return stats_; }

    /// Returns the spatial set, for queries (rays, overlaps) over the colliders; its keys are `ColliderId::ToKey()`.
    ///
    /// - Returns: the set.
    /// - Ownership: owned by the broad phase.
    /// - Fails: never.
    const spatial::SpatialSet& Set() const { return set_; }

private:
    struct Tracked
    {
        u32 generation = 0;  // 0: the slot has no collider in the set
        spatial::SetHandle handle = spatial::kInvalidSetHandle;
        Aabb box;
        bool is_static = false;
        u8 layer = 0;
    };

    void Remove(u32 slot);

    BroadPhaseConfig config_;
    spatial::SpatialSet set_;
    spatial::PairTracker tracker_;
    spatial::PairEvents events_;
    Vector<Tracked> tracked_;
    Vector<ColliderPair> pairs_;
    Vector<ColliderPair> begun_;
    Vector<ColliderPair> ended_;
    BroadPhaseStats stats_;
};

}  // namespace engine_placeholder::physics
