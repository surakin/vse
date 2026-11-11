#pragma once

#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"

#include "jobs/topology.h"
#include "types.h"

//! The participants of the job system and the threads behind them.
//!
//! `N` participants are participant 0 (the thread that constructs the pool:
//! "main", never pinned, never looping here) plus `N - 1` spawned workers
//! pinned to distinct allowed CPUs. One participant spawns no threads. Each
//! participant thread has a thread-local identity so any code can ask which
//! participant, if any, it is running on.
namespace engine_placeholder::jobs {

/// Identity of one job-system participant thread.
///
/// - Thread-safety: immutable. Written once during pool construction, before any worker body runs.
/// - Ownership: owned by its [[jobs::WorkerPool]]; references stay valid for the pool's lifetime.
struct Participant {
    /// Position of this participant in the pool; 0 is the main thread.
    u32 index = 0;
    /// CPU id the thread is pinned to, or -1 for main or an unpinned worker.
    i32 cpu = -1;
    /// OS thread id (Linux: the kernel tid, for `/proc`; Windows: GetCurrentThreadId; macOS: the low 31 bits of `pthread_threadid_np`, never 0,
    /// for logs and tests only: no OS call takes it, and after 2^31 threads since boot two can share it).
    i32 tid = 0;
    /// The [[jobs::WorkerPool]] this participant belongs to, type-erased.
    void* owner = nullptr;
};

/// Owns the participant threads: participant 0 is the constructing thread and the others are spawned workers.
///
/// Construction returns once every worker has registered its identity. The
/// pool must be destroyed on the thread that constructed it, and
/// overlapping pools on one thread must be destroyed in reverse construction
/// order; both are asserted. It cannot be copied.
///
/// - Thread-safety: thread-safe for [[jobs::WorkerPool::RequestStop]],
///   [[jobs::WorkerPool::Join]] and the accessors; construction and destruction belong to
///   the constructing thread.
/// - Ownership: owns and joins the spawned worker threads; the destructor
///   requests stop and joins them.
/// - See: [[jobs::Scheduler]]
class WorkerPool {
public:
    /// Body run by each spawned worker after it registered itself.
    ///
    /// It returns when the worker should exit. A null body selects a default
    /// that parks until [[jobs::WorkerPool::RequestStop]].
    using WorkerFn = void (*)(void* user, Participant& self);

    /// Pool construction parameters.
    ///
    /// - Thread-safety: not-thread-safe. A plain value type.
    struct Config {
        /// Number of participants including main; clamped to `[1, kMaxParticipants]`.
        usize participants = 1;
        /// Pin each spawned worker to a distinct allowed CPU.
        bool pin = true;
        /// Body run by each spawned worker, or null for the default parking body.
        WorkerFn worker_fn = nullptr;
        /// Opaque pointer handed to `worker_fn` and returned by [[jobs::WorkerPool::User]]; not owned.
        void* user = nullptr;
    };

    /// Starts the pool: registers the calling thread as participant 0 and spawns the workers.
    ///
    /// - Param config: participant count, pinning and worker body.
    explicit WorkerPool(const Config& config);

    /// Requests stop and joins the workers (idempotent with [[jobs::WorkerPool::Join]]).
    ///
    /// - Aborts: not called on the constructing thread, or overlapping pools were not destroyed in reverse order.
    /// - Ownership: releases the worker threads.
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    /// Gives the opaque pointer from [[jobs::WorkerPool::Config::user]].
    ///
    /// - Returns: the configured pointer, possibly null.
    /// - Ownership: not owned by the pool; the caller of the constructor keeps it alive.
    void* User() const { return config_.user; }
    /// Gives the configured worker body.
    ///
    /// - Returns: the configured function, or null for the default body.
    WorkerFn WorkerFunction() const { return config_.worker_fn; }
    /// Gives the number of participants, including main.
    ///
    /// - Returns: the clamped participant count, at least 1.
    usize ParticipantCount() const { return count_; }
    /// Gives the number of spawned worker threads, which excludes main.
    ///
    /// - Returns: the participant count minus one.
    usize SpawnedWorkerCount() const { return count_ - 1; }
    /// Looks up a participant by index.
    ///
    /// - Param i: participant index in `[0, ParticipantCount())`.
    /// - Returns: the identity of participant `i`, valid for the pool's lifetime.
    /// - Aborts: `i` is out of range.
    const Participant& GetParticipant(usize i) const;

    /// Wakes the default worker body and sets [[jobs::WorkerPool::StopRequested]].
    ///
    /// Custom worker bodies are expected to watch their own state (the
    /// scheduler has its own shutdown flag) but may also poll
    /// [[jobs::WorkerPool::StopRequested]].
    ///
    /// - Thread-safety: thread-safe.
    void RequestStop();
    /// Tells whether a stop was requested.
    ///
    /// - Returns: `true` once [[jobs::WorkerPool::RequestStop]] has been called.
    /// - Thread-safety: thread-safe.
    bool StopRequested() const { return stop_.Load(MemoryOrder::Acquire); }

    /// Joins every spawned worker.
    ///
    /// Idempotent and safe to call concurrently: only the first caller joins.
    /// The workers must have been told to exit first, through
    /// [[jobs::WorkerPool::RequestStop]] or the scheduler's own shutdown. A
    /// custom worker body must return once it observes
    /// [[jobs::WorkerPool::StopRequested]] or its own shutdown condition,
    /// otherwise this call and the destructor block forever.
    ///
    /// - Thread-safety: thread-safe.
    void Join();

    /// Gives the identity of the calling thread.
    ///
    /// - Returns: the calling thread's participant record, or null on a non-participant thread.
    /// - Ownership: the record belongs to its pool; do not retain it beyond that pool's lifetime.
    static const Participant* Current();

    /// Tells whether the calling thread is a job-system participant.
    ///
    /// - Returns: `true` on participant threads (including main), `false` elsewhere.
    static bool IsParticipantThread() { return Current() != nullptr; }
    /// Gives the calling thread's participant index.
    ///
    /// - Returns: the index, or -1 on a non-participant thread.
    static i32 CurrentParticipantIndex();

private:
    static void ThreadMain(WorkerPool* pool, usize index);

    usize count_ = 1;
    Config config_;
    Participant participants_[kMaxParticipants];
    Thread threads_[kMaxParticipants];
    u32 allowed_[kMaxParticipants];
    usize allowed_count_ = 0;

    Mutex mutex_;
    ConditionVariable cv_;
    usize registered_ = 0; // guarded by mutex_
    Atomic<bool> stop_{false};
    Atomic<bool> joined_{false};
    ThreadId constructor_thread_;
    const Participant* saved_main_identity_ = nullptr;
};

} // namespace engine_placeholder::jobs
