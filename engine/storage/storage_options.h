#pragma once

//! What a save or a load is told and what it answers (Phase 19 Day 259): [[storage::Selection]], [[storage::SaveOptions]], [[storage::LoadOptions]],
//! [[storage::Report]], a progress callback and a [[storage::CancelToken]]. [[storage::WorldStorage]] takes them from Day 260.

#include "containers/vector.h"
#include "ecs/entity.h"
#include "ecs/world.h"
#include "storage/component_registry.h"
#include "storage/entity_ref.h"
#include "storage/identity.h"
#include "string/string.h"
#include "sync/atomic.h"
#include "types.h"

namespace engine_placeholder::storage {

/// A flag that asks a running save or load to stop. Checked between batches, so a stop takes effect after the batch in progress; nothing committed is undone
/// and the world a load was building is left as it was.
///
/// Thread-safety: thread-safe; `Cancel` may be called from any thread.
/// Ownership: owned by the caller; must outlive the operation it is given to.
class CancelToken {
public:
    /// Asks for a stop. Stays set.
    /// Fails: never.
    void Cancel() { flag_.Store(true); }
    /// Returns true once `Cancel` was called.
    /// Fails: never.
    bool IsCancelled() const { return flag_.Load(); }
    /// Clears the flag so the token can be used again.
    /// Fails: never.
    void Reset() { flag_.Store(false); }

private:
    Atomic<bool> flag_{false};
};

/// Which entities a save looks at.
///
/// Thread-safety: thread-compatible.
/// Ownership: owns its key list; `filter` and `user` are borrowed and must outlive the operation.
struct Selection {
    /// How the entities are chosen.
    enum class Kind : u8 {
        AllKeyed, ///< Every entity that has an [[storage::EntityKeyComponent]].
        Keys,     ///< The entities with the keys in `keys` (a key with no entity is reported, not an error).
        Filter    ///< Every keyed entity that has all of `require` and for which `filter` returns true.
    };
    /// The kind.
    Kind kind = Kind::AllKeyed;
    /// The keys, for `Kind::Keys`.
    Vector<EntityKey> keys;
    /// The ECS component ids an entity must have, for `Kind::Filter` (the query part).
    Vector<ecs::ComponentId> require;
    /// For `Kind::Filter`: called for each entity that has everything in `require`; null accepts them all. Runs on the saving thread.
    bool (*filter)(const ecs::World& world, ecs::Entity entity, void* user) = nullptr;
    /// Passed to `filter`.
    void* user = nullptr;

    /// Selects every keyed entity.
    /// Fails: never.
    static Selection All() { return Selection(); }
};

class SaveSession;

/// Whether a save writes everything or what changed.
enum class SaveMode : u8 {
    Full,        ///< Every selected entity; records of entities that no longer exist are removed.
    Incremental  ///< Only entities that changed since the last save of this session (Day 261).
};

/// A progress callback: `done` of `total` entities handled so far. Called between batches on the saving or loading thread.
using ProgressFn = void (*)(usize done, usize total, void* user);

/// How to save.
///
/// Thread-safety: thread-compatible.
/// Ownership: `label` is owned; `cancel`, `progress` and `user` are borrowed for the operation.
struct SaveOptions {
    SaveMode mode = SaveMode::Full;      ///< Full or incremental.
    Selection selection;                 ///< The entities to save.
    String<HeapAllocator> label{HeapAllocator::Default()}; ///< Recorded as the store's `last_save_label`.
    usize batch_size = 1000;             ///< Entities per store batch (a full save is one batch whatever this is; it sizes the progress and cancel steps).
    SaveSession* session = nullptr;      ///< Required for `SaveMode::Incremental`; a `Full` save that is given one starts it from that save.
    CancelToken* cancel = nullptr;       ///< Checked between steps; null: never cancelled.
    ProgressFn progress = nullptr;       ///< Called between steps; null: none.
    void* user = nullptr;                ///< Passed to `progress`.
};

/// What a load does with the entities already in the world.
enum class LoadMode : u8 {
    Create,   ///< The world must hold no keyed entity; the stored ones are created.
    Replace,  ///< Every keyed entity of the world is destroyed first (after the stored state has been checked), then the stored ones are created.
    Merge     ///< A stored entity whose key is in the world replaces that entity's stored components (the entity id stays); the others are created.
};

/// How to load.
///
/// Thread-safety: thread-compatible.
/// Ownership: `cancel`, `progress` and `user` are borrowed for the operation.
struct LoadOptions {
    LoadMode mode = LoadMode::Create;                       ///< What to do with entities already there.
    Selection selection;                                    ///< The stored entities to load (`Filter` is not available on a load).
    UnknownPolicy unknown = UnknownPolicy::Fail;            ///< What to do with a record of a component the registry does not know.
    MissingPolicy missing = MissingPolicy::Fail;            ///< What to do with a reference to a key that is nowhere.
    CancelToken* cancel = nullptr;                          ///< Checked between steps; null: never cancelled.
    ProgressFn progress = nullptr;                          ///< Called between steps; null: none.
    void* user = nullptr;                                   ///< Passed to `progress`.
};

/// What a save or load did.
///
/// Thread-safety: thread-compatible plain data.
struct Report {
    usize created = 0;        ///< Entities created in the world (load) or records added to the store (save).
    usize replaced = 0;       ///< Entities or records overwritten.
    usize removed = 0;        ///< Records removed from the store (save) or entities destroyed (replace).
    usize skipped = 0;        ///< Entities left alone: unchanged on an incremental save, or components skipped by `SkipAndReport`.
    usize unknown_kept = 0;   ///< Records of unknown components carried opaquely (`KeepOpaque`).
    usize nulled = 0;         ///< References turned into null references.
    usize missing_keys = 0;   ///< Keys named by `Selection::Keys` that nothing has.
    usize encoded = 0;        ///< Entities encoded (a save): the cost driver; an idle incremental save encodes none.
    usize chunks_visited = 0; ///< Chunks an incremental save walked because something in them changed.
    usize chunks_skipped = 0; ///< Chunks an incremental save skipped as unchanged.
    usize reconciled = 0;     ///< 1 when an incremental save had to compare the whole world with the store (a new session, or removals that the log no longer held).
    usize unkeyed_parents = 0; ///< Entities whose parent has no key: saved without a parent link.
    usize parents_rejected = 0; ///< Parent links not applied on a load: in a cycle, or deeper than the limit.
    u64 bytes = 0;            ///< Bytes written to or read from the store.
};

} // namespace engine_placeholder::storage
