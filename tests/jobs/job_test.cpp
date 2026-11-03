#include <atomic>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "jobs/frame_pool.h"
#include "jobs/job.h"

using namespace engine_placeholder;
using engine_placeholder::jobs::FramePool;
using engine_placeholder::jobs::Job;

namespace {

// Job bodies are free functions taking state by value / pointer — the
// parameter-lifetime rule from job.h.

Job Example() { co_return; }

Job SetFlag(bool* flag) {
    *flag = true;
    co_return;
}

Job CountResumes(int* counter) {
    ++*counter;
    co_await std::suspend_always{};
    ++*counter;
    co_await std::suspend_always{};
    ++*counter;
}

// A frame much larger than the biggest pool class: the buffer is live
// across a suspension point, so it must live in the frame.
Job BigFrame(int* checksum) {
    volatile char buf[4096];
    for (int i = 0; i < 4096; ++i) buf[i] = static_cast<char>(i);
    co_await std::suspend_always{};
    int sum = 0;
    for (int i = 0; i < 4096; ++i) sum += static_cast<unsigned char>(buf[i]);
    *checksum = sum;
}

struct HookRecord {
    int calls = 0;
    u64 live_when_called = ~u64{0};
};

void RecordingHook(void* user) {
    auto* rec = static_cast<HookRecord*>(user);
    ++rec->calls;
    rec->live_when_called = FramePool::Global().GetStats().Live();
}

FramePool::Stats Snap() { return FramePool::Global().GetStats(); }

// Live frames from other, concurrently-finishing tests must not exist in
// this process while these run (tests execute sequentially), so Live()
// deltas are exact.

} // namespace

TEST_CASE("Job Example() { co_return; } compiles, runs, and its frame came from the FramePool",
          "[jobs][job]") {
    const FramePool::Stats before = Snap();

    Job job = Example();
    REQUIRE(job.Valid());
    FramePool::Stats created = Snap();
    // Exactly one pooled allocation (not the global default), no fallback.
    REQUIRE(created.TotalPooledAllocs() == before.TotalPooledAllocs() + 1);
    REQUIRE(created.fallback_allocs == before.fallback_allocs);
    REQUIRE(created.Live() == before.Live() + 1);

    std::coroutine_handle<> h = job.Release();
    REQUIRE_FALSE(job.Valid()); // ownership moved out
    REQUIRE_FALSE(h.done());
    h.resume(); // runs to completion; the frame frees itself
    FramePool::Stats finished = Snap();
    REQUIRE(finished.TotalPooledFrees() == before.TotalPooledFrees() + 1);
    REQUIRE(finished.Live() == before.Live());
}

TEST_CASE("A Job does not run until it is resumed", "[jobs][job]") {
    bool flag = false;
    Job job = SetFlag(&flag);
    REQUIRE_FALSE(flag); // initial_suspend = suspend_always
    std::coroutine_handle<> h = job.Release();
    REQUIRE_FALSE(flag);
    h.resume();
    REQUIRE(flag);
}

TEST_CASE("A Job destroyed without being released frees its never-started frame", "[jobs][job]") {
    const FramePool::Stats before = Snap();
    bool flag = false;
    {
        Job job = SetFlag(&flag);
        REQUIRE(Snap().Live() == before.Live() + 1);
    }
    REQUIRE_FALSE(flag); // the body never ran
    REQUIRE(Snap().Live() == before.Live());
    REQUIRE(Snap().TotalPooledFrees() == before.TotalPooledFrees() + 1);
}

TEST_CASE("Job is move-only and moving transfers or destroys frames correctly", "[jobs][job]") {
    const FramePool::Stats before = Snap();
    bool a_flag = false, b_flag = false;

    Job a = SetFlag(&a_flag);
    Job b = std::move(a);
    REQUIRE_FALSE(a.Valid());
    REQUIRE(b.Valid());
    REQUIRE(Snap().Live() == before.Live() + 1);

    // Move-assigning over a valid, never-started Job destroys its frame.
    Job c = SetFlag(&b_flag);
    REQUIRE(Snap().Live() == before.Live() + 2);
    b = std::move(c);
    REQUIRE(Snap().Live() == before.Live() + 1);
    REQUIRE_FALSE(c.Valid());

    b.Release().resume();
    REQUIRE(b_flag);
    REQUIRE_FALSE(a_flag);
    REQUIRE(Snap().Live() == before.Live());

    // Self-move-assign leaves a valid Job alone.
    Job d = SetFlag(&a_flag);
    Job& d_ref = d;
    d = std::move(d_ref);
    REQUIRE(d.Valid());
}

TEST_CASE("A multi-suspend job completes and frees its frame only on the final resume", "[jobs][job]") {
    const FramePool::Stats before = Snap();
    int counter = 0;
    Job job = CountResumes(&counter);
    std::coroutine_handle<> h = job.Release();

    h.resume();
    REQUIRE(counter == 1);
    REQUIRE(Snap().Live() == before.Live() + 1); // suspended, frame alive
    h.resume();
    REQUIRE(counter == 2);
    REQUIRE(Snap().Live() == before.Live() + 1);
    h.resume();
    REQUIRE(counter == 3);
    REQUIRE(Snap().Live() == before.Live()); // finished, frame freed
}

TEST_CASE("The completion hook runs exactly once, after the frame is already freed", "[jobs][job]") {
    const FramePool::Stats before = Snap();
    HookRecord rec;

    Job job = Example();
    job.SetCompletion(RecordingHook, &rec);
    std::coroutine_handle<> h = job.Release();
    REQUIRE(rec.calls == 0);
    h.resume();

    REQUIRE(rec.calls == 1);
    // At hook time the job's own frame was already freed, so whatever the
    // hook wakes (a joining parent) sees a balanced pool.
    REQUIRE(rec.live_when_called == before.Live());
}

TEST_CASE("The completion hook does not run for a never-started Job", "[jobs][job]") {
    HookRecord rec;
    {
        Job job = Example();
        job.SetCompletion(RecordingHook, &rec);
    }
    REQUIRE(rec.calls == 0);
}

TEST_CASE("A frame bigger than the top size class takes the counted fallback and still balances",
          "[jobs][job]") {
    const FramePool::Stats before = Snap();
    int checksum = 0;
    Job job = BigFrame(&checksum);
    FramePool::Stats created = Snap();
    REQUIRE(created.fallback_allocs == before.fallback_allocs + 1);
    REQUIRE(created.TotalPooledAllocs() == before.TotalPooledAllocs());

    std::coroutine_handle<> h = job.Release();
    h.resume();
    REQUIRE(checksum == 0); // not finished yet
    h.resume();
    int expected = 0;
    for (int i = 0; i < 4096; ++i) expected += static_cast<unsigned char>(static_cast<char>(i));
    REQUIRE(checksum == expected);
    FramePool::Stats finished = Snap();
    REQUIRE(finished.fallback_frees == before.fallback_frees + 1);
    REQUIRE(finished.Live() == before.Live());
}

TEST_CASE("Jobs created on one thread can finish on another with balanced frame accounting",
          "[jobs][job]") {
    const FramePool::Stats before = Snap();
    constexpr int kJobs = 2000;
    std::atomic<int> ran{0};

    std::vector<std::coroutine_handle<>> handles;
    static bool flags[kJobs];
    for (bool& f : flags) f = false;
    for (int i = 0; i < kJobs; ++i) {
        Job j = SetFlag(&flags[i]);
        handles.push_back(j.Release());
    }
    REQUIRE(Snap().Live() == before.Live() + kJobs);

    std::thread runner([&] {
        for (auto h : handles) {
            h.resume();
            ran.fetch_add(1);
        }
    });
    runner.join();

    REQUIRE(ran.load() == kJobs);
    for (int i = 0; i < kJobs; ++i) {
        REQUIRE(flags[i]);
    }
    REQUIRE(Snap().Live() == before.Live());
}

namespace {
Job SuspendOnce(std::atomic<int>* stage) {
    stage->store(1, std::memory_order_relaxed);
    co_await std::suspend_always{};
    stage->store(2, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("A job started on one thread, resumed on another, fires its hook once", "[jobs][job]") {
    const FramePool::Stats before = Snap();
    std::atomic<int> stage{0};
    HookRecord rec;
    Job job = SuspendOnce(&stage);
    job.SetCompletion(RecordingHook, &rec);
    std::coroutine_handle<> h = job.Release();

    std::thread first([&] { h.resume(); });
    first.join();
    REQUIRE(stage.load() == 1);
    REQUIRE(rec.calls == 0);

    std::thread second([&] { h.resume(); });
    second.join();
    REQUIRE(stage.load() == 2);
    REQUIRE(rec.calls == 1);
    REQUIRE(Snap().Live() == before.Live());
}
