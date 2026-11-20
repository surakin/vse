// Saving a physics snapshot into a storage record store (Phase 25 Day 406): the snapshot is one record, so it travels
// through the same stores (memory, local files, SQL) as saved worlds do, with the store's own integrity hash.
#pragma once

#include "physics/snapshot_status.h"
#include "physics/world.h"
#include "storage/entity_ref.h"
#include "storage/status.h"
#include "storage/store.h"
#include "types.h"

namespace vse::physics
{

/// Writes the world's snapshot as one record of a store under `key`, replacing the record that was there.
///
/// - Param world: the world.
/// - Param store: the store.
/// - Param key: a non-null key.
/// - Returns: the store's status (`Invalid` for a null key; the store's `Put` errors).
/// - Thread-safety: thread-compatible; not while the world steps.
/// - Ownership: the bytes are copied into the store.
/// - Fails: as the return says.
storage::Status SaveSnapshot(const PhysicsWorld& world, storage::Store* store, storage::EntityKey key);

/// Reads the record under `key` and restores the world from it.
///
/// - Param world: the world; unchanged unless the result is success.
/// - Param store: the store.
/// - Param key: the key `SaveSnapshot` used.
/// - Param detail: receives what `Restore` found (`Ok` when the record was read and restored); may be null.
/// - Returns: the store's status (`NotFound`, `Corrupt` for a record whose hash does not match, `Io`), or `Corrupt`
/// when
///   the bytes are not a valid snapshot (`detail` says which way).
/// - Thread-safety: thread-compatible; not while the world steps.
/// - Ownership: nothing is kept.
/// - Fails: as the return says.
storage::Status LoadSnapshot(PhysicsWorld* world, const storage::Store& store, storage::EntityKey key,
                             SnapshotStatus* detail);

}  // namespace vse::physics
