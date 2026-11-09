#include "jobs/worker_pool.h"

#include <cstdio>

#include "jobs/detail/thread_platform.h"

#include "log/diagnostics_hooks.h"
#include "jobs/jobs_log.h"
#include "log/assert.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"

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
    participants_[0].tid = detail::CurrentThreadId();
    participants_[0].owner = this;
    constructor_thread_ = ThisThread::Id();
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
        threads_[i] = Thread(&WorkerPool::ThreadMain, this, i);
    }
    // Wait until every worker has registered its identity.
    {
        UniqueLock<Mutex> lock(mutex_);
        cv_.Wait(lock, [&] { return registered_ == workers; });
    }
    JobsLog(LogVerbosity::Info, "job pool started: %zu participants = %zu spawned workers + main",
            static_cast<size_t>(count_), static_cast<size_t>(workers));
}

WorkerPool::~WorkerPool() {
    RequestStop();
    Join();
    ENGINE_ASSERT(ThisThread::Id() == constructor_thread_,
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
        detail::NameCurrentThread(name);
    }
    Participant& self = pool->participants_[index];
    self.tid = detail::CurrentThreadId();
    if (self.cpu >= 0) {
        if (!detail::PinCurrentThread(static_cast<u32>(self.cpu))) {
            JobsLog(LogVerbosity::Warn, "worker %zu: could not pin to CPU %d", static_cast<size_t>(index), self.cpu);
            self.cpu = -1;
        }
    }
    tls_current = &self;
    {
        LockGuard<Mutex> lock(pool->mutex_);
        ++pool->registered_;
    }
    pool->cv_.NotifyAll();

    if (pool->config_.worker_fn != nullptr) {
        pool->config_.worker_fn(pool->config_.user, self);
    } else {
        UniqueLock<Mutex> lock(pool->mutex_);
        pool->cv_.Wait(lock, [&] { return pool->stop_.Load(MemoryOrder::Acquire); });
    }
    tls_current = nullptr;
}

void WorkerPool::RequestStop() {
    {
        LockGuard<Mutex> lock(mutex_);
        stop_.Store(true, MemoryOrder::Release);
    }
    cv_.NotifyAll();
}

void WorkerPool::Join() {
    if (joined_.Exchange(true)) {
        return;
    }
    for (usize i = 1; i < count_; ++i) {
        if (threads_[i].Joinable()) {
            threads_[i].Join();
        }
    }
}

} // namespace engine_placeholder::jobs
