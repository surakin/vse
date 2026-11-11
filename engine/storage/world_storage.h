#pragma once

//! [[storage::WorldStorage]]: saves a world's entities to a [[storage::Store]] and loads them back (Phase 19). `Save` and `Load` of whole worlds (Day 260), the incremental
//! form (Day 261) and the asynchronous forms (Day 264).

#include "ecs/world.h"
#include "storage/component_registry.h"
#include "storage/identity.h"
#include "storage/save_session.h"
#include "storage/storage_options.h"
#include "storage/store.h"
#include "containers/unique_ptr.h"
#include "sync/atomic.h"

namespace engine_placeholder::jobs {
class Scheduler;
} // namespace engine_placeholder::jobs

namespace engine_placeholder::storage {

namespace detail {
struct SavePlan;
} // namespace detail

class AsyncSave;
class AsyncLoad;

/// Reads a world through the registry and writes records to a store, and the reverse. Non-virtual: every store is used through the same code.
///
/// - Thread-safety: thread-compatible; a save or load runs on the calling thread and needs the world to itself while it runs. At most one save runs at a time per object (a second returns `Busy`).
/// - Ownership: borrows the registry and the store; both must outlive it.
class WorldStorage {
public:
    /// Binds a registry and a store.
    ///
    /// - Param registry: the components that can be stored.
    /// - Param store: where the records go.
    /// - Ownership: both borrowed.
    WorldStorage(const ComponentRegistry& registry, Store& store) : registry_(&registry), store_(&store) {}

    /// Sets the bounds every encode and decode uses (the default is [[storage::CodecLimits]] as declared; `LimitsFromSettings` reads them from the settings).
    ///
    /// - Param limits: the bounds.
    /// - Fails: never.
    void SetLimits(const CodecLimits& limits) { limits_ = limits; }

    /// Saves the selected entities as one store batch: one record per entity, in key order, holding its stored components (the derived ones that ask for it as
    /// presence only), its parent as a key and its unknown components unchanged. A `Full` save of every keyed entity also removes the records of entities that
    /// are gone.
    ///
    /// - Param world: the world to read; needs an up-to-date `KeyIndex`.
    /// - Param index: the keys of `world`.
    /// - Param options: what and how to save.
    /// - Param report: receives counts; may be null.
    /// - Returns: success, or `Cancelled`, `Io`, `Busy`, `LimitExceeded`, `Invalid`, `Duplicate`.
    /// - Fails: those codes (`Invalid` also for an empty registry, for `SaveMode::Incremental` without a `SaveSession`, and for a session with a selection other than `AllKeyed`); on any failure the store holds the state before the save.
    /// - Ownership: all borrowed for the call.
    Status Save(ecs::World& world, KeyIndex& index, const SaveOptions& options, Report* report);
    /// Loads stored entities into `world`. Every record is decoded and checked first; the world changes only when all of them are good, so a corrupt, truncated,
    /// unknown (under `Fail`), cancelled or dangling-reference load leaves it as it was. Entities are created in key order. Derived state is not stored: run the
    /// transform and spatial systems once afterwards.
    ///
    /// - Param world: the world to fill.
    /// - Param index: the keys of `world`; updated.
    /// - Param options: what and how to load.
    /// - Param report: receives counts; may be null.
    /// - Returns: success, or `Corrupt`, `TooNew`, `TooOld`, `NotFound`, `Exists`, `MissingReference`, `Mismatch`, `LimitExceeded`, `Cancelled`, `Io`, `Duplicate`.
    /// - Fails: those codes; on any failure the world is as it was.
    /// - Ownership: all borrowed for the call.
    Status Load(ecs::World& world, KeyIndex& index, const LoadOptions& options, Report* report);
    /// Saves as [[storage::WorldStorage::Save]] does, but only the reading of the world happens before the call returns: the selected (or, incrementally, the changed)
    /// entities are encoded on the workers of `scheduler` into memory, then one writer job writes them to the store in a single batch while the caller goes on.
    /// What was encoded is the world as the call found it, so the world may change freely once the call has returned, and the stored state is the same whatever
    /// the number of workers (the records are written in key order). Hand the result to `AsyncSave::Finish` (or `Wait`) to learn how it went and, for a save with a
    /// session, to bring the session up to date.
    ///
    /// - Param scheduler: runs the encoding and the writer job; must outlive the returned handle.
    /// - Param world: the world to read; not touched after the call returns.
    /// - Param index: the keys of `world`.
    /// - Param options: as for `Save`; `cancel`, `progress` and `user` must outlive the handle, and `progress` is called from the calling thread while encoding and
    /// once from the writer job when the save has committed.
    /// - Param out: receives the handle on success; unchanged on failure.
    /// - Aborts: not called from participant 0 (the main thread) of `scheduler` outside a job.
    /// - Thread-safety: owner-thread-only; the main thread.
    /// - Ownership: `out` owns the handle, which borrows this object, the store, `scheduler` and the options' borrowed parts; none may go away before it does.
    /// - Fails: `Busy` (a save is running on this object), and the errors of `Save` that can be found before the store is touched (`Invalid`, `Cancelled`, `LimitExceeded`); the store's own errors arrive from `Finish`.
    Status SaveAsync(jobs::Scheduler& scheduler, ecs::World& world, KeyIndex& index, const SaveOptions& options, UniquePtr<AsyncSave>* out);
    /// Reads the stored records on a worker job (the store is touched there and nowhere else), then, in `AsyncLoad::Finish`, decodes them and fills the world on
    /// the calling thread. The world is not touched until `Finish`; a load that fails or is cancelled leaves it as it was.
    ///
    /// - Param scheduler: runs the reading job; must outlive the returned handle.
    /// - Param options: as for `Load`; `cancel`, `progress` and `user` must outlive the handle, and `progress` is called from the reading job while it reads, then from `Finish`.
    /// - Param out: receives the handle on success; unchanged on failure.
    /// - Thread-safety: thread-compatible; any thread may start it, and `AsyncLoad::Finish` is called on the thread that owns the world.
    /// - Ownership: `out` owns the handle, which borrows this object, the store, `scheduler` and the options' borrowed parts; none may go away before it does.
    /// - Fails: `Invalid` (an empty registry, a filter selection); the store's errors arrive from `Finish`.
    Status LoadAsync(jobs::Scheduler& scheduler, const LoadOptions& options, UniquePtr<AsyncLoad>* out);

    /// Removes the records of the keys in `keys` from the store, as one batch.
    ///
    /// - Param keys: the entities to forget.
    /// - Returns: success, or `Io`, `Busy`; a key with no record is not an error.
    /// - Fails: those codes.
    Status Delete(Span<const EntityKey> keys);
    /// Lists the keys in the store in key order, at most `limit`.
    ///
    /// - Param range: the keys to list.
    /// - Param limit: the most keys to return.
    /// - Param out: receives the keys.
    /// - Ownership: `out` is borrowed for the call.
    /// - Fails: the errors of [[storage::ListKeys]].
    Status List(const KeyRange& range, usize limit, Vector<EntityKey>* out) const;

private:
    friend class AsyncSave;
    friend class AsyncLoad;
    Status Plan(ecs::World& world, KeyIndex& index, const SaveOptions& options, jobs::Scheduler* scheduler, detail::SavePlan* plan);
    Status PlanChanged(ecs::World& world, KeyIndex& index, const SaveOptions& options, jobs::Scheduler* scheduler, detail::SavePlan* plan);
    Status Write(detail::SavePlan& plan);
    static void Adopt(SaveSession& session, detail::SavePlan& plan);
    bool BeginWriter() {
        bool expected = false;
        return writer_active_.CompareExchangeStrong(expected, true);
    }
    void EndWriter() { writer_active_.Store(false); }

    const ComponentRegistry* registry_;
    Store* store_;
    CodecLimits limits_;
    Atomic<bool> writer_active_{false};
};

} // namespace engine_placeholder::storage
