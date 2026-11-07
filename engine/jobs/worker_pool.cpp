#include "jobs/worker_pool.h"

#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdio>

#include "log/diagnostics_hooks.h"
#include "jobs/jobs_log.h"
#include "log/assert.h"

namespace engine_placeholder::jobs {

namespace {
thread_local const Participant* tls_current = nullptr;

} // namespace

const Participant& WorkerPool::GetParticipant(usize i) const {
    ENGINE_ASSERT(i < count_, "WorkerPool::GetParticipant index out of range");
    return participants_[i];
}

const Participant* WorkerPool::Current() { return tls_current; }

i32 WorkerPool::CurrentParticipantIndex() {
    return tls_current != nullptr ? static_cast<i32>(tls_current->index) : -1;
}

WorkerPool::WorkerPool(const Config& config) : config_(config) {
    count_ = config.participants < 1 ? 1 : config.participants;
    if (count_ > kMaxParticipants) {
        count_ = kMaxParticipants;
    }
    allowed_count_ = GetAllowedCpus(allowed_, kMaxParticipants);

    // Participant 0 = the calling thread.
    participants_[0].index = 0;
    participants_[0].cpu = -1;
    participants_[0].tid = static_cast<i32>(::syscall(SYS_gettid));
    participants_[0].owner = this;
    constructor_thread_ = std::this_thread::get_id();
    saved_main_identity_ = tls_current;
    tls_current = &participants_[0];

    // Assign CPUs: allowed list excluding its first entry (left to main).
    // With a single allowed CPU everyone shares it.
    usize workers = count_ - 1;
    usize pool_cpus = allowed_count_ > 1 ? allowed_count_ - 1 : 1;
    for (usize i = 1; i < count_; ++i) {
        participants_[i].index = static_cast<u32>(i);
        participants_[i].owner = this;
        if (config_.pin) {
            usize slot = (i - 1) % pool_cpus;
            participants_[i].cpu = static_cast<i32>(allowed_count_ > 1 ? allowed_[1 + slot] : allowed_[0]);
        }
    }
    if (config_.pin && workers > pool_cpus) {
        JobsLog(LogVerbosity::Warn, "%zu workers share %zu CPUs: pinning is shared", static_cast<size_t>(workers),
                static_cast<size_t>(pool_cpus));
    }

    for (usize i = 1; i < count_; ++i) {
        threads_[i] = std::thread(&WorkerPool::ThreadMain, this, i);
    }
    // Wait until every worker has registered its identity.
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] { return registered_ == workers; });
    }
    JobsLog(LogVerbosity::Info, "job pool started: %zu participants = %zu spawned workers + main",
            static_cast<size_t>(count_), static_cast<size_t>(workers));
}

WorkerPool::~WorkerPool() {
    RequestStop();
    Join();
    ENGINE_ASSERT(std::this_thread::get_id() == constructor_thread_,
                  "WorkerPool must be destroyed on the thread that constructed it");
    ENGINE_ASSERT(tls_current == &participants_[0], "overlapping WorkerPools must be destroyed in reverse order");
    tls_current = saved_main_identity_;
}

void WorkerPool::ThreadMain(WorkerPool* pool, usize index) {
    {
        // A crash report names this thread, and a stack overflow on it can still be handled.
        char name[24];
        std::snprintf(name, sizeof(name), "worker-%zu", static_cast<size_t>(index));
        NotifyThreadStart(name);
    }
    Participant& self = pool->participants_[index];
    self.tid = static_cast<i32>(::syscall(SYS_gettid));
    if (self.cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(static_cast<unsigned>(self.cpu), &set);
        if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) {
            JobsLog(LogVerbosity::Warn, "worker %zu: could not pin to CPU %d", static_cast<size_t>(index), self.cpu);
            self.cpu = -1;
        }
    }
    tls_current = &self;
    {
        std::lock_guard<std::mutex> lock(pool->mutex_);
        ++pool->registered_;
    }
    pool->cv_.notify_all();

    if (pool->config_.worker_fn != nullptr) {
        pool->config_.worker_fn(pool->config_.user, self);
    } else {
        std::unique_lock<std::mutex> lock(pool->mutex_);
        pool->cv_.wait(lock, [&] { return pool->stop_.load(std::memory_order_acquire); });
    }
    tls_current = nullptr;
}

void WorkerPool::RequestStop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
}

void WorkerPool::Join() {
    if (joined_.exchange(true)) {
        return;
    }
    for (usize i = 1; i < count_; ++i) {
        if (threads_[i].joinable()) {
            threads_[i].join();
        }
    }
}

} // namespace engine_placeholder::jobs
