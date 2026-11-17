#pragma once
//! The resources of one engine (Phase 23 Day 350): the memory backend that holds the content a host adds, the resource
//! manager that loads it, the asynchronous loader, the release of lent bytes on an engine worker, and the handles and
//! tickets the C API hands out. Internal to the module `engine_c`: nothing here reaches the host but through
//! `engine_c.h`.
#include "animation/manager_resolver.h"
#include "asset/async_loader.h"
#include "asset/memory_backend.h"
#include "asset/resource_manager.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "engine/engine_c.h"
#include "engine_c/import.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "types.h"

namespace engine_placeholder::engine_c
{

class Runtime;

/// What a loaded resource is to the host: the manager's handle and the type the host asked for.
///
/// - Thread-safety: thread-compatible; the host's table mutex guards its use.
/// - Ownership: owns one reference to the loaded resource.
struct ResourceObject
{
    asset::HandleBase handle;
    u32 type = 0;
};

/// One asynchronous load.
///
/// - Thread-safety: thread-compatible; `waiters` is guarded by the host's table mutex.
/// - Ownership: owns the ticket (a share of the load).
struct TicketObject
{
    asset::LoadTicket ticket;
    u32 type = 0;
    u32 waiters = 0;
};

/// The content and loading of an engine, made by the first resource call.
///
/// - Thread-safety: thread-safe; see the mutexes.
/// - Ownership: owns the backend, the manager, the loader, the handles and the tickets; destroying it releases every
///   lent buffer (on the engine's worker) before it returns.
class ResourceHost
{
public:
    /// Makes the host and mounts the memory backend.
    ///
    /// - Param runtime: the runtime whose job system runs the releases and the reading; it must outlive the host.
    explicit ResourceHost(Runtime& runtime);
    /// Cancels the loads, drops the handles' manager and the backend, and waits until every lent buffer was handed
    /// back.
    ~ResourceHost();
    ResourceHost(const ResourceHost&) = delete;
    ResourceHost& operator=(const ResourceHost&) = delete;

    engine_status Add(const engine_resource_add_info* info);
    engine_status Replace(const engine_resource_add_info* info, engine_buffer* dependents);
    engine_status Remove(u64 id, u32 flags, engine_buffer* dependents);
    engine_status Mounted(u64 id, engine_resource_info* out);
    engine_status Load(u64 id, engine_string path, u32 type, engine_handle* out);
    engine_status Release(engine_handle resource);
    engine_status StatusOf(engine_handle resource, engine_resource_info* out);
    engine_status LoadAsync(u64 id, engine_string path, u32 type, u32 priority, engine_handle* ticket);
    engine_status TicketPoll(engine_handle ticket, engine_ticket_info* out);
    engine_status TicketWait(engine_handle ticket, u64 timeout_ms);
    engine_status TicketFinish(engine_handle ticket, engine_handle* resource);
    engine_status TicketCancel(engine_handle ticket);
    engine_status TicketDestroy(engine_handle ticket);

    /// The glTF import of this host (its results go to this host's memory backend).
    ///
    /// - Thread-safety: thread-safe.
    ImportHost& Imports() { return *imports_; }

    /// The type registry and the memory backend a surface's own manager is built over (Phase 24 Day 359): the content a
    /// host added is read through them on the surface's device.
    ///
    /// - Thread-safety: thread-safe (set at construction).
    asset::TypeRegistry& Types() { return types_; }
    /// See `Types`.
    asset::HostMemoryBackend& Memory() { return *memory_; }

    /// The animation system's view of this host's manager (Phase 24 Day 363): skeletons and clips pinned with
    /// `PinSkeleton` and `PinClip` are found through it.
    ///
    /// - Thread-safety: thread-safe.
    const animation::AnimationResolver& Animation() const { return *animation_; }
    /// Loads the skeleton `id` and keeps it loaded until the host is destroyed.
    ///
    /// - Returns: ENGINE_OK, NOT_FOUND (not mounted) or TYPE_MISMATCH (not a skeleton), with the text set.
    engine_status PinSkeleton(u64 id);
    /// Loads the clip `id`, checks that it was made for the skeleton `skeleton_id` (which must be pinned), and keeps it
    /// loaded until the host is destroyed.
    ///
    /// - Returns: ENGINE_OK, NOT_FOUND (not mounted), TYPE_MISMATCH (not a clip) or INVALID_ARGUMENT (another
    ///   skeleton's), with the text set.
    engine_status PinClip(u64 id, u64 skeleton_id);
    /// Reads the level blob `id` and checks that everything it names is mounted (Phase 24 Day 364).
    ///
    /// - Param bytes: replaced by a copy of the blob.
    /// - Returns: ENGINE_OK, NOT_FOUND (the level or a resource it names is not mounted), TYPE_MISMATCH (not a level),
    ///   UNSUPPORTED (it includes other levels) or INVALID_ARGUMENT (damaged), with the text set.
    engine_status ReadLevel(u64 id, Vector<byte>* bytes);

    /// Finds the mesh slot named `name` of the mesh `id` (Phase 24 Day 377).
    ///
    /// - Param index: receives the slot's index.
    /// - Returns: ENGINE_OK, NOT_FOUND (the mesh is not mounted, or has no such slot), TYPE_MISMATCH (not a mesh), with
    ///   the text set.
    engine_status FindMeshSlot(u64 id, StringView name, u32* index);

    /// Builds what the loader has read (called by every frame).
    ///
    /// - Thread-safety: thread-safe.
    void PumpLoads();

    /// One lent buffer waiting to be handed back (an implementation detail of `resources.cpp`).
    struct ReleaseRecord;

    /// Hands back the queued lent buffers (the body of the job that runs on an engine worker).
    ///
    /// - Thread-safety: runs on one worker at a time.
    void RunReleases();

    /// Queues the hand-back of a lent buffer for an engine worker. Called by the backend's entry from any thread.
    void QueueRelease(ReleaseRecord* record);

private:
    engine_status MakeResource(asset::HandleBase&& handle, u32 type, engine_handle* out);
    static void OnRelease(void* user, ByteSpan bytes);
    asset::AsyncLoader* LoaderLocked();  // the loader_mutex is held
    engine_status ResolveId(u64 id, engine_string path, asset::ResourceId* out);
    void TablesChanged() { tables_dirty_.Store(true, MemoryOrder::Release); }
    void FreshTables();  // reads the mounts' tables again when content changed since they were read

    Runtime* runtime_;
    asset::TypeRegistry types_;

    // The hand-back queue lives before the backend: the backend's entries queue releases while it is destroyed.
    Mutex release_mutex_;
    ConditionVariable release_changed_;
    Vector<ReleaseRecord*> release_queue_;
    bool release_pump_active_ = false;
    u32 release_pending_ = 0;  // queued or running

    Atomic<bool> tables_dirty_{false};  // content changed since the mounts' tables were read

    UniquePtr<asset::HostMemoryBackend> memory_;
    UniquePtr<ImportHost> imports_;  // after the backend: destroyed before it
    UniquePtr<asset::ResourceManager> manager_;
    UniquePtr<animation::ManagerResolver> animation_;  // over manager_
    Mutex pin_mutex_;
    Vector<asset::Handle<asset::SkeletonView>> skeleton_pins_;
    Vector<asset::Handle<asset::ClipView>> clip_pins_;
    Mutex loader_mutex_;  // the loader belongs to one thread at a time
    UniquePtr<asset::AsyncLoader> loader_;

    Mutex table_mutex_;  // guards the two tables
    struct ResourceSlot
    {
        UniquePtr<ResourceObject> object;
        u32 generation = 0;
    };
    struct TicketSlot
    {
        UniquePtr<TicketObject> object;
        u32 generation = 0;
    };
    Vector<ResourceSlot> resources_;
    Vector<TicketSlot> tickets_;
};

/// The host of the engine a handle names, made on first use.
///
/// - Returns: OK, or the failure of resolving the engine handle.
engine_status HostOfEngine(engine_handle engine, ResourceHost** out);

/// The host of the engine that exists, for the calls that have only a handle of an object.
///
/// - Returns: OK, or INVALID_ARGUMENT / STALE_HANDLE with the failure text set.
engine_status HostOfCurrent(engine_handle handle, ResourceHost** out);

// The functions of the table that the resource host serves (engine_c.cpp lists them in the table).
engine_status ResourceAddFn(engine_handle engine, const engine_resource_add_info* info);
engine_status ResourceReplaceFn(engine_handle engine, const engine_resource_add_info* info, engine_buffer* dependents);
engine_status ResourceRemoveFn(engine_handle engine, u64 id, u32 flags, engine_buffer* dependents);
engine_status ResourceMountedFn(engine_handle engine, u64 id, engine_resource_info* out);
engine_status ResourceLoadFn(engine_handle engine, u64 id, engine_string path, u32 type, engine_handle* out);
engine_status ResourceReleaseFn(engine_handle resource);
engine_status ResourceStatusFn(engine_handle resource, engine_resource_info* out);
engine_status ResourceLoadAsyncFn(engine_handle engine, u64 id, engine_string path, u32 type, u32 priority,
                                  engine_handle* ticket);
engine_status TicketPollFn(engine_handle ticket, engine_ticket_info* out);
engine_status TicketWaitFn(engine_handle ticket, u64 timeout_ms);
engine_status TicketFinishFn(engine_handle ticket, engine_handle* resource);
engine_status TicketCancelFn(engine_handle ticket);
engine_status TicketDestroyFn(engine_handle ticket);

}  // namespace engine_placeholder::engine_c
