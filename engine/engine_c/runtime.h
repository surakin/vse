#pragma once
//! The runtime of one engine (Phase 23 Day 349): its worlds, its counters and its job system, behind the functions of
//! the C API's table that make and use them. Internal to the module `engine_c`: nothing here reaches the host but
//! through `engine_c.h`.
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/world.h"
#include "engine_c/engine_c.h"
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

private:
    static void Main(JobHost* self);

    Mutex mutex_;
    ConditionVariable ready_changed_;
    bool ready_ = false;
    Atomic<int> stop_{0};
    jobs::Scheduler* scheduler_ = nullptr;
    Thread thread_;
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
    /// Jobs submitted and not yet run to their end. The scheduler aborts when its queues are full, so a submit that
    /// would pass `kMaxJobsInFlight` is refused.
    Atomic<u32> jobs_in_flight{0};
    static constexpr u32 kMaxJobsInFlight = 2048;
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
