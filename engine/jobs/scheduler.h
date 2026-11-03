#pragma once

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <mutex>

#include "jobs/deque.h"
#include "jobs/job.h"
#include "jobs/worker_pool.h"
#include "types.h"

// Phase 2 Day 31: the scheduler — design decisions D1 (main helps only
// while waiting), D4 (idle strategy), D5 (injection queue), D6 (victim
// selection). An ordinary object, not a global.
//
// WORK ACQUISITION ORDER (workers and main-in-HelpUntil alike):
//   1. own deque, LIFO;
//   2. the injection queue (mutex-guarded ring);
//   3. a bounded number of steals from random victims (per-participant
//      xorshift32 seeded from the participant index).
// A worker whose round found nothing yields a bounded number of times and
// then PARKS on a condition variable.
//
// SUBMISSION. A participant thread pushes onto its own deque and, if that
// is full, spills to the injection queue. Any other thread (an I/O
// thread, a test's plain std::thread) uses the injection queue. If the
// injection queue is also full: participant 0 outside a job (the main
// thread) runs queued work until there is room (backpressure — this is
// what lets N == 1 submit more jobs than the queues hold before it ever
// calls HelpUntil) until the injection queue has kBackpressureSlack free
// slots, so jobs that run during that help can themselves fan out;
// any other submitter (a job on a worker, a job run by HelpUntil, a
// non-participant thread) hits ENGINE_ASSERT (a capacity bug, never
// silent loss). Backpressure only helps leaf-like submissions: a
// wide fan-out tree that saturates BOTH queues cannot be made to fit by
// running more of it (each parent run adds children faster than leaves
// retire), so such workloads must keep their outstanding job count under
// the combined capacity (deque + injection, ~8K per participant/queue).
//
// LOST-WAKEUP PROTOCOL. `submit_seq_` is bumped by every submission AFTER
// the work is made visible. A worker about to park (1) reads
// `submit_seq_` = s0, (2) runs a full acquire round that finds nothing,
// (3) after one last deterministic sweep that tries every other lane
// (retrying lost CAS races), takes `park_mutex_`, increments `parked_` and re-reads
// `submit_seq_`; if it is no longer s0 (or shutdown was requested) it
// does not wait, else it waits on `park_cv_` (releasing the mutex
// atomically). A submitter (a) publishes the work, (b) bumps
// `submit_seq_`, (c) reads `parked_` and, if non-zero, takes
// `park_mutex_` and notifies one waiter. All those accesses are seq_cst,
// so either the worker's re-read sees the bump (it doesn't sleep and its
// next round sees the work), or the submitter's read of `parked_` sees
// the worker (it blocks on the mutex until the worker is really waiting,
// then notifies). A submission racing a worker deciding to park therefore
// cannot be stranded.
//
// SHUTDOWN. Submitting concurrently with or after Shutdown() is a caller
// bug (asserted once shutdown has completed). Sets a flag, wakes everyone, joins. A job running at that
// moment finishes; jobs still queued are never run — their frames are
// destroyed (so the frame pool balances) and counted in the return value
// of Shutdown(). Callers join their work (HelpUntil) before destroying.
namespace engine_placeholder::jobs {

class Scheduler {
public:
    struct Config {
        usize participants = 0; // 0 = DefaultParticipantCount() (env override aware)
        bool pin = true;
    };

    explicit Scheduler(const Config& config);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Takes ownership of the job's frame (Job::Release). Any thread.
    void Submit(Job&& job);

    // Queues an already-suspended coroutine to be resumed on a
    // participant (used by Submit and by Counter to reschedule a
    // continuation). Any thread.
    void Schedule(std::coroutine_handle<> h);

    // Participant 0 (main) only, and never from inside a running job:
    // runs jobs (same acquire order as a worker) until `done(user)` is
    // true, then returns.
    void HelpUntil(bool (*done)(void* user), void* user);
    // Convenience: until `value >= target`.
    void HelpUntil(const std::atomic<int>& value, int target);

    // Stops the workers and destroys never-run queued jobs. Idempotent;
    // also run by the destructor. Returns the number of queued handles
    // that were destroyed without running.
    usize Shutdown();

    usize ParticipantCount() const { return pool_->ParticipantCount(); }
    const WorkerPool& Pool() const { return *pool_; }

    // The scheduler the calling thread participates in, or nullptr.
    static Scheduler* Current();
    // True while the calling thread is inside a job's resume().
    static bool InsideJob();

    // Statistics (relaxed counters; exact once the system is quiescent).
    u64 ResumesBy(usize participant) const;
    u64 StealsBy(usize participant) const;
    u64 TotalResumes() const;

    // Testing aids.
    u32 ParkedWorkersForTesting() const { return parked_.load(std::memory_order_seq_cst); }
    usize InjectionQueueSizeForTesting();
    static constexpr usize InjectionCapacity() { return kInjectionCapacity; }

private:
    static constexpr usize kInjectionCapacity = 4096;
    static constexpr int kYieldRounds = 64;
    static constexpr usize kBackpressureSlack = 512;

    struct Lane {
        WorkStealingDeque deque;
        u32 rng = 1;
        std::atomic<u64> resumes{0};
        std::atomic<u64> steals{0};
    };

    static void WorkerMain(void* user, Participant& self);
    void WorkerLoop(Participant& self);

    // One acquisition round for participant `index`; null handle if none.
    std::coroutine_handle<> FindWork(usize index);
    std::coroutine_handle<> PopInjection();
    bool PushInjection(std::coroutine_handle<> h);
    std::coroutine_handle<> TrySteal(usize index);
    std::coroutine_handle<> SweepAll(usize index);
    void MakeRoom();
    u32 NextRandom(Lane& lane);

    void Run(usize index, std::coroutine_handle<> h);
    void NotifyWork();
    bool IsMainThread() const;
    int OwnIndex() const; // participant index on this scheduler, or -1

    usize count_ = 1;
    Lane* lanes_ = nullptr;
    WorkerPool* pool_ = nullptr;

    // Injection queue (ring), guarded by injection_mutex_.
    std::mutex injection_mutex_;
    std::coroutine_handle<> injection_[kInjectionCapacity];
    usize injection_head_ = 0;
    usize injection_count_ = 0;
    std::atomic<usize> injection_hint_{0}; // lock-free "maybe non-empty"

    // Idle parking.
    std::mutex park_mutex_;
    std::condition_variable park_cv_;
    std::atomic<u64> submit_seq_{0};
    std::atomic<u32> parked_{0};
    std::atomic<bool> shutdown_{false};
    bool shut_down_ = false;               // Shutdown() entered (owner thread)
    std::atomic<bool> drained_{false};     // Shutdown() finished sweeping
};

} // namespace engine_placeholder::jobs
