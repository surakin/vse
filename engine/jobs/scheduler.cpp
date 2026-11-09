#include "jobs/scheduler.h"

#include "sync/thread.h"

#include "jobs/async_io.h"
#include "jobs/counter.h"
#include "jobs/jobs_log.h"
#include "log/assert.h"
#include "sync/atomic.h"
#include "sync/mutex.h"

namespace engine_placeholder::jobs {

namespace {
thread_local int tls_job_depth = 0;
} // namespace

Scheduler::Scheduler(const Config& config) {
    usize n = config.participants != 0 ? config.participants : DefaultParticipantCount();
    if (n > kMaxParticipants) n = kMaxParticipants;
    count_ = n;
    lanes_.Resize(count_);
    for (usize i = 0; i < count_; ++i) {
        lanes_[i].rng = 0x9E3779B9u * static_cast<u32>(i + 1);
        if (lanes_[i].rng == 0) lanes_[i].rng = 1;
    }
    WorkerPool::Config pc;
    pc.participants = count_;
    pc.pin = config.pin;
    pc.worker_fn = &Scheduler::WorkerMain;
    pc.user = this;
    // The workers start running immediately and use lanes_/queues, which
    // are fully built above.
    pool_ = MakeUnique<WorkerPool>(pc);
    io_ = MakeUnique<IoThread>();
}

Scheduler::~Scheduler() {
    Shutdown();
    io_.Reset(); // the I/O thread first, then the workers it feeds
    pool_.Reset();
}

Scheduler* Scheduler::Current() {
    const Participant* p = WorkerPool::Current();
    if (p == nullptr || p->owner == nullptr) return nullptr;
    auto* pool = static_cast<WorkerPool*>(p->owner);
    // Only pools created by a Scheduler carry a Scheduler* in `user`.
    if (pool->WorkerFunction() != &Scheduler::WorkerMain) return nullptr;
    return static_cast<Scheduler*>(pool->User());
}

bool Scheduler::InsideJob() { return tls_job_depth > 0; }

int Scheduler::OwnIndex() const {
    const Participant* p = WorkerPool::Current();
    if (p != nullptr && p->owner == pool_.Get()) return static_cast<int>(p->index);
    return -1;
}

bool Scheduler::IsMainThread() const { return OwnIndex() == 0; }

u32 Scheduler::NextRandom(Lane& lane) {
    u32 x = lane.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    lane.rng = x;
    return x;
}

bool Scheduler::PushInjection(std::coroutine_handle<> h) {
    LockGuard<Mutex> lock(injection_mutex_);
    if (injection_count_ == kInjectionCapacity) return false;
    injection_[(injection_head_ + injection_count_) % kInjectionCapacity] = h;
    ++injection_count_;
    injection_hint_.Store(injection_count_, MemoryOrder::SeqCst);
    return true;
}

std::coroutine_handle<> Scheduler::PopInjection() {
    if (injection_hint_.Load(MemoryOrder::SeqCst) == 0) return {};
    LockGuard<Mutex> lock(injection_mutex_);
    if (injection_count_ == 0) return {};
    std::coroutine_handle<> h = injection_[injection_head_];
    injection_head_ = (injection_head_ + 1) % kInjectionCapacity;
    --injection_count_;
    injection_hint_.Store(injection_count_, MemoryOrder::SeqCst);
    return h;
}

usize Scheduler::InjectionQueueSizeForTesting() {
    LockGuard<Mutex> lock(injection_mutex_);
    return injection_count_;
}

std::coroutine_handle<> Scheduler::TrySteal(usize index) {
    if (count_ < 2) return {};
    Lane& me = lanes_[index];
    const usize attempts = 2 * count_;
    for (usize a = 0; a < attempts; ++a) {
        // Uniform over the other count_-1 participants.
        usize victim = NextRandom(me) % (count_ - 1);
        if (victim >= index) ++victim;
        std::coroutine_handle<> h;
        StealStatus st = lanes_[victim].deque.StealTop(h);
        if (st == StealStatus::Success) {
            me.steals.FetchAdd(1, MemoryOrder::Relaxed);
            return h;
        }
    }
    return {};
}

std::coroutine_handle<> Scheduler::FindWork(usize index) {
    std::coroutine_handle<> h = lanes_[index].deque.PopBottom();
    if (h) return h;
    h = PopInjection();
    if (h) return h;
    return TrySteal(index);
}

void Scheduler::Run(usize index, std::coroutine_handle<> h) {
    ++tls_job_depth;
    h.resume(); // may destroy the frame; do not touch `h` afterwards
    --tls_job_depth;
    lanes_[index].resumes.FetchAdd(1, MemoryOrder::Relaxed);
}

void Scheduler::NotifyWork() {
    submit_seq_.FetchAdd(1, MemoryOrder::SeqCst);
    if (parked_.Load(MemoryOrder::SeqCst) > 0) {
        // Taking the mutex orders this notify after a worker that has
        // decided to wait is really waiting (see the protocol above).
        { LockGuard<Mutex> lock(park_mutex_); }
        park_cv_.NotifyOne();
    }
}

void Scheduler::Submit(Job&& job) { Schedule(job.Release()); }

std::coroutine_handle<> Scheduler::SweepAll(usize index) {
    for (usize v = 0; v < count_; ++v) {
        if (v == index) continue;
        for (;;) {
            std::coroutine_handle<> h;
            StealStatus st = lanes_[v].deque.StealTop(h);
            if (st == StealStatus::Success) {
                lanes_[index].steals.FetchAdd(1, MemoryOrder::Relaxed);
                return h;
            }
            if (st == StealStatus::Empty) break;
        }
    }
    return {};
}

// Main thread, outside any job, both queues full: run work until the
// injection queue has kBackpressureSlack free slots (preferring to drain
// the injection queue itself) so the jobs run here can fan out.
void Scheduler::MakeRoom() {
    for (;;) {
        {
            LockGuard<Mutex> lock(injection_mutex_);
            if (kInjectionCapacity - injection_count_ >= kBackpressureSlack) return;
        }
        std::coroutine_handle<> w = PopInjection();
        if (!w) w = FindWork(0);
        if (w) {
            Run(0, w);
        } else {
            ThisThread::Yield();
        }
    }
}

void Scheduler::Schedule(std::coroutine_handle<> h) {
    ENGINE_ASSERT(h.address() != nullptr, "Scheduler::Schedule: null handle");
    ENGINE_ASSERT(!drained_.Load(MemoryOrder::Acquire), "Scheduler::Schedule after Shutdown completed");
    const int own = OwnIndex();
    if (own >= 0 && lanes_[own].deque.PushBottom(h)) {
        NotifyWork();
        return;
    }
    if (!PushInjection(h)) {
        // Both full. Main outside a job makes room by running work;
        // anyone else has hit a capacity bug.
        ENGINE_ASSERT(own == 0 && tls_job_depth == 0,
                      "job queues full: deque and injection queue are at capacity");
        MakeRoom();
        // Prefer our own deque again (running jobs may have freed it),
        // else the injection queue, which now has slack.
        if (!lanes_[0].deque.PushBottom(h)) {
            bool ok = PushInjection(h);
            ENGINE_ASSERT(ok, "injection queue refilled while making room");
            (void)ok;
        }
    }
    NotifyWork();
}

void Scheduler::HelpUntil(bool (*done)(void* user), void* user) {
    ENGINE_ASSERT(IsMainThread(), "Scheduler::HelpUntil is only for participant 0 (the main thread)");
    ENGINE_ASSERT(tls_job_depth == 0, "Scheduler::HelpUntil called from inside a job (jobs wait with co_await)");
    while (!done(user)) {
        std::coroutine_handle<> h = FindWork(0);
        if (h) {
            Run(0, h);
        } else {
            ThisThread::Yield();
        }
    }
}

void Scheduler::HelpUntil(Counter& counter) {
    HelpUntil([](void* c) { return static_cast<Counter*>(c)->IsZero(); }, &counter);
}

void Scheduler::HelpUntil(const Atomic<int>& value, int target) {
    struct Ctx {
        const Atomic<int>* v;
        int target;
    } ctx{&value, target};
    HelpUntil([](void* u) { auto* c = static_cast<Ctx*>(u); return c->v->Load(MemoryOrder::Acquire) >= c->target; },
              &ctx);
}

void Scheduler::WorkerMain(void* user, Participant& self) { static_cast<Scheduler*>(user)->WorkerLoop(self); }

void Scheduler::WorkerLoop(Participant& self) {
    const usize index = self.index;
    while (!shutdown_.Load(MemoryOrder::Acquire)) {
        const u64 seq = submit_seq_.Load(MemoryOrder::SeqCst);
        std::coroutine_handle<> h = FindWork(index);
        if (h) {
            Run(index, h);
            continue;
        }
        // Bounded yield backoff before parking.
        bool found = false;
        for (int r = 0; r < kYieldRounds && !shutdown_.Load(MemoryOrder::Acquire); ++r) {
            ThisThread::Yield();
            h = FindWork(index);
            if (h) {
                found = true;
                break;
            }
        }
        if (found) {
            Run(index, h);
            continue;
        }
        h = SweepAll(index);
        if (h) {
            Run(index, h);
            continue;
        }
        // Park (protocol in scheduler.h). `seq` was read before the
        // rounds above, so any submission after it changes submit_seq_.
        UniqueLock<Mutex> lock(park_mutex_);
        parked_.FetchAdd(1, MemoryOrder::SeqCst);
        park_cv_.Wait(lock, [&] {
            return submit_seq_.Load(MemoryOrder::SeqCst) != seq || shutdown_.Load(MemoryOrder::Acquire);
        });
        parked_.FetchSub(1, MemoryOrder::SeqCst);
    }
}

usize Scheduler::Shutdown() {
    if (shut_down_) return 0;
    shut_down_ = true;
    // Drain the I/O thread first: in-flight reads finish and their
    // continuations are scheduled while the workers still run.
    io_->Shutdown();
    {
        LockGuard<Mutex> lock(park_mutex_);
        shutdown_.Store(true, MemoryOrder::SeqCst);
    }
    park_cv_.NotifyAll();
    pool_->Join();

    // Workers are gone: destroy whatever never ran.
    usize dropped = 0;
    for (;;) {
        std::coroutine_handle<> h = PopInjection();
        if (!h) break;
        h.destroy();
        ++dropped;
    }
    for (usize i = 0; i < count_; ++i) {
        for (;;) {
            std::coroutine_handle<> h; // reset every pass: a Lost steal leaves it null
            if (i == 0 && IsMainThread()) {
                h = lanes_[0].deque.PopBottom();
                if (!h) break;
            } else {
                if (lanes_[i].deque.StealTop(h) == StealStatus::Empty) break;
                if (!h) continue;
            }
            h.destroy();
            ++dropped;
        }
    }
    drained_.Store(true, MemoryOrder::Release);
    if (dropped != 0) {
        JobsLog(LogVerbosity::Warn, "scheduler shutdown destroyed %zu queued job(s) that never ran",
                static_cast<size_t>(dropped));
    }
    return dropped;
}

u64 Scheduler::ResumesBy(usize p) const { return lanes_[p].resumes.Load(MemoryOrder::Relaxed); }
u64 Scheduler::StealsBy(usize p) const { return lanes_[p].steals.Load(MemoryOrder::Relaxed); }
u64 Scheduler::TotalResumes() const {
    u64 t = 0;
    for (usize i = 0; i < count_; ++i) t += ResumesBy(i);
    return t;
}

} // namespace engine_placeholder::jobs
