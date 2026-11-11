#pragma once

//! [[storage::WorldStorage]]: saves a world's entities to a [[storage::Store]] and loads them back (Phase 19). `Save` and `Load` of whole worlds (Day 260); the incremental form
//! arrives on Day 261 and the asynchronous forms on Day 264.

#include "ecs/world.h"
#include "storage/component_registry.h"
#include "storage/identity.h"
#include "storage/save_session.h"
#include "storage/storage_options.h"
#include "storage/store.h"

namespace engine_placeholder::storage {

/// Reads a world through the registry and writes records to a store, and the reverse. Non-virtual: every store is used through the same code.
///
/// Thread-safety: thread-compatible; a save or load runs on the calling thread and needs the world to itself while it runs.
/// Ownership: borrows the registry and the store; both must outlive it.
class WorldStorage {
public:
    /// Binds a registry and a store.
    ///
    /// Param registry: the components that can be stored.
    /// Param store: where the records go.
    /// Ownership: both borrowed.
    WorldStorage(const ComponentRegistry& registry, Store& store) : registry_(&registry), store_(&store) {}

    /// Sets the bounds every encode and decode uses (the default is [[storage::CodecLimits]] as declared; `LimitsFromSettings` reads them from the settings).
    ///
    /// Param limits: the bounds.
    /// Fails: never.
    void SetLimits(const CodecLimits& limits) { limits_ = limits; }

    /// Saves the selected entities as one store batch: one record per entity, in key order, holding its stored components (the derived ones that ask for it as
    /// presence only), its parent as a key and its unknown components unchanged. A `Full` save of every keyed entity also removes the records of entities that
    /// are gone.
    ///
    /// Param world: the world to read; needs an up-to-date `KeyIndex`.
    /// Param index: the keys of `world`.
    /// Param options: what and how to save.
    /// Param report: receives counts; may be null.
    /// Returns: success, or `Cancelled`, `Io`, `Busy`, `LimitExceeded`, `Invalid`, `Duplicate`.
    /// Fails: those codes (`Invalid` also for an empty registry, for `SaveMode::Incremental` without a `SaveSession`, and for a session with a selection other than `AllKeyed`); on any failure the store holds the state before the save.
    /// Ownership: all borrowed for the call.
    Status Save(ecs::World& world, KeyIndex& index, const SaveOptions& options, Report* report);
    /// Loads stored entities into `world`. Every record is decoded and checked first; the world changes only when all of them are good, so a corrupt, truncated,
    /// unknown (under `Fail`), cancelled or dangling-reference load leaves it as it was. Entities are created in key order. Derived state is not stored: run the
    /// transform and spatial systems once afterwards.
    ///
    /// Param world: the world to fill.
    /// Param index: the keys of `world`; updated.
    /// Param options: what and how to load.
    /// Param report: receives counts; may be null.
    /// Returns: success, or `Corrupt`, `TooNew`, `TooOld`, `NotFound`, `Exists`, `MissingReference`, `Mismatch`, `LimitExceeded`, `Cancelled`, `Io`, `Duplicate`.
    /// Fails: those codes; on any failure the world is as it was.
    /// Ownership: all borrowed for the call.
    Status Load(ecs::World& world, KeyIndex& index, const LoadOptions& options, Report* report);
    /// Removes the records of the keys in `keys` from the store, as one batch.
    ///
    /// Param keys: the entities to forget.
    /// Returns: success, or `Io`, `Busy`; a key with no record is not an error.
    /// Fails: those codes.
    Status Delete(Span<const EntityKey> keys);
    /// Lists the keys in the store in key order, at most `limit`.
    ///
    /// Param range: the keys to list.
    /// Param limit: the most keys to return.
    /// Param out: receives the keys.
    /// Ownership: `out` is borrowed for the call.
    /// Fails: the errors of [[storage::ListKeys]].
    Status List(const KeyRange& range, usize limit, Vector<EntityKey>* out) const;

private:
    Status SaveChanged(ecs::World& world, KeyIndex& index, const SaveOptions& options, Report& report);

    const ComponentRegistry* registry_;
    Store* store_;
    CodecLimits limits_;
};

} // namespace engine_placeholder::storage
