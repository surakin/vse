#include <atomic>
#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "jobs/frame_pool.h"
#include "jobs/scheduler.h"
#include "jobs/topology.h"

using namespace engine_placeholder;
using namespace engine_placeholder::jobs;

namespace {

Job IncrementJob(std::atomic<int>* counter) {
    counter->fetch_add(1, std::memory_order_release);
    co_return;
}

// Parent spawns `fanout` children from whatever participant runs it.
Job SpawnChildren(std::atomic<int>* counter, int fanout) {
    Scheduler* s = Scheduler::Current();
    for (int i = 0; i < fanout; ++i) s->Submit(IncrementJob(counter));
    counter->fetch_add(1, std::memory_order_release);
    co_return;
}

u64 SchedulerTestLive() { return FramePool::Global().GetStats().Live(); }

std::unique_ptr<Scheduler> SchedulerTestMakeScheduler(usize n) {
    Scheduler::Config cfg;
    cfg.participants = n; // 0 = default
    return std::make_unique<Scheduler>(cfg);
}

void RunBurst(Scheduler& s, int jobs) {
    std::atomic<int> counter{0};
    for (int i = 0; i < jobs; ++i) s.Submit(IncrementJob(&counter));
    s.HelpUntil(counter, jobs);
    REQUIRE(counter.load() == jobs);
}

} // namespace

TEST_CASE("10,000 trivial jobs complete exactly once for N = 1, 2 and the default", "[jobs][scheduler]") {
    for (usize n : {usize{1}, usize{2}, usize{0}}) {
        const u64 live_before = SchedulerTestLive();
        auto s = SchedulerTestMakeScheduler(n);
        RunBurst(*s, 10'000);
        REQUIRE(s->Shutdown() == 0); // quiesce: stats are exact once workers are joined
        REQUIRE(s->TotalResumes() == 10'000);
        s.reset();
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
    std::atomic<int> counter{0};
    const int extra = 500;
    const int total = static_cast<int>(WorkStealingDeque::Capacity()) + extra;
    for (int i = 0; i < total; ++i) s->Submit(IncrementJob(&counter));
    REQUIRE(s->InjectionQueueSizeForTesting() == static_cast<usize>(extra));
    s->HelpUntil(counter, total);
    REQUIRE(counter.load() == total);
    REQUIRE(s->InjectionQueueSizeForTesting() == 0);
}

TEST_CASE("A non-participant thread submits through the injection queue", "[jobs][scheduler]") {
    for (usize n : {usize{1}, usize{3}}) {
        auto s = SchedulerTestMakeScheduler(n);
        std::atomic<int> counter{0};
        constexpr int kJobs = 3000; // < injection capacity: no consumer needed while submitting
        std::thread t([&] {
            for (int i = 0; i < kJobs; ++i) s->Submit(IncrementJob(&counter));
        });
        t.join();
        s->HelpUntil(counter, kJobs);
        REQUIRE(counter.load() == kJobs);
    }
}

TEST_CASE("Jobs submitted from worker participants run exactly once", "[jobs][scheduler]") {
    auto s = SchedulerTestMakeScheduler(4);
    std::atomic<int> counter{0};
    constexpr int kParents = 200, kFanout = 20;
    for (int i = 0; i < kParents; ++i) s->Submit(SpawnChildren(&counter, kFanout));
    s->HelpUntil(counter, kParents * (kFanout + 1));
    REQUIRE(counter.load() == kParents * (kFanout + 1));
}

TEST_CASE("Shutdown while every worker is parked does not hang", "[jobs][scheduler]") {
    auto s = SchedulerTestMakeScheduler(4);
    RunBurst(*s, 100);
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let them park
    REQUIRE(s->Shutdown() == 0);
}

TEST_CASE("Shutdown right after a burst loses nothing: ran + destroyed == submitted", "[jobs][scheduler]") {
    for (int round = 0; round < 20; ++round) {
        const u64 live_before = SchedulerTestLive();
        auto s = SchedulerTestMakeScheduler(4);
        std::atomic<int> counter{0};
        constexpr int kJobs = 5000;
        for (int i = 0; i < kJobs; ++i) s->Submit(IncrementJob(&counter));
        usize dropped = s->Shutdown();
        REQUIRE(static_cast<usize>(counter.load()) + dropped == static_cast<usize>(kJobs));
        REQUIRE(SchedulerTestLive() == live_before); // every frame freed, run or not
    }
}

TEST_CASE("Scheduler::Current is set on participants only", "[jobs][scheduler]") {
    REQUIRE(Scheduler::Current() == nullptr);
    auto s = SchedulerTestMakeScheduler(2);
    REQUIRE(Scheduler::Current() == s.get());
    Scheduler* seen = reinterpret_cast<Scheduler*>(1);
    std::thread t([&] { seen = Scheduler::Current(); });
    t.join();
    REQUIRE(seen == nullptr);
    REQUIRE_FALSE(Scheduler::InsideJob());
}

namespace {
bool WaitForParked(Scheduler& s, u32 want) {
    for (int i = 0; i < 4000; ++i) { // up to ~4 s
        if (s.ParkedWorkersForTesting() >= want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
} // namespace

TEST_CASE("Workers really park when idle, and a later submission wakes one", "[jobs][scheduler]") {
    auto s = SchedulerTestMakeScheduler(4);
    REQUIRE(WaitForParked(*s, 3)); // all 3 workers parked
    for (int round = 0; round < 200; ++round) {
        std::atomic<int> counter{0};
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
    std::atomic<int> counter{0};
    std::thread t([&] { s->Submit(IncrementJob(&counter)); });
    t.join();
    for (int i = 0; i < 4000 && counter.load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(counter.load() == 1);
}

TEST_CASE("Scheduler::Current is null on threads of a plain WorkerPool", "[jobs][scheduler]") {
    WorkerPool plain(WorkerPool::Config{1});
    REQUIRE(WorkerPool::IsParticipantThread());
    REQUIRE(Scheduler::Current() == nullptr);
}
