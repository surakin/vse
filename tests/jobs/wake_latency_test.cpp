// Wake latency of an idle worker (Phase 13 Day 153): how long a job submitted while every worker sleeps waits before a worker starts it.
// A measurement, not a check: hidden ([.]), run by hand on each machine and recorded in the day file:
//   jobs_tests "[latency]"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"

using namespace engine_placeholder;
using namespace engine_placeholder::jobs;

namespace {

i64 NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Job Stamp(std::atomic<i64>* started) {
    started->store(NowNs(), std::memory_order_release);
    co_return;
}

} // namespace

TEST_CASE("Wake latency of an idle worker: median and 99th percentile of 10000 wake-ups", "[jobs][latency][.]") {
    Scheduler::Config cfg;
    cfg.participants = 2; // main and one worker; main never helps, so the worker must wake for every job
    Scheduler s(cfg);
    constexpr usize kSamples = 10000;
    Vector<i64> waits;
    waits.Reserve(kSamples);
    for (usize i = 0; i < kSamples; ++i) {
        std::this_thread::sleep_for(std::chrono::microseconds(500)); // long enough for the worker to go back to sleep
        std::atomic<i64> started{0};
        const i64 submitted = NowNs();
        s.Submit(Stamp(&started));
        while (started.load(std::memory_order_acquire) == 0) std::this_thread::yield();
        waits.Push(started.load() - submitted);
    }
    Sort(Span<i64>(waits.Data(), waits.Size()));
    const double median_us = static_cast<double>(waits[kSamples / 2]) / 1000.0;
    const double p99_us = static_cast<double>(waits[kSamples * 99 / 100]) / 1000.0;
    const double max_us = static_cast<double>(waits[kSamples - 1]) / 1000.0;
    std::printf("wake latency over %zu wake-ups: median %.1f us, p99 %.1f us, max %.1f us\n", static_cast<size_t>(kSamples), median_us,
                p99_us, max_us);
    CHECK(median_us > 0.0);
}
