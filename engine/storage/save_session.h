#pragma once

//! [[storage::SaveSession]]: what an incremental save remembers between saves of one world to one
//! store.

#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "ecs/entity.h"
#include "mem/heap_allocator.h"
#include "storage/entity_ref.h"
#include "types.h"

namespace vse::storage
{

/// The memory of a series of saves of one world to one store: the world tick and removal clock of the last save, the
/// content hash of every record written, and the key each entity had. With it an incremental save visits only the
/// chunks written since, encodes only their entities and writes only records whose hash changed.
///
/// A session starts empty; its first incremental save compares the whole world with the store by hash (so a restarted
/// process does not rewrite what is there). A session is for one world and one store: a save of another world resets
/// it, and so does [[storage::SaveSession::Reset]]. If the store is changed by anything else, call `Reset`.
///
/// - Thread-safety: thread-compatible; used by one save at a time.
/// - Ownership: owns its tables; stores entity ids, not pointers into the world.
class SaveSession
{
public:
    /// Creates an empty session.
    SaveSession();
    SaveSession(const SaveSession&) = delete;
    SaveSession& operator=(const SaveSession&) = delete;

    /// Forgets everything: the next incremental save compares the world with the store.
    /// - Fails: never.
    void Reset();
    /// Returns true when a save has run, so that the next incremental save can use the change stamps.
    /// - Fails: never.
    bool HasBaseline() const { return baseline_; }
    /// The number of records the session believes the store holds.
    /// - Fails: never.
    usize TrackedRecords() const { return hash_of_->Size(); }

private:
    friend class WorldStorage;
    bool baseline_ = false;
    u64 world_id_ = 0;
    u64 since_ = 0;
    u64 removal_epoch_ = 0;
    u64 last_removal_seq_ = 0;
    UniquePtr<HashMap<EntityKey, u64, HeapAllocator>> hash_of_;
    UniquePtr<HashMap<ecs::Entity, EntityKey, HeapAllocator>> key_of_;
};

}  // namespace vse::storage
