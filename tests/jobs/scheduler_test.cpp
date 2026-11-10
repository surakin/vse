#include "sync/atomic.h"
#include "containers/unique_ptr.h"
#include "sync/thread.h"

#include <catch2/catch_test_macros.hpp>

#include "jobs/frame_pool.h"
#include "jobs/scheduler.h"
#include "jobs/topology.h"

using namespace engine_placeholder;
using namespace engine_placeholder::jobs;

namespace {

Job IncrementJob(Atomic<int>* counter) {
    counter->FetchAdd(1, MemoryOrder::Release);
    co_return;
}

// Parent spawns `fanout` children from whatever participant runs it.
Job SpawnChildren(Atomic<int>* counter, int fanout) {
    Scheduler* s = Scheduler::Current();
    for (int i = 0; i < fanout; ++i) s->Submit(IncrementJob(counter));
    counter->FetchAdd(1, MemoryOrder::Release);
    co_return;
}

u64 SchedulerTestLive() { return FramePool::Global().GetStats().Live(); }

UniquePtr<Scheduler> SchedulerTestMakeScheduler(usize n) {
    Scheduler::Config cfg;
    cfg.participants = n; // 0 = default
    return MakeUnique<Scheduler>(cfg);
}

void RunBurst(Scheduler& s, int jobs) {
    Atomic<int> counter{0};
    for (int i = 0; i < jobs; ++i) s.Submit(IncrementJob(&counter));
    s.HelpUntil(counter, jobs);
    REQUIRE(counter.Load() == jobs);
}

} // namespace

TEST_CASE("10,000 trivial jobs complete exactly once for N = 1, 2 and the default", "[jobs][scheduler]") {
    for (usize n : {usize{1}, usize{2}, usize{0}}) {
        const u64 live_before = SchedulerTestLive();
        auto s = SchedulerTestMakeScheduler(n);
        RunBurst(*s, 10'000);
        REQUIRE(s->Shutdown() == 0); // quiesce: stats are exact once workers are joined
        REQUIRE(s->TotalResumes() == 10'000);
        s.Reset();
        REQUIRE(SchedulerTestLive() == live_before);
    }
}

TEST_CASE("The 10,000-job burst is stable over 100 consecutive runs per configuration", "[jobs][scheduler][repeat]") {
    for (usize n : {usize{1}, usize{2}, usize{0}}) {
        auto s = SchedulerTestMakeScheduler(n);
        for (int run = 0; run < 100; ++run) RunBurst(*s, 10'000);
        REQUIRE(s->Shutdown() == 0);
        REQUIRE(s->TotalResumes() == 100u * 10'000u);
    }
}

TEST_CASE("Main with N=1 can submit more jobs than the queues hold before helping", "[jobs][scheduler]") {
    auto s = SchedulerTestMakeScheduler(1);
    constexpr int kJobs = static_cast<int>(WorkStealingDeque::Capacity() + Scheduler::InjectionCapacity()) + 1000;
    RunBurst(*s, kJobs);
}

TEST_CASE("A full deque spills to the injection queue", "[jobs][scheduler]") {
    auto s = SchedulerTestMakeScheduler(1);
    Atomic<int> counter{0};
    const int extra = 500;
    const int total = static_cast<int>(WorkStealingDeque::Capacity()) + extra;
    for (int i = 0; i < total; ++i) s->Submit(IncrementJob(&counter));
    REQUIRE(s->InjectionQueueSizeForTesting() == static_cast<usize>(extra));
    s->HelpUntil(counter, total);
    REQUIRE(counter.Load() == total);
    REQUIRE(s->InjectionQueueSizeForTesting() == 0);
}

TEST_CASE("A non-participant thread submits through the injection queue", "[jobs][scheduler]") {
    for (usize n : {usize{1}, usize{3}}) {
        auto s = SchedulerTestMakeScheduler(n);
        Atomic<int> counter{0};
        constexpr int kJobs = 3000; // < injection capacity: no consumer needed while submitting
        Thread t([&] {
            for (int i = 0; i < kJobs; ++i) s->Submit(IncrementJob(&counter));
        });
        t.Join();
        s->HelpUntil(counter, kJobs);
        REQUIRE(counter.Load() == kJobs);
    }
}

TEST_CASE("Jobs submitted from worker participants run exactly once", "[jobs][scheduler]") {
    auto s = SchedulerTestMakeScheduler(4);
    Atomic<int> counter{0};
    constexpr int kParents = 200, kFanout = 20;
    for (int i = 0; i < kParents; ++i) s->Submit(SpawnChildren(&counter, kFanout));
    s->HelpUntil(counter, kParents * (kFanout + 1));
    REQUIRE(counter.Load() == kParents * (kFanout + 1));
}

TEST_CASE("Shutdown while every worker is parked does not hang", "[jobs][scheduler]") {
    auto s = SchedulerTestMakeScheduler(4);
    RunBurst(*s, 100);
    ThisThread::SleepForMilliseconds(50); // let them park
    REQUIRE(s->Shutdown() == 0);
}

TEST_CASE("Shutdown right after a burst loses nothing: ran + destroyed == submitted", "[jobs][scheduler]") {
    for (int round = 0; round < 20; ++round) {
        const u64 live_before = SchedulerTestLive();
        auto s = SchedulerTestMakeScheduler(4);
        Atomic<int> counter{0};
        constexpr int kJobs = 5000;
        for (int i = 0; i < kJobs; ++i) s->Submit(IncrementJob(&counter));
        usize dropped = s->Shutdown();
        REQUIRE(static_cast<usize>(counter.Load()) + dropped == static_cast<usize>(kJobs));
        REQUIRE(SchedulerTestLive() == live_before); // every frame freed, run or not
    }
}

TEST_CASE("Scheduler::Current is set on participants only", "[jobs][scheduler]") {
    REQUIRE(Scheduler::Current() == nullptr);
    auto s = SchedulerTestMakeScheduler(2);
    REQUIRE(Scheduler::Current() == s.Get());
    Scheduler* seen = reinterpret_cast<Scheduler*>(1);
    Thread t([&] { seen = Scheduler::Current(); });
    t.Join();
    REQUIRE(seen == nullptr);
    REQUIRE_FALSE(Scheduler::InsideJob());
}

namespace {
bool WaitForParked(Scheduler& s, u32 want) {
    for (int i = 0; i < 4000; ++i) { // up to ~4 s
        if (s.ParkedWorkersForTesting() >= want) return true;
        ThisThread::SleepForMilliseconds(1);
    }
    return false;
}
} // namespace

TEST_CASE("Workers really park when idle, and a later submission wakes one", "[jobs][scheduler]") {
    auto s = SchedulerTestMakeScheduler(4);
    REQUIRE(WaitForParked(*s, 3)); // all 3 workers parked
    for (int round = 0; round < 200; ++round) {
        Atomic<int> counter{0};
        s->Submit(IncrementJob(&counter));
        s->HelpUntil(counter, 1);
        REQUIRE(WaitForParked(*s, 3));
    }
    // Shutdown while parked.
    REQUIRE(s->Shutdown() == 0);
}

TEST_CASE("A woken-from-park worker runs work submitted from a non-participant thread", "[jobs][scheduler]") {
    auto s = SchedulerTestMakeScheduler(2); // one worker, main does not help: the worker must run it
    REQUIRE(WaitForParked(*s, 1));
    Atomic<int> counter{0};
    Thread t([&] { s->Submit(IncrementJob(&counter)); });
    t.Join();
    for (int i = 0; i < 4000 && counter.Load() == 0; ++i) ThisThread::SleepForMilliseconds(1);
    REQUIRE(counter.Load() == 1);
}

TEST_CASE("Scheduler::Current is null on threads of a plain WorkerPool", "[jobs][scheduler]") {
    WorkerPool plain(WorkerPool::Config{1});
    REQUIRE(WorkerPool::IsParticipantThread());
    REQUIRE(Scheduler::Current() == nullptr);
}
