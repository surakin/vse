#pragma once

//! [[asset::AsyncLoader]] (Phase 22 Day 330): loading without stalling the frame. A request reads its blob and the
//! blobs of its closure on the scheduler's workers (into the manager's buffer pool); the owner thread's `Update` then
//! builds the objects, GPU uploads included, within a time and byte budget, so a frame's cost is bounded. At most
//! `assets.max_inflight` requests read at once, the others wait by priority, and past `assets.max_queued` a request
//! fails with `LimitExceeded`. Two requests for one resource share one load. A request can be cancelled at any stage.
//!
//! The objects of a closure are built together, as in [[asset::ResourceManager::Load]]: a level and everything it
//! names appear at once, while the reading reports its progress.

#include "containers/binary_heap.h"
#include "asset/resource_manager.h"
#include "containers/hash_map.h"
#include "containers/shared_ptr.h"
#include "containers/vector.h"
#include "jobs/counter.h"
#include "jobs/scheduler.h"
#include "storage/storage_options.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

namespace vse::asset
{

/// How soon a request is served; a lower value goes first, among requests of one priority the earlier one.
enum class LoadPriority : u8
{
    Immediate = 0,   ///< Needed now (the next frame).
    Normal = 1,      ///< The default.
    Background = 2,  ///< Whenever nothing more urgent waits.
};

/// The state of one asynchronous load, shared by the loader and every ticket that waits for it (an implementation
/// detail of `async_loader.cpp`).
struct LoadRequest;
/// How a load request is held: shared by the loader and the tickets that wait for it.
using LoadRequestPtr = SharedPtr<LoadRequest>;

/// The caller's end of an asynchronous load: poll it, wait for it through the loader, finish it into a handle, or
/// cancel it.
///
/// A ticket that is destroyed or cancelled without being finished stops waiting; when no ticket waits for a load any
/// more, the load is cancelled.
///
/// - Thread-safety: thread-compatible; `IsDone`, `Progress` and `Cancel` are thread-safe.
/// - Ownership: shares the load's state with the loader; the loader (and the manager) must outlive the loads in flight.
class LoadTicket
{
public:
    /// An empty ticket.
    LoadTicket() = default;
    LoadTicket(const LoadTicket&) = delete;
    LoadTicket& operator=(const LoadTicket&) = delete;
    /// Takes the load of `other`.
    LoadTicket(LoadTicket&& other) noexcept;
    /// Stops waiting for the old load, then takes the load of `other`.
    LoadTicket& operator=(LoadTicket&& other) noexcept;
    /// Stops waiting for the load.
    ~LoadTicket();

    /// True when the ticket stands for a load.
    ///
    /// - Fails: never.
    bool Valid() const { return static_cast<bool>(request_); }
    /// True once the load ended, with a handle, an error or a cancellation. Never blocks.
    ///
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    bool IsDone() const;
    /// The outcome of a load that is done (`NotReady` while it is not).
    ///
    /// - Fails: `NotReady`, or the errors of [[asset::ResourceManager::Load]], `Cancelled` and `LimitExceeded`.
    Status Result() const;
    /// Gives the handle of a load that is done.
    ///
    /// - Param out: receives the handle on success; reset otherwise.
    /// - Returns: the outcome, as [[asset::LoadTicket::Result]].
    /// - Ownership: `*out` receives its own reference to the resource.
    Status Finish(HandleBase* out) const;
    /// Stops waiting for the load (once); the load itself is cancelled when no other ticket waits for it. A cancelled
    /// ticket reads as done with `Cancelled` as soon as the load notices.
    ///
    /// - Thread-safety: thread-safe.
    void Cancel();
    /// How many blobs the load has read of how many it knows it needs (the total grows as the closure is found).
    ///
    /// - Param read: receives the blobs read; may be null.
    /// - Param total: receives the blobs known so far; may be null.
    /// - Ownership: writes through its pointer arguments during the call only.
    /// - Thread-safety: thread-safe.
    void Progress(u32* read, u32* total) const;

private:
    friend class AsyncLoader;
    explicit LoadTicket(const LoadRequestPtr& request) : request_(request) {}
    void Detach();

    LoadRequestPtr request_;
    Atomic<bool> detached_{false};
};

/// What [[asset::AsyncLoader::Update]] did.
///
/// - Thread-safety: immutable value.
struct LoadUpdate
{
    u32 completed = 0;  ///< Requests that ended this call (built, failed or cancelled).
    u64 bytes = 0;      ///< Bytes of the blobs of the requests built.
    u32 backlog = 0;    ///< Requests whose blobs were read and that wait for a later `Update`.
};

/// What the loader counted.
///
/// - Thread-safety: immutable value (a copy).
struct AsyncLoadStats
{
    u32 queued = 0;         ///< Requests waiting for a reading slot.
    u32 waiting_table = 0;  ///< Requests by path that wait for a mount's table.
    u32 reading = 0;        ///< Requests being read now.
    u32 ready = 0;          ///< Requests read and waiting for `Update` (the backlog).
    u64 completed = 0;      ///< Requests that ended with a handle.
    u64 failed = 0;         ///< Requests that ended with an error other than a cancellation.
    u64 cancelled = 0;      ///< Requests that ended cancelled.
    u64 rejected = 0;       ///< Requests refused with `LimitExceeded` because the queue was full.
    u64 shared = 0;         ///< Requests that joined a load already in progress.
    u64 updates = 0;        ///< Calls of `Update`.
};

/// What a loader is made with.
///
/// - Thread-safety: thread-compatible plain data.
struct AsyncLoaderOptions
{
    u32 max_inflight = 0;  ///< Requests reading at once; 0 takes the manager's `assets.max_inflight`.
    u32 max_queued = 0;    ///< Requests waiting; 0 takes the manager's `assets.max_queued`.
    /// The clock `Update` measures its time budget with, in nanoseconds; null for the monotonic clock. A test passes a
    /// fake one.
    u64 (*clock)(void* user) = nullptr;
    void* clock_user = nullptr;  ///< Passed to `clock`.
};

/// Asynchronous loading over a [[asset::ResourceManager]] and a job scheduler.
///
/// - Thread-safety: thread-compatible: `LoadAsync`, `Update`, `Wait` and `Stats` belong to the owner thread (the one
///   that calls `Update`, which is also the thread that builds the objects); the reading jobs run on the scheduler's
///   workers.
/// - Ownership: borrows the manager and the scheduler, which must outlive it. Destroying the loader cancels every load,
///   waits for the reading jobs and ends the requests with `Cancelled`.
class AsyncLoader
{
public:
    /// Makes a loader.
    ///
    /// - Ownership: borrows `manager` and `scheduler`.
    AsyncLoader(ResourceManager& manager, jobs::Scheduler& scheduler,
                const AsyncLoaderOptions& options = AsyncLoaderOptions());
    AsyncLoader(const AsyncLoader&) = delete;
    AsyncLoader& operator=(const AsyncLoader&) = delete;
    /// Cancels every load and waits for the reading jobs (on a scheduler of one participant the calling thread, which
    /// must be its main thread, runs them).
    ~AsyncLoader();

    /// Starts loading the resource `id` as a `T`.
    ///
    /// - Param id: the resource.
    /// - Param priority: how soon.
    /// - Param cancel: asks the load to stop when it is set; may be null; must outlive the load.
    /// - Returns: the ticket; a ticket that is already done with `LimitExceeded` when the queue is full, and one done
    ///   at once when the resource is loaded already.
    /// - Fails: reported through the ticket, as [[asset::ResourceManager::Load]].
    /// - Ownership: the ticket shares the load.
    template <class T>
    LoadTicket LoadAsync(ResourceId id, LoadPriority priority = LoadPriority::Normal,
                         const storage::CancelToken* cancel = nullptr)
    {
        return LoadAsyncRaw(id, ResourceTraits<T>::kType, ObjectKey<T>(), priority, cancel);
    }
    /// As `LoadAsync`, for a path: it is resolved through the mounts' tables; while a mount's table has not arrived the
    /// ticket stays pending, and it ends with `NotFound` once every table is in and the path is not.
    ///
    /// - Ownership: `path` is read during the call only.
    template <class T>
    LoadTicket LoadPathAsync(StringView path, LoadPriority priority = LoadPriority::Normal,
                             const storage::CancelToken* cancel = nullptr)
    {
        return LoadPathAsyncRaw(path, ResourceTraits<T>::kType, ObjectKey<T>(), priority, cancel);
    }
    /// As `LoadAsync` with the type and the C++ type key given instead of a `T`.
    ///
    /// - Ownership: as `LoadAsync`.
    LoadTicket LoadAsyncRaw(ResourceId id, ResourceType type, u32 key, LoadPriority priority,
                            const storage::CancelToken* cancel);
    /// As `LoadPathAsync` with the type and the key given.
    ///
    /// - Ownership: as `LoadPathAsync`.
    LoadTicket LoadPathAsyncRaw(StringView path, ResourceType type, u32 key, LoadPriority priority,
                                const storage::CancelToken* cancel);
    /// Loads a level as a streamed unit: its blob is read, then the blobs of what it names, those of the levels it
    /// includes first and the rest in the order the level lists them, and [[asset::LoadTicket::Progress]] reports how
    /// many are read. The level and its dependencies are built together by one `Update`, as `Load` builds a closure,
    /// so the level is active when its ticket is done.
    ///
    /// - Param id: the level.
    /// - Returns: a ticket of the level's summary ([[asset::LevelSummary]], whatever loader the manager has for the
    ///   level type).
    /// - Ownership: as `LoadAsync`.
    LoadTicket LoadLevelAsync(ResourceId id, LoadPriority priority = LoadPriority::Normal,
                              const storage::CancelToken* cancel = nullptr);

    /// Builds the objects of the requests whose blobs were read, most urgent first, until `max_microseconds` of the
    /// clock or `max_bytes` of blobs are spent (at least one request is built per call when any waits), and starts
    /// queued requests as reading slots free. Call it once per frame on the thread that owns the GPU objects.
    ///
    /// - Param max_microseconds: the time budget; 0 for none.
    /// - Param max_bytes: the byte budget; 0 for none.
    /// - Returns: what was done.
    LoadUpdate Update(u64 max_microseconds, u64 max_bytes);
    /// Runs `Update` without a budget (and the scheduler's jobs when it has no workers) until the ticket is done.
    ///
    /// - Returns: the outcome of the load, as [[asset::LoadTicket::Result]].
    /// - Fails: an empty ticket is `Invalid`.
    Status Wait(const LoadTicket& ticket);
    /// A copy of the counters.
    AsyncLoadStats Stats() const;

    /// Called by the reading job.
    void RunRead(LoadRequest& request);

private:
    void PumpLocked();
    void DropCancelledLocked();
    LoadRequestPtr FindActiveLocked(ResourceId id, u32 key, const storage::CancelToken* cancel,
                                    const LoadRequest* except);
    bool FullLocked() const;
    void StartLocked(const LoadRequestPtr& request);
    void CompleteLocked(const LoadRequestPtr& request, const Status& status);
    LoadTicket Submit(const LoadRequestPtr& request);
    u64 Now() const;

    ResourceManager* manager_;
    jobs::Scheduler* scheduler_;
    AsyncLoaderOptions options_;
    jobs::Counter outstanding_;
    mutable Mutex mutex_;
    // The most urgent first (container audit Day 11: a linear scan per start). `Before` is a total order (priority,
    // then the submit sequence), so the loads start in the same order as before.
    struct LaterRequest
    {
        bool operator()(const LoadRequestPtr& a, const LoadRequestPtr& b) const;
    };
    BinaryHeap<LoadRequestPtr, LaterRequest> queued_;
    Vector<LoadRequestPtr> waiting_;
    Vector<LoadRequestPtr> ready_;
    Vector<LoadRequestPtr> active_;  // every request that is not done: for deduplication and shutdown
    u32 reading_ = 0;
    u64 next_sequence_ = 0;
    AsyncLoadStats stats_;
};

}  // namespace vse::asset
