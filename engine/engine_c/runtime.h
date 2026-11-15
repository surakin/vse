#pragma once
//! The runtime of one engine (Phase 23 Day 349): its worlds, its counters and its job system, behind the functions of
//! the C API's table that make and use them. Internal to the module `engine_c`: nothing here reaches the host but
//! through `engine_c.h`.
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/world.h"
#include "engine/engine_c.h"
#include "jobs/job.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

namespace engine_placeholder::jobs
{
class Scheduler;
}

namespace engine_placeholder::engine_c
{

/// A world and the systems that run on it.
///
/// - Thread-safety: thread-compatible; the runtime's world mutex guards every use.
struct WorldObject
{
    WorldObject();
    ecs::World world;
    ecs::Schedule schedule;
};

/// A count of jobs not yet finished.
///
/// - Thread-safety: thread-safe; `pending` is read and written under `mutex`.
struct CounterObject
{
    Mutex mutex;
    ConditionVariable changed;
    u64 pending = 0;
    u32 waiters = 0;  // guarded by the runtime's table mutex, not by `mutex`

    /// Counts a job that ended and wakes the waiters when none is left.
    ///
    /// - Thread-safety: thread-safe.
    /// - Fails: never.
    void Done();
};

/// The job system of an engine: a scheduler on a thread of its own (participant 0 sleeps between jobs, it is not the
/// host's thread), started by the first job submitted and stopped when the engine is destroyed.
///
/// - Thread-safety: `Submit` is thread-safe; the constructor and destructor belong to the runtime's lifetime.
class JobHost
{
public:
    /// Starts the thread and waits until the scheduler runs.
    JobHost();
    /// Stops the scheduler and joins the thread. Every counter is at zero by then (the engine refuses to be destroyed
    /// otherwise).
    ~JobHost();
    JobHost(const JobHost&) = delete;
    JobHost& operator=(const JobHost&) = delete;

    /// Queues a job.
    ///
    /// - Thread-safety: thread-safe.
    void Submit(jobs::Job&& job);

    /// The scheduler (it runs from the constructor's return until the destructor).
    ///
    /// - Thread-safety: thread-safe.
    jobs::Scheduler* GetScheduler() const { return scheduler_; }

private:
    static void Main(JobHost* self);

    Mutex mutex_;
    ConditionVariable ready_changed_;
    bool ready_ = false;
    Atomic<int> stop_{0};
    jobs::Scheduler* scheduler_ = nullptr;
    Thread thread_;
};

class ResourceHost;
class SurfaceHost;

/// The limits of a glTF import, read from the `assets.import.*` settings when the engine is created (all 0 when the
/// importer is not built in).
///
/// - Thread-safety: thread-compatible plain data.
struct ImportLimitValues
{
    u64 max_file_bytes = 0;    ///< `assets.import.max_file_bytes`.
    u64 max_json_bytes = 0;    ///< `assets.import.max_json_bytes`.
    u64 max_buffer_bytes = 0;  ///< `assets.import.max_buffer_bytes`.
    u64 max_output_bytes = 0;  ///< `assets.import.max_output_bytes`.
    u64 max_pixels = 0;        ///< `assets.import.max_pixels`.
    u32 max_nodes = 0;         ///< `assets.import.max_nodes`.
    u32 max_accessors = 0;     ///< `assets.import.max_accessors`.
    u32 max_images = 0;        ///< `assets.import.max_images`.
};

/// The settings the resource host is made from (read when the engine is created).
///
/// - Thread-safety: thread-compatible plain data.
struct ResourceConfig
{
    u64 memory_bytes = u64{512} << 20;       ///< `assets.memory_backend_bytes`.
    i32 priority = 1000;                     ///< `assets.memory_priority`.
    u64 buffer_pool_bytes = u64{256} << 20;  ///< `assets.buffer_pool_bytes`.
    u64 cache_bytes = 0;                     ///< `assets.cache_bytes`.
    bool allow_fallbacks = false;            ///< `assets.allow_fallbacks`.
    bool verify_on_load = false;             ///< `assets.verify_on_load`.
    u32 max_inflight = 4;                    ///< `assets.max_inflight`.
    u32 max_queued = 256;                    ///< `assets.max_queued`.
    ImportLimitValues import_limits;  ///< The limits of a glTF import (`assets.import.*`); all 0 without the importer.
};

/// What an engine holds of worlds, counters and jobs.
///
/// - Thread-safety: thread-safe; see the mutexes.
class Runtime
{
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /// The world calls and the frame run under this mutex: they are serialized with each other.
    Mutex world_mutex;
    struct WorldSlot
    {
        UniquePtr<WorldObject> object;
        u32 generation = 0;
    };
    Vector<WorldSlot> worlds;  // guarded by world_mutex
    u64 frames = 0;            // guarded by world_mutex
    usize cursor = 0;  // the world a budgeted frame stopped before: the next frame starts there; guarded by world_mutex

    /// Guards `counters`, the counters' `waiters` and the start of `jobs`.
    Mutex table_mutex;
    struct CounterSlot
    {
        UniquePtr<CounterObject> object;
        u32 generation = 0;
    };
    Vector<CounterSlot> counters;
    UniquePtr<JobHost> jobs;

    /// Starts the job system if it is not running and returns it.
    ///
    /// - Thread-safety: thread-safe.
    JobHost& EnsureJobs();

    /// What the resource host is made from; set once when the engine is created, before any call.
    ResourceConfig resource_config;

    /// The resources of the engine: made by the first call that needs them.
    ///
    /// - Thread-safety: thread-safe.
    ResourceHost* Resources();

    /// The resources if they exist, else null.
    ///
    /// - Thread-safety: thread-safe.
    ResourceHost* ResourcesIfCreated();

    /// The surfaces of the engine (Day 352): made by the first call that needs them.
    ///
    /// - Thread-safety: thread-safe.
    SurfaceHost* Surfaces();

    /// The surfaces if they exist, else null.
    ///
    /// - Thread-safety: thread-safe.
    SurfaceHost* SurfacesIfCreated();
    /// Jobs submitted and not yet run to their end. The scheduler aborts when its queues are full, so a submit that
    /// would pass `kMaxJobsInFlight` is refused.
    Atomic<u32> jobs_in_flight{0};
    static constexpr u32 kMaxJobsInFlight = 2048;

    // Declared last, so it is destroyed first: its lent buffers are handed back on the job system's worker, and its
    // loads read on the job system's scheduler.
    UniquePtr<ResourceHost> resources;  // guarded by table_mutex at creation
    // After the resources: destroyed before them (a surface holds a device and a swapchain, nothing of the content).
    UniquePtr<SurfaceHost> surfaces;  // guarded by table_mutex at creation
};

// The functions of the table that the runtime serves (engine_c.cpp lists them in the table).
engine_status FrameFn(engine_handle engine, const engine_frame_info* info, engine_frame_stats* stats);
engine_status WorldCreateFn(engine_handle engine, engine_handle* out);
engine_status WorldDestroyFn(engine_handle world);
engine_status EntityCreateFn(engine_handle world, u64* out_key);
engine_status EntityDestroyFn(engine_handle world, u64 key);
engine_status ComponentSetFn(engine_handle world, u64 key, u32 kind, const void* value);
engine_status ComponentGetFn(engine_handle world, u64 key, u32 kind, void* out);
engine_status ComponentRemoveFn(engine_handle world, u64 key, u32 kind);
engine_status ComponentHasFn(engine_handle world, u64 key, u32 kind, int* out);
engine_status EntityNameSetFn(engine_handle world, u64 key, engine_string name);
engine_status EntityNameGetFn(engine_handle world, u64 key, engine_buffer* out);
engine_status QueryFn(engine_handle world, u32 mask, u64* keys, size_t capacity, size_t* total);
engine_status CounterCreateFn(engine_handle engine, engine_handle* out);
engine_status CounterDestroyFn(engine_handle counter);
engine_status JobsSubmitFn(engine_handle engine, engine_job_fn fn, void* user, engine_handle counter);
engine_status CounterValueFn(engine_handle counter, u64* out);
engine_status CounterWaitFn(engine_handle counter, u64 timeout_ms);

/// Makes a text buffer (a NUL after the bytes) the host releases with `engine_buffer_release`.
///
/// - Thread-safety: thread-safe.
/// - Fails: never.
void MakeTextBuffer(StringView text, engine_buffer* out);

}  // namespace engine_placeholder::engine_c
