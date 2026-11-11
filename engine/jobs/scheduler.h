#pragma once

#include "sync/atomic.h"
#include "sync/mutex.h"
#include "jobs/coroutine.h"

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "jobs/deque.h"
#include "jobs/job.h"
#include "jobs/worker_pool.h"
#include "types.h"

//! The work-stealing job scheduler.
//!
//! An ordinary object, not a global. Participant 0 is the thread that
//! constructs it ("main"), which helps run jobs only while waiting; the other
//! participants are pinned worker threads.
//!
//! Work acquisition order, for workers and for main inside
//! [[jobs::Scheduler::HelpUntil(Counter&)]] alike:
//! - the participant's own deque, LIFO;
//! - the injection queue (a mutex-guarded ring);
//! - a bounded number of steals from random victims (per-participant
//!   xorshift32 seeded from the participant index).
//!
//! A worker whose round found nothing yields a bounded number of times and
//! then parks on a condition variable.
//!
//! Submission. A participant thread pushes onto its own deque and, if that is
//! full, spills to the injection queue. Any other thread (an I/O thread, a
//! test's plain `std::thread`) uses the injection queue. If the injection
//! queue is also full, participant 0 outside a job (the main thread) runs
//! queued work until the injection queue has `kBackpressureSlack` (512) free
//! slots, so that jobs run during that help can themselves fan out. This
//! backpressure is what lets a single-participant scheduler submit more jobs
//! than the queues hold before it ever calls `HelpUntil`. Any other
//! submitter (a job on a worker, a job run by `HelpUntil`, a non-participant
//! thread) aborts: a capacity bug, never silent loss.
//!
//! Backpressure only helps leaf-like submissions. A wide fan-out tree that
//! saturates both queues cannot be made to fit by running more of it, since
//! each parent run adds children faster than leaves retire, so such workloads
//! must keep their outstanding job count under the combined capacity (deque
//! plus injection queue, about 8K per participant and queue).
//!
//! Shutdown. Coroutines suspended in a [[jobs::Counter]] await are in no
//! queue, so [[jobs::Scheduler::Shutdown]] cannot free them: callers must
//! join (`HelpUntil` or await) all outstanding work before destroying the
//! scheduler, or those frames leak. Submitting concurrently with or after
//! shutdown is a caller bug (asserted once shutdown has completed).
namespace engine_placeholder::jobs
{

class Counter;
class IoThread;

/// Work-stealing scheduler that runs [[jobs::Job]] coroutines on a pool of pinned worker threads.
///
/// Construction starts the workers and the dedicated I/O thread
/// ([[jobs::IoThread]]); the constructing thread becomes participant 0. It
/// cannot be copied, and it must be destroyed on the constructing thread.
/// Join all outstanding work before destroying it.
///
/// - Thread-safety: thread-safe for [[jobs::Scheduler::Submit]],
///   [[jobs::Scheduler::Schedule]], the accessors and the statistics, which any
///   thread may call. [[jobs::Scheduler::HelpUntil(Counter&)]] is limited to participant 0
///   outside a job, and [[jobs::Scheduler::Shutdown]] and destruction to the owning thread.
/// - Ownership: owns the worker pool, the per-participant deques and the I/O
///   thread. Frames of submitted jobs belong to the scheduler until they run to
///   completion or [[jobs::Scheduler::Shutdown]] destroys them unrun.
/// - See: [[jobs::Counter]], [[jobs::Spawn]], [[jobs::WorkerPool]]
class Scheduler
{
public:
    /// Scheduler construction parameters.
    ///
    /// - Thread-safety: not-thread-safe. A plain value type.
    struct Config
    {
        /// Number of participants including main; 0 selects the default.
        ///
        /// The default is [[jobs::DefaultParticipantCount]], which honours the
        /// `ENGINE_JOB_THREADS` override. Clamped to [[jobs::kMaxParticipants]].
        usize participants = 0;
        /// Pin each spawned worker to a distinct allowed CPU.
        bool pin = true;
    };

    /// Starts the workers and the I/O thread.
    ///
    /// The calling thread becomes participant 0 and must also destroy the scheduler.
    ///
    /// - Param config: participant count and pinning.
    explicit Scheduler(const Config& config);

    /// Shuts the scheduler down (see [[jobs::Scheduler::Shutdown]]) and releases all its resources.
    ///
    /// Join outstanding work first, or suspended frames leak and queued jobs are destroyed unrun.
    ///
    /// - Aborts: not called on the thread that constructed the scheduler.
    /// - Ownership: destroys the I/O thread, the worker pool and the deques.
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    /// Queues a job to run on a participant.
    ///
    /// The scheduler takes over the job's frame through [[jobs::Job::Release]],
    /// which leaves `job` empty. See the class overview for what happens when
    /// the queues are full.
    ///
    /// - Param job: the job to run; must be non-empty. It is moved from.
    /// - Aborts: `job` is empty; the scheduler has completed shutdown; both queues are full and the caller is not
    /// participant 0 outside a job.
    /// - Ownership: takes ownership of the job's frame, which destroys itself on completion.
    /// - Thread-safety: thread-safe. Any thread.
    void Submit(Job&& job);

    /// Queues an already-suspended coroutine to be resumed on a participant.
    ///
    /// Used by [[jobs::Scheduler::Submit]] and by [[jobs::Counter]] to
    /// reschedule a continuation. A participant thread pushes onto its own
    /// deque, falling back to the injection queue; other threads use the
    /// injection queue.
    ///
    /// - Param h: the suspended coroutine to resume; must not be null and must not be queued already.
    /// - Aborts: `h` is null; the scheduler has completed shutdown; both queues are full and the caller is not
    /// participant 0 outside a job.
    /// - Ownership: takes responsibility for resuming `h` exactly once, or destroying it if shutdown drops it unrun.
    /// - Thread-safety: thread-safe. Any thread.
    void Schedule(CoroutineHandle<> h);

    /// Runs jobs on the calling thread until a predicate becomes true.
    ///
    /// Uses the same acquisition order as a worker, then returns. Jobs wait
    /// with `co_await`; this is how the main thread waits.
    ///
    /// - Param done: predicate polled between jobs; returns `true` when the wait is over.
    /// - Param user: passed to `done` unchanged.
    /// - Aborts: not called from participant 0 (the main thread), or called from inside a running job.
    /// - Ownership: `user` is borrowed, not owned.
    /// - Thread-safety: owner-thread-only. Participant 0, outside any job.
    void HelpUntil(bool (*done)(void* user), void* user);

    /// Runs jobs on the calling thread until a counter reaches zero.
    ///
    /// - Param counter: the counter to wait on; must outlive the call.
    /// - Aborts: not called from participant 0 (the main thread), or called from inside a running job.
    /// - Thread-safety: owner-thread-only. Participant 0, outside any job.
    void HelpUntil(Counter& counter);

    /// Runs jobs on the calling thread until an atomic reaches a target value.
    ///
    /// - Param value: the atomic to watch; read with acquire ordering.
    /// - Param target: the wait ends once `value >= target`.
    /// - Aborts: not called from participant 0 (the main thread), or called from inside a running job.
    /// - Thread-safety: owner-thread-only. Participant 0, outside any job.
    void HelpUntil(const Atomic<int>& value, int target);

    /// Stops the I/O thread and the workers and destroys jobs that never ran.
    ///
    /// The I/O thread drains first, so in-flight reads finish and their
    /// continuations are scheduled while the workers still run. Then a flag is
    /// set, everyone is woken and the workers are joined. A job running at that
    /// moment finishes; jobs still queued are never run: their frames are
    /// destroyed (so the frame pool balances) and counted in the result. A
    /// warning is logged if any were dropped. Idempotent; also run by the
    /// destructor. Coroutines parked in a counter await are in no queue and are
    /// not freed, so join outstanding work first.
    ///
    /// - Returns: the number of queued handles destroyed without running; 0 on any call after the first.
    /// - Ownership: destroys the frames of every queued job that never ran.
    /// - Thread-safety: owner-thread-only. Do not call it concurrently with submissions or from several threads.
    usize Shutdown();

    /// Gives the dedicated, non-participant I/O thread.
    ///
    /// It is started with the scheduler and stopped by
    /// [[jobs::Scheduler::Shutdown]] before the workers.
    ///
    /// - Returns: the I/O thread, valid for the scheduler's lifetime.
    /// - Thread-safety: thread-safe.
    IoThread& Io() { return *io_; }

    /// Gives the number of participants, including main.
    ///
    /// - Returns: the participant count, at least 1.
    usize ParticipantCount() const { return pool_->ParticipantCount(); }
    /// Gives the worker pool behind the participants.
    ///
    /// - Returns: the pool, valid for the scheduler's lifetime.
    const WorkerPool& Pool() const { return *pool_; }

    /// Finds the scheduler the calling thread participates in.
    ///
    /// - Returns: the scheduler, or null on a thread that is not a participant of any scheduler.
    /// - Ownership: not transferred; the pointer is valid while the scheduler lives.
    /// - Thread-safety: thread-safe.
    static Scheduler* Current();

    /// Tells whether the calling thread is inside a job's resume.
    ///
    /// - Returns: `true` while a job runs on the calling thread.
    /// - Thread-safety: thread-safe.
    static bool InsideJob();

    /// Counts the coroutine resumes performed by one participant.
    ///
    /// Statistics use relaxed counters and are exact once the system is quiescent.
    ///
    /// - Param participant: participant index, which must be below [[jobs::Scheduler::ParticipantCount]]; not checked.
    /// - Returns: number of resumes so far.
    /// - Thread-safety: thread-safe.
    u64 ResumesBy(usize participant) const;

    /// Counts the successful steals performed by one participant.
    ///
    /// - Param participant: participant index, which must be below [[jobs::Scheduler::ParticipantCount]]; not checked.
    /// - Returns: number of steals so far.
    /// - Thread-safety: thread-safe.
    u64 StealsBy(usize participant) const;

    /// Counts the coroutine resumes across all participants.
    ///
    /// - Returns: the sum of the per-participant resume counts.
    /// - Thread-safety: thread-safe.
    u64 TotalResumes() const;

    /// Counts the workers currently parked on the idle condition variable; a testing aid.
    ///
    /// - Returns: number of parked workers at the moment of the call.
    /// - Thread-safety: thread-safe.
    u32 ParkedWorkersForTesting() const { return parked_.Load(MemoryOrder::SeqCst); }
    /// Counts the entries in the injection queue; a testing aid.
    ///
    /// - Returns: the current queue length.
    /// - Thread-safety: thread-safe. Takes the queue lock.
    usize InjectionQueueSizeForTesting();

    /// Gives the capacity of the injection queue.
    ///
    /// - Returns: the maximum number of handles it holds.
    static constexpr usize InjectionCapacity() { return kInjectionCapacity; }

private:
    static constexpr usize kInjectionCapacity = 4096;
    static constexpr int kYieldRounds = 64;
    static constexpr usize kBackpressureSlack = 512;

    struct Lane
    {
        WorkStealingDeque deque;
        u32 rng = 1;
        Atomic<u64> resumes{0};
        Atomic<u64> steals{0};
    };

    static void WorkerMain(void* user, Participant& self);
    void WorkerLoop(Participant& self);

    // One acquisition round for participant `index`; null handle if none.
    CoroutineHandle<> FindWork(usize index);
    CoroutineHandle<> PopInjection();
    bool PushInjection(CoroutineHandle<> h);
    CoroutineHandle<> TrySteal(usize index);
    CoroutineHandle<> SweepAll(usize index);
    void MakeRoom();
    u32 NextRandom(Lane& lane);

    void Run(usize index, CoroutineHandle<> h);
    void NotifyWork();
    bool IsMainThread() const;
    int OwnIndex() const;  // participant index on this scheduler, or -1

    usize count_ = 1;
    Vector<Lane> lanes_;
    UniquePtr<WorkerPool> pool_;
    UniquePtr<IoThread> io_;

    // Injection queue (ring), guarded by injection_mutex_.
    Mutex injection_mutex_;
    CoroutineHandle<> injection_[kInjectionCapacity];
    usize injection_head_ = 0;
    usize injection_count_ = 0;
    Atomic<usize> injection_hint_{0};  // lock-free "maybe non-empty"

    // LOST-WAKEUP PROTOCOL. `submit_seq_` is bumped by every submission AFTER
    // the work is made visible. A worker about to park (1) reads
    // `submit_seq_` = s0, (2) runs a full acquire round that finds nothing,
    // (3) after one last deterministic sweep that tries every other lane
    // (retrying lost CAS races), takes `park_mutex_`, increments `parked_` and
    // re-reads `submit_seq_`; if it is no longer s0 (or shutdown was requested)
    // it does not wait, else it waits on `park_cv_` (releasing the mutex
    // atomically). A submitter (a) publishes the work, (b) bumps
    // `submit_seq_`, (c) reads `parked_` and, if non-zero, takes
    // `park_mutex_` and notifies one waiter. All those accesses are seq_cst,
    // so either the worker's re-read sees the bump (it doesn't sleep and its
    // next round sees the work), or the submitter's read of `parked_` sees
    // the worker (it blocks on the mutex until the worker is really waiting,
    // then notifies). A submission racing a worker deciding to park therefore
    // cannot be stranded.
    // Idle parking.
    Mutex park_mutex_;
    ConditionVariable park_cv_;
    Atomic<u64> submit_seq_{0};
    Atomic<u32> parked_{0};
    Atomic<bool> shutdown_{false};
    bool shut_down_ = false;       // Shutdown() entered (owner thread)
    Atomic<bool> drained_{false};  // Shutdown() finished sweeping
};

}  // namespace engine_placeholder::jobs
