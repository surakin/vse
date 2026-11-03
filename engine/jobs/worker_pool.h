#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "jobs/topology.h"
#include "types.h"

// Phase 2 Day 29 (design decision D1): the participants and the threads
// behind them. `N` participants = participant 0 (the thread that
// constructs the pool — "main", never pinned, never looping here) plus
// N-1 spawned workers pinned to distinct allowed CPUs. N == 1 spawns no
// threads. Each participant thread has a thread-local identity so any
// code can ask which participant (if any) it is running on.
namespace engine_placeholder::jobs {

struct Participant {
    u32 index = 0;
    i32 cpu = -1;  // pinned CPU id, or -1 (main / unpinned)
    i32 tid = 0;   // OS thread id (for /proc inspection)
    void* owner = nullptr; // the WorkerPool this participant belongs to
};

class WorkerPool {
public:
    // Body run by each spawned worker after it registered itself.
    // Returns when the worker should exit. Null selects a default body
    // that parks until RequestStop() (Day 29's lifecycle-only pool).
    using WorkerFn = void (*)(void* user, Participant& self);

    struct Config {
        usize participants = 1; // clamped to [1, kMaxParticipants]
        bool pin = true;
        WorkerFn worker_fn = nullptr;
        void* user = nullptr;
    };

    explicit WorkerPool(const Config& config);
    // Requests stop and joins (idempotent with Join()).
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void* User() const { return config_.user; }
    WorkerFn WorkerFunction() const { return config_.worker_fn; }
    usize ParticipantCount() const { return count_; }
    usize SpawnedWorkerCount() const { return count_ - 1; }
    const Participant& GetParticipant(usize i) const;

    // Wakes the default worker body and sets StopRequested(). Custom
    // worker bodies are expected to watch their own state (the Scheduler
    // has its own shutdown flag) but may also poll StopRequested().
    void RequestStop();
    bool StopRequested() const { return stop_.load(std::memory_order_acquire); }

    // Joins every spawned worker; idempotent and safe to call
    // concurrently (only the first caller joins). The workers must have
    // been told to exit first (RequestStop(), or the Scheduler's own
    // shutdown): a custom worker_fn MUST return once StopRequested() or
    // its own shutdown condition is observed, or Join()/~WorkerPool block
    // forever. The pool must be destroyed on the thread that constructed
    // it (that thread is participant 0) and overlapping pools on one
    // thread must be destroyed in reverse construction order; both are
    // asserted.
    void Join();

    // Thread-local identity. nullptr / false / -1 on non-participants.
    static const Participant* Current();
    static bool IsParticipantThread() { return Current() != nullptr; }
    static i32 CurrentParticipantIndex();

private:
    static void ThreadMain(WorkerPool* pool, usize index);

    usize count_ = 1;
    Config config_;
    Participant participants_[kMaxParticipants];
    std::thread threads_[kMaxParticipants];
    u32 allowed_[kMaxParticipants];
    usize allowed_count_ = 0;

    std::mutex mutex_;
    std::condition_variable cv_;
    usize registered_ = 0; // guarded by mutex_
    std::atomic<bool> stop_{false};
    std::atomic<bool> joined_{false};
    std::thread::id constructor_thread_;
    const Participant* saved_main_identity_ = nullptr;
};

} // namespace engine_placeholder::jobs
