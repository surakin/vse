#pragma once

//! [[storage::WorldStorage]]: saves a world's entities to a [[storage::Store]] and loads them back (Phase 19). Declared on Day 259 so the options and results
//! are fixed; `Save` and `Load` are implemented on Day 260, the incremental form on Day 261 and the asynchronous forms on Day 264. Until then `Save` and `Load` return `Other`.

#include "ecs/world.h"
#include "storage/component_registry.h"
#include "storage/identity.h"
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

    /// Saves the selected entities as one store batch (Day 260).
    ///
    /// Param world: the world to read; needs an up-to-date `KeyIndex`.
    /// Param index: the keys of `world`.
    /// Param options: what and how to save.
    /// Param report: receives counts; may be null.
    /// Returns: success, or `Cancelled`, `Io`, `Busy`, `LimitExceeded`.
    /// Fails: those codes (`Other` until Day 260); on any failure the store holds the state before the save.
    /// Ownership: all borrowed for the call.
    Status Save(ecs::World& world, KeyIndex& index, const SaveOptions& options, Report* report);
    /// Loads stored entities into `world`, all or nothing (Day 260).
    ///
    /// Param world: the world to fill.
    /// Param index: the keys of `world`; updated.
    /// Param options: what and how to load.
    /// Param report: receives counts; may be null.
    /// Returns: success, or `Corrupt`, `TooNew`, `TooOld`, `NotFound`, `MissingReference`, `Cancelled`, `Io`.
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
    const ComponentRegistry* registry_;
    Store* store_;
};

} // namespace engine_placeholder::storage
