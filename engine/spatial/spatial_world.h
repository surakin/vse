#pragma once

//! The spatial world (Phase 18 Day 244): a resource holding the world's named spatial sets, so independent systems (rendering, physics, audio, gameplay)
//! each find their set without coordinating.

#include "containers/array.h"
#include "containers/unique_ptr.h"
#include "settings/registry.h"
#include "settings/setting.h"
#include "spatial/spatial_set.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::spatial {

/// The index of a set in a [[spatial::SpatialWorld]]; also the bit position in [[spatial::SpatialMembership::sets]].
using SetId = u32;

/// The id no set has.
inline constexpr SetId kInvalidSetId = 0xFFFFFFFFu;
/// The set the renderer culls against.
inline constexpr SetId kRenderSet = 0;
/// The set physics' broad phase uses.
inline constexpr SetId kPhysicsSet = 1;
/// The set audio emitters are looked up in.
inline constexpr SetId kAudioSet = 2;
/// The set gameplay proximity and ray queries use.
inline constexpr SetId kGameplaySet = 3;

/// The longest set name; longer names are cut.
inline constexpr usize kMaxSetNameLength = 31;

/// What the sync did, accumulated over its frames.
///
/// Thread-safety: thread-compatible plain data.
struct SpatialWorldStats {
    u64 frames = 0;           ///< Frames the sync ran.
    u64 joined = 0;           ///< Entities that got a proxy.
    u64 left = 0;             ///< Entities whose proxy went (destroyed, lost a component, or lost every set bit).
    u64 items_added = 0;      ///< Items added to a set.
    u64 items_removed = 0;    ///< Items removed from a set.
    u64 moved_between = 0;    ///< Items moved between the static and the dynamic side.
    u64 refused_bounds = 0;   ///< Looks at an entity whose bounds or transform give a non-finite or empty box, or whose layer is 32 or more (a joiner is retried every frame; a member keeps its items).
    u64 refused_full = 0;     ///< Adds a set refused because it is full (retried at the entity's next edit or arrival).
    u64 unknown_set_bits = 0; ///< Set bits of a membership that name no set (counted once per join or membership change; ignored for good: a set created later does not pick up entities that already named its bit until their membership changes).
    u64 bounds_written = 0;   ///< `WorldBounds` values that changed.
    u64 items_moved = 0;      ///< `SpatialSet::Move` calls made for moved entities (a static item's move is refused by the set and counted in its `static_moves_refused`).
    u64 items_reinserted = 0; ///< Of those, the moves that changed the structure (the box left its margin or its cell).
    u64 static_rebuilds = 0;  ///< Static trees rebuilt because their items changed.
    u64 refused_sets = 0;     ///< `CreateSet` calls refused because all `kMaxSpatialSets` sets exist.
};

/// The world's spatial sets, by id and by name. Create it with `world.SetResource(SpatialWorld())`; it starts with the four well-known sets (`kRenderSet`,
/// `kPhysicsSet`, `kAudioSet`, `kGameplaySet`, named "render", "physics", "audio", "gameplay"), and `CreateSet` adds up to four more.
///
/// Thread-safety: thread-compatible; queries on a set are thread-safe against each other, a write (the sync) is not against anything.
/// Ownership: owns the sets.
class SpatialWorld {
public:
    /// Creates the world with the four well-known sets, each with the default configuration.
    SpatialWorld();

    SpatialWorld(SpatialWorld&&) = default;
    SpatialWorld& operator=(SpatialWorld&&) = default;

    /// Adds a set.
    ///
    /// Param name: the set's name, at most `kMaxSetNameLength` characters (cut beyond); a name already used is allowed, `Find` returns the first.
    /// Param config: how the set is made (for example a grid for a crowd).
    /// Returns: the id, or `kInvalidSetId` when `kMaxSpatialSets` sets exist (counted in `Stats().refused_sets`).
    /// Fails: as described.
    SetId CreateSet(StringView name, const SpatialSetConfig& config = SpatialSetConfig());
    /// Returns the number of sets.
    u32 SetCount() const { return count_; }
    /// The set with this id.
    /// Returns: the set, or null for an id that names none.
    /// Fails: returns null as described.
    /// Ownership: valid until the world is destroyed or moved from.
    SpatialSet* Set(SetId id) { return id < count_ ? sets_[id].Get() : nullptr; }
    /// The set with this id (const).
    /// Returns: the set, or null for an id that names none.
    /// Fails: returns null as described.
    /// Ownership: valid until the world is destroyed or moved from.
    const SpatialSet* Set(SetId id) const { return id < count_ ? sets_[id].Get() : nullptr; }
    /// The id of the first set with this name.
    /// Returns: the id, or `kInvalidSetId` when there is none.
    SetId Find(StringView name) const;
    /// The name of a set.
    /// Returns: the name, empty for an id that names none.
    /// Ownership: valid until the world is destroyed.
    StringView NameOf(SetId id) const;
    /// Empties every set (the sets and their names stay). The entities keep their proxies, so call it only together with removing the `SpatialProxy`
    /// components, or when the world is being torn down.
    void Clear();

    /// Whether the sync computes `WorldBounds` as jobs (when its system has the frame thread to itself). On by default; off runs it on the calling thread.
    /// Fails: never.
    /// With [[spatial::SpatialWorld::UseSettings]] it reads `spatial.parallel` each time (so the setting changes it at run time).
    bool Parallel() const { return parallel_setting_.IsBound() ? parallel_setting_.Get() : parallel_; }
    /// Sets whether the sync computes `WorldBounds` as jobs (ignored while a setting is used).
    void SetParallel(bool parallel) { parallel_ = parallel; }
    /// Makes the world follow the `spatial.parallel` setting declared by [[spatial::RegisterSpatialSettings]].
    /// Param registry: the registry that holds it; it must outlive the world.
    /// Aborts: the setting is not registered.
    void UseSettings(const settings::Registry& registry);

    /// What the sync has done.
    const SpatialWorldStats& Stats() const { return stats_; }
    /// What the sync has done, for the sync to update.
    SpatialWorldStats& MutableStats() { return stats_; }

private:
    Array<UniquePtr<SpatialSet>, kMaxSpatialSets> sets_;
    Array<Array<char, kMaxSetNameLength + 1>, kMaxSpatialSets> names_;
    u32 count_ = 0;
    bool parallel_ = true;
    settings::Setting<bool> parallel_setting_;
    SpatialWorldStats stats_;
};

} // namespace engine_placeholder::spatial
