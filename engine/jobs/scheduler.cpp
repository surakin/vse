#include "jobs/scheduler.h"

#include <thread>

#include "jobs/async_io.h"
#include "jobs/counter.h"
#include "jobs/jobs_log.h"
#include "log/assert.h"

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
    pool_ = new WorkerPool(pc);
    io_ = new IoThread();
}

Scheduler::~Scheduler() {
    Shutdown();
    delete io_;
    delete pool_;
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
    if (p != nullptr && p->owner == pool_) return static_cast<int>(p->index);
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
    std::lock_guard<std::mutex> lock(injection_mutex_);
    if (injection_count_ == kInjectionCapacity) return false;
    injection_[(injection_head_ + injection_count_) % kInjectionCapacity] = h;
    ++injection_count_;
    injection_hint_.store(injection_count_, std::memory_order_seq_cst);
    return true;
}

std::coroutine_handle<> Scheduler::PopInjection() {
    if (injection_hint_.load(std::memory_order_seq_cst) == 0) return {};
    std::lock_guard<std::mutex> lock(injection_mutex_);
    if (injection_count_ == 0) return {};
    std::coroutine_handle<> h = injection_[injection_head_];
    injection_head_ = (injection_head_ + 1) % kInjectionCapacity;
    --injection_count_;
    injection_hint_.store(injection_count_, std::memory_order_seq_cst);
    return h;
}

usize Scheduler::InjectionQueueSizeForTesting() {
    std::lock_guard<std::mutex> lock(injection_mutex_);
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
            me.steals.fetch_add(1, std::memory_order_relaxed);
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
    lanes_[index].resumes.fetch_add(1, std::memory_order_relaxed);
}

void Scheduler::NotifyWork() {
    submit_seq_.fetch_add(1, std::memory_order_seq_cst);
    if (parked_.load(std::memory_order_seq_cst) > 0) {
        // Taking the mutex orders this notify after a worker that has
        // decided to wait is really waiting (see the protocol above).
        { std::lock_guard<std::mutex> lock(park_mutex_); }
        park_cv_.notify_one();
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
                lanes_[index].steals.fetch_add(1, std::memory_order_relaxed);
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
            std::lock_guard<std::mutex> lock(injection_mutex_);
            if (kInjectionCapacity - injection_count_ >= kBackpressureSlack) return;
        }
        std::coroutine_handle<> w = PopInjection();
        if (!w) w = FindWork(0);
        if (w) {
            Run(0, w);
        } else {
            std::this_thread::yield();
        }
    }
}

void Scheduler::Schedule(std::coroutine_handle<> h) {
    ENGINE_ASSERT(h.address() != nullptr, "Scheduler::Schedule: null handle");
    ENGINE_ASSERT(!drained_.load(std::memory_order_acquire), "Scheduler::Schedule after Shutdown completed");
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
            std::this_thread::yield();
        }
    }
}

void Scheduler::HelpUntil(Counter& counter) {
    HelpUntil([](void* c) { return static_cast<Counter*>(c)->IsZero(); }, &counter);
}

void Scheduler::HelpUntil(const std::atomic<int>& value, int target) {
    struct Ctx {
        const std::atomic<int>* v;
        int target;
    } ctx{&value, target};
    HelpUntil([](void* u) { auto* c = static_cast<Ctx*>(u); return c->v->load(std::memory_order_acquire) >= c->target; },
              &ctx);
}

void Scheduler::WorkerMain(void* user, Participant& self) { static_cast<Scheduler*>(user)->WorkerLoop(self); }

void Scheduler::WorkerLoop(Participant& self) {
    const usize index = self.index;
    while (!shutdown_.load(std::memory_order_acquire)) {
        const u64 seq = submit_seq_.load(std::memory_order_seq_cst);
        std::coroutine_handle<> h = FindWork(index);
        if (h) {
            Run(index, h);
            continue;
        }
        // Bounded yield backoff before parking.
        bool found = false;
        for (int r = 0; r < kYieldRounds && !shutdown_.load(std::memory_order_acquire); ++r) {
            std::this_thread::yield();
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
        std::unique_lock<std::mutex> lock(park_mutex_);
        parked_.fetch_add(1, std::memory_order_seq_cst);
        park_cv_.wait(lock, [&] {
            return submit_seq_.load(std::memory_order_seq_cst) != seq || shutdown_.load(std::memory_order_acquire);
        });
        parked_.fetch_sub(1, std::memory_order_seq_cst);
    }
}

usize Scheduler::Shutdown() {
    if (shut_down_) return 0;
    shut_down_ = true;
    // Drain the I/O thread first: in-flight reads finish and their
    // continuations are scheduled while the workers still run.
    io_->Shutdown();
    {
        std::lock_guard<std::mutex> lock(park_mutex_);
        shutdown_.store(true, std::memory_order_seq_cst);
    }
    park_cv_.notify_all();
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
    drained_.store(true, std::memory_order_release);
    if (dropped != 0) {
        JobsLog(LogVerbosity::Warn, "scheduler shutdown destroyed %zu queued job(s) that never ran",
                static_cast<size_t>(dropped));
    }
    return dropped;
}

u64 Scheduler::ResumesBy(usize p) const { return lanes_[p].resumes.load(std::memory_order_relaxed); }
u64 Scheduler::StealsBy(usize p) const { return lanes_[p].steals.load(std::memory_order_relaxed); }
u64 Scheduler::TotalResumes() const {
    u64 t = 0;
    for (usize i = 0; i < count_; ++i) t += ResumesBy(i);
    return t;
}

} // namespace engine_placeholder::jobs
