#pragma once

//! [[storage::AsyncSave]] and [[storage::AsyncLoad]] (Phase 19 Day 264): the handles of [[storage::WorldStorage::SaveAsync]] and [[storage::WorldStorage::LoadAsync]],
//! and [[storage::Autosaver]], which saves the running world every few seconds without stalling the frame.

#include "containers/unique_ptr.h"
#include "jobs/counter.h"
#include "jobs/scheduler.h"
#include "storage/memory_store.h"
#include "storage/world_storage.h"

namespace engine_placeholder::storage {

/// A save in progress: the writer job is running (or has finished) and the session still has to learn the outcome.
///
/// A save is two steps. `WorldStorage::SaveAsync` reads the world (encoding on the workers) and returns; the writer job then owns the store until it is done.
/// `Finish` (or `Wait`) reports how it went and, for a save with a session, brings the session up to date; call it on the thread that owns the world and the session,
/// before the next save with that session. A handle destroyed without `Finish` waits for the writer, then, if the save succeeded, brings the session up to date on the destroying thread
/// (so that thread must own the world and the session, as for `Finish`).
///
/// - Thread-safety: thread-compatible; `IsDone` is thread-safe, `Wait` belongs to the main thread of the scheduler, and `Finish` is called by one thread at a time.
/// - Ownership: borrows the `WorldStorage` (and through it the store), the scheduler and the session of the options, all of which must outlive it; owns the encoded records.
class AsyncSave {
public:
    /// Used by `WorldStorage::SaveAsync` (public only so that `MakeUnique` can reach it); starts the writer job.
    ///
    /// - Ownership: borrows the storage, scheduler and session; owns the plan.
    AsyncSave(WorldStorage& storage, jobs::Scheduler& scheduler, SaveSession* session, UniquePtr<detail::SavePlan> plan);
    /// Waits for the writer job if it is still running (on a scheduler with no workers the calling thread, which must then be its main thread, runs it).
    ~AsyncSave();
    AsyncSave(const AsyncSave&) = delete;
    AsyncSave& operator=(const AsyncSave&) = delete;

    /// Returns true once the writer job has finished (the store is committed or rolled back).
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    bool IsDone() const { return counter_.IsZero(); }
    /// Runs jobs on the calling thread until the writer has finished, then finishes the save.
    ///
    /// - Param report: receives the counts; may be null.
    /// - Returns: the result of the save.
    /// - Aborts: not called from participant 0 (the main thread) outside a job.
    /// - Ownership: `report` is written during the call only.
    /// - Fails: the errors of [[storage::WorldStorage::Save]] that concern the store: `Cancelled`, `Io`, `Busy`.
    Status Wait(Report* report);
    /// Reports the outcome of a save whose writer has finished, and updates the session. Idempotent: a second call returns the same result.
    ///
    /// - Param report: receives the counts; may be null.
    /// - Returns: the result of the save.
    /// - Ownership: `report` is written during the call only.
    /// - Fails: `Busy` when the writer is still running (call again later); the errors of `Wait`.
    Status Finish(Report* report);
    /// Called by the writer job.
    void Run();

private:
    WorldStorage* storage_;
    jobs::Scheduler* scheduler_;
    SaveSession* session_;
    UniquePtr<detail::SavePlan> plan_;
    jobs::Counter counter_;
    Status result_;
    bool finished_ = false;
};

/// A load in progress: the reading job copies the stored records into memory; `Finish` decodes them and fills the world.
///
/// - Thread-safety: thread-compatible; `IsDone` is thread-safe, `Wait` belongs to the main thread of the scheduler, and `Finish` is called on the thread that owns the world.
/// - Ownership: borrows the `WorldStorage`, the scheduler and the options' borrowed parts, which must outlive it; owns the records it read.
class AsyncLoad {
public:
    /// Used by `WorldStorage::LoadAsync` (public only so that `MakeUnique` can reach it).
    AsyncLoad(WorldStorage& storage, jobs::Scheduler& scheduler, const LoadOptions& options);
    /// Waits for the reading job if it is still running (on a scheduler with no workers the calling thread, which must then be its main thread, runs it).
    ~AsyncLoad();
    AsyncLoad(const AsyncLoad&) = delete;
    AsyncLoad& operator=(const AsyncLoad&) = delete;

    /// Returns true once the reading job has finished.
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    bool IsDone() const { return counter_.IsZero(); }
    /// Runs jobs on the calling thread until the reading job has finished, then finishes the load.
    ///
    /// - Param world: the world to fill; as for [[storage::WorldStorage::Load]].
    /// - Param index: the keys of `world`.
    /// - Param report: receives the counts; may be null.
    /// - Aborts: not called from participant 0 (the main thread) outside a job.
    /// - Ownership: all borrowed for the call.
    /// - Fails: the errors of [[storage::WorldStorage::Load]].
    Status Wait(ecs::World& world, KeyIndex& index, Report* report);
    /// Decodes what the job read and fills the world, as `Load` does; the world is as it was when this fails. Call it once: a second call loads again.
    ///
    /// - Param world: the world to fill.
    /// - Param index: the keys of `world`; updated.
    /// - Param report: receives the counts; may be null.
    /// - Ownership: all borrowed for the call.
    /// - Fails: `Busy` when the reading job is still running (call again later); the errors of `Load`, and `Cancelled` when the read was cancelled.
    Status Finish(ecs::World& world, KeyIndex& index, Report* report);
    /// Called by the reading job.
    void Run();

private:
    WorldStorage* storage_;
    jobs::Scheduler* scheduler_;
    LoadOptions options_;
    MemoryStore records_;
    jobs::Counter counter_;
    Status result_;
};

/// Saves a running world to a store at a fixed interval, as incremental saves with their own session, without stalling the frame: `Update` encodes only the entities
/// written since the last save (a few chunks in a quiet scene) and hands the writing to a job. Call `Update` once a frame from the main thread, at a point where no
/// system is writing components.
///
/// - Thread-safety: owner-thread-only; the main thread of the scheduler.
/// - Ownership: borrows the storage, scheduler, world and index, which must outlive it; owns its session and the save in flight.
class Autosaver {
public:
    /// Makes an autosaver.
    ///
    /// - Param storage: where the saves go.
    /// - Param scheduler: runs the encoding and the writer jobs.
    /// - Param world: the world to save.
    /// - Param index: the keys of `world`.
    /// - Param interval_seconds: seconds between saves; 0 never saves by itself (`SaveNow` still works).
    /// - Ownership: all borrowed.
    Autosaver(WorldStorage& storage, jobs::Scheduler& scheduler, ecs::World& world, KeyIndex& index, f64 interval_seconds);
    /// Waits for the save in flight and drops its session update.
    ~Autosaver();
    Autosaver(const Autosaver&) = delete;
    Autosaver& operator=(const Autosaver&) = delete;

    /// Advances the clock by `dt` seconds. Collects the save in flight if it has finished, and starts the next when the interval has passed and none is running.
    ///
    /// - Param dt: seconds since the last call.
    /// - Returns: success, or the error of the save that was just collected; the next save is tried at the next interval either way.
    /// - Aborts: not called from participant 0 (the main thread) outside a job.
    /// - Fails: the errors of [[storage::WorldStorage::SaveAsync]] and [[storage::AsyncSave::Finish]].
    Status Update(f64 dt);
    /// Starts a save now unless one is running.
    ///
    /// - Returns: success (also when one is already running), or the error of starting it.
    /// - Fails: the errors of [[storage::WorldStorage::SaveAsync]].
    Status SaveNow();
    /// Waits for the save in flight and collects it.
    ///
    /// - Returns: its result, or success when none was running.
    /// - Aborts: not called from participant 0 (the main thread) outside a job.
    /// - Fails: the errors of [[storage::AsyncSave::Wait]].
    Status Flush();
    /// The number of saves that have been collected successfully.
    /// - Fails: never.
    usize Completed() const { return completed_; }

private:
    WorldStorage* storage_;
    jobs::Scheduler* scheduler_;
    ecs::World* world_;
    KeyIndex* index_;
    f64 interval_;
    f64 elapsed_ = 0;
    SaveSession session_;
    UniquePtr<AsyncSave> running_;
    usize completed_ = 0;
};

} // namespace engine_placeholder::storage
