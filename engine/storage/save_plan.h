#pragma once

//! [[storage::detail::SavePlan]] (Phase 19 Day 264): what a save decided and encoded from the world, before the store
//! is touched.

#include "bytes/byte_writer.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "storage/entity_ref.h"
#include "storage/storage_options.h"
#include "string/string.h"

namespace engine_placeholder::storage::detail
{

/// One entity encoded.
struct Encoded
{
    bytes::ByteWriter blob;  ///< The entity's record bytes.
    u64 hash = 0;            ///< Their hash.
    Status status;           ///< The encoding's result.
    usize unkeyed = 0;       ///< Parent links that were left out because the parent has no key.
};

/// A keyed entity chosen by a save, with its key at the time.
struct Chosen
{
    EntityKey key;
    ecs::Entity entity;
    friend bool operator<(const Chosen& a, const Chosen& b) { return a.key < b.key; }
};

/// A record to write by an incremental save.
struct PutOp
{
    EntityKey key;
    ecs::Entity entity;
    u64 hash = 0;
    bytes::ByteWriter blob;
};

/// An entity with the key it has now.
struct KeyedEntity
{
    EntityKey key;
    ecs::Entity entity;
};

/// Everything a save read from the world: the encoded records and the bookkeeping its session needs. Built by
/// `WorldStorage` on the thread that owns the world, written to the store by `WorldStorage` (possibly on another
/// thread), and adopted by the session afterwards. Reads no world after it is built.
///
/// - Thread-safety: thread-compatible; handed from the building thread to the writing thread and back by the caller.
/// - Ownership: owns everything it holds; `cancel`, `progress` and `user` are borrowed from the options.
struct SavePlan
{
    bool changed_style = false;  ///< Built from the change stamps (an incremental save with a baseline) rather than
                                 ///< from the whole selection.
    bool reconcile = false;      ///< Records whose stored hash equals the new one are not written again.
    bool all_keyed = false;      ///< Records of entities that are gone are removed.
    bool track_session = false;  ///< The session is brought up to date after the commit.
    usize step = 1000;           ///< Entities per cancel and progress step.
    String<HeapAllocator> label{HeapAllocator::Default()};
    CancelToken* cancel = nullptr;
    ProgressFn progress = nullptr;
    void* user = nullptr;
    Report report;

    // The whole selection.
    Vector<Chosen> chosen;
    Vector<Encoded> encoded;  // index-aligned with `chosen`

    // The changed entities only.
    Vector<PutOp> puts;
    Vector<EntityKey> removes;
    Vector<ecs::Entity> forgotten;
    Vector<KeyedEntity> rekeyed;
    u64 largest_removal_seq = 0;

    // The world's clocks when the plan was built.
    u64 world_id = 0;
    u64 tick = 0;
    u64 removal_epoch = 0;
};

}  // namespace engine_placeholder::storage::detail
