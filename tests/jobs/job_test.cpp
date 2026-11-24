#include "sync/atomic.h"
#include "sync/thread.h"
#include "lang/utility.h"
#include "containers/vector.h"

#include <catch2/catch_test_macros.hpp>

#include "jobs/frame_pool.h"
#include "debug/compiler.h"
#include "jobs/job.h"

using namespace vse;
using vse::jobs::FramePool;
using vse::jobs::Job;

namespace
{

// Job bodies are free functions taking state by value / pointer — the
// parameter-lifetime rule from job.h.

// Not inlined: a Job made and destroyed in one scope would otherwise have its frame allocation elided by the
// optimizer (Release), and these tests count the pool's frames.
ENGINE_NOINLINE Job Example()
{
    co_return;
}

// Not inlined: a Job made and destroyed in one scope would otherwise have its frame allocation elided by the
// optimizer (Release), and these tests count the pool's frames.
ENGINE_NOINLINE Job SetFlag(bool* flag)
{
    *flag = true;
    co_return;
}

// Not inlined: a Job made and destroyed in one scope would otherwise have its frame allocation elided by the
// optimizer (Release), and these tests count the pool's frames.
ENGINE_NOINLINE Job CountResumes(int* counter)
{
    ++*counter;
    co_await SuspendAlways{};
    ++*counter;
    co_await SuspendAlways{};
    ++*counter;
}

// A frame much larger than the biggest pool class: the buffer is live
// across a suspension point, so it must live in the frame.
Job BigFrame(int* checksum)
{
    volatile char buf[4096];
    for (int i = 0; i < 4096; ++i)
    {
        buf[i] = static_cast<char>(i);
    }
    co_await SuspendAlways{};
    int sum = 0;
    for (int i = 0; i < 4096; ++i)
    {
        sum += static_cast<vse::byte>(buf[i]);
    }
    *checksum = sum;
}

struct HookRecord
{
    int calls = 0;
    u64 live_when_called = ~u64{0};
};

void RecordingHook(void* user)
{
    auto* rec = static_cast<HookRecord*>(user);
    ++rec->calls;
    rec->live_when_called = FramePool::Global().GetStats().Live();
}

FramePool::Stats Snap()
{
    return FramePool::Global().GetStats();
}

// Live frames from other, concurrently-finishing tests must not exist in
// this process while these run (tests execute sequentially), so Live()
// deltas are exact.

}  // namespace

TEST_CASE("Job Example() { co_return; } compiles, runs, and its frame came from the FramePool", "[jobs][job]")
{
    const FramePool::Stats before = Snap();

    Job job = Example();
    REQUIRE(job.Valid());
    FramePool::Stats created = Snap();
    // Exactly one pooled allocation (not the global default), no fallback.
    REQUIRE(created.TotalPooledAllocs() == before.TotalPooledAllocs() + 1);
    REQUIRE(created.fallback_allocs == before.fallback_allocs);
    REQUIRE(created.Live() == before.Live() + 1);

    CoroutineHandle<> h = job.Release();
    REQUIRE_FALSE(job.Valid());  // ownership moved out
    REQUIRE_FALSE(h.done());
    h.resume();  // runs to completion; the frame frees itself
    FramePool::Stats finished = Snap();
    REQUIRE(finished.TotalPooledFrees() == before.TotalPooledFrees() + 1);
    REQUIRE(finished.Live() == before.Live());
}

TEST_CASE("A Job does not run until it is resumed", "[jobs][job]")
{
    bool flag = false;
    Job job = SetFlag(&flag);
    REQUIRE_FALSE(flag);  // initial_suspend = suspend_always
    CoroutineHandle<> h = job.Release();
    REQUIRE_FALSE(flag);
    h.resume();
    REQUIRE(flag);
}

TEST_CASE("A Job destroyed without being released frees its never-started frame", "[jobs][job]")
{
    const FramePool::Stats before = Snap();
    bool flag = false;
    {
        Job job = SetFlag(&flag);
        REQUIRE(Snap().Live() == before.Live() + 1);
    }
    REQUIRE_FALSE(flag);  // the body never ran
    REQUIRE(Snap().Live() == before.Live());
    REQUIRE(Snap().TotalPooledFrees() == before.TotalPooledFrees() + 1);
}

TEST_CASE("Job is move-only and moving transfers or destroys frames correctly", "[jobs][job]")
{
    const FramePool::Stats before = Snap();
    bool a_flag = false, b_flag = false;

    Job a = SetFlag(&a_flag);
    Job b = Move(a);
    REQUIRE_FALSE(a.Valid());
    REQUIRE(b.Valid());
    REQUIRE(Snap().Live() == before.Live() + 1);

    // Move-assigning over a valid, never-started Job destroys its frame.
    Job c = SetFlag(&b_flag);
    REQUIRE(Snap().Live() == before.Live() + 2);
    b = Move(c);
    REQUIRE(Snap().Live() == before.Live() + 1);
    REQUIRE_FALSE(c.Valid());

    b.Release().resume();
    REQUIRE(b_flag);
    REQUIRE_FALSE(a_flag);
    REQUIRE(Snap().Live() == before.Live());

    // Self-move-assign leaves a valid Job alone.
    Job d = SetFlag(&a_flag);
    Job& d_ref = d;
    d = Move(d_ref);
    REQUIRE(d.Valid());
}

TEST_CASE("A multi-suspend job completes and frees its frame only on the final resume", "[jobs][job]")
{
    const FramePool::Stats before = Snap();
    int counter = 0;
    Job job = CountResumes(&counter);
    CoroutineHandle<> h = job.Release();

    h.resume();
    REQUIRE(counter == 1);
    REQUIRE(Snap().Live() == before.Live() + 1);  // suspended, frame alive
    h.resume();
    REQUIRE(counter == 2);
    REQUIRE(Snap().Live() == before.Live() + 1);
    h.resume();
    REQUIRE(counter == 3);
    REQUIRE(Snap().Live() == before.Live());  // finished, frame freed
}

TEST_CASE("The completion hook runs exactly once, after the frame is already freed", "[jobs][job]")
{
    const FramePool::Stats before = Snap();
    HookRecord rec;

    Job job = Example();
    job.SetCompletion(RecordingHook, &rec);
    CoroutineHandle<> h = job.Release();
    REQUIRE(rec.calls == 0);
    h.resume();

    REQUIRE(rec.calls == 1);
    // At hook time the job's own frame was already freed, so whatever the
    // hook wakes (a joining parent) sees a balanced pool.
    REQUIRE(rec.live_when_called == before.Live());
}

TEST_CASE("The completion hook does not run for a never-started Job", "[jobs][job]")
{
    HookRecord rec;
    {
        Job job = Example();
        job.SetCompletion(RecordingHook, &rec);
    }
    REQUIRE(rec.calls == 0);
}

TEST_CASE("A frame bigger than the top size class takes the counted fallback and still balances", "[jobs][job]")
{
    const FramePool::Stats before = Snap();
    int checksum = 0;
    Job job = BigFrame(&checksum);
    FramePool::Stats created = Snap();
    REQUIRE(created.fallback_allocs == before.fallback_allocs + 1);
    REQUIRE(created.TotalPooledAllocs() == before.TotalPooledAllocs());

    CoroutineHandle<> h = job.Release();
    h.resume();
    REQUIRE(checksum == 0);  // not finished yet
    h.resume();
    int expected = 0;
    for (int i = 0; i < 4096; ++i)
    {
        expected += static_cast<vse::byte>(static_cast<char>(i));
    }
    REQUIRE(checksum == expected);
    FramePool::Stats finished = Snap();
    REQUIRE(finished.fallback_frees == before.fallback_frees + 1);
    REQUIRE(finished.Live() == before.Live());
}

TEST_CASE("Jobs created on one thread can finish on another with balanced frame accounting", "[jobs][job]")
{
    const FramePool::Stats before = Snap();
    constexpr int kJobs = 2000;
    Atomic<int> ran{0};

    Vector<CoroutineHandle<>> handles;
    static bool flags[kJobs];
    for (bool& f : flags)
    {
        f = false;
    }
    for (int i = 0; i < kJobs; ++i)
    {
        Job j = SetFlag(&flags[i]);
        handles.Push(j.Release());
    }
    REQUIRE(Snap().Live() == before.Live() + kJobs);

    Thread runner(
        [&]
        {
            for (usize i = 0; i < handles.Size(); ++i)
            {
                CoroutineHandle<> h = handles[i];
                h.resume();
                ran.FetchAdd(1);
            }
        });
    runner.Join();

    REQUIRE(ran.Load() == kJobs);
    for (int i = 0; i < kJobs; ++i)
    {
        REQUIRE(flags[i]);
    }
    REQUIRE(Snap().Live() == before.Live());
}

namespace
{
Job SuspendOnce(Atomic<int>* stage)
{
    stage->Store(1, MemoryOrder::Relaxed);
    co_await SuspendAlways{};
    stage->Store(2, MemoryOrder::Relaxed);
}
}  // namespace

TEST_CASE("A job started on one thread, resumed on another, fires its hook once", "[jobs][job]")
{
    const FramePool::Stats before = Snap();
    Atomic<int> stage{0};
    HookRecord rec;
    Job job = SuspendOnce(&stage);
    job.SetCompletion(RecordingHook, &rec);
    CoroutineHandle<> h = job.Release();

    Thread first([&] { h.resume(); });
    first.Join();
    REQUIRE(stage.Load() == 1);
    REQUIRE(rec.calls == 0);

    Thread second([&] { h.resume(); });
    second.Join();
    REQUIRE(stage.Load() == 2);
    REQUIRE(rec.calls == 1);
    REQUIRE(Snap().Live() == before.Live());
}

namespace
{

struct Probe
{
    struct promise_type
    {
        int steps = 0;
        Probe get_return_object() { return Probe{CoroutineHandle<promise_type>::from_promise(*this)}; }
        SuspendAlways initial_suspend() noexcept { return {}; }
        SuspendAlways final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
    CoroutineHandle<promise_type> handle;
};

Probe TwoSteps()
{
    co_await SuspendAlways{};
    co_return;
}

}  // namespace

TEST_CASE("CoroutineHandle resumes, reports done, round-trips its address and destroys", "[jobs][job]")
{
    Probe probe = TwoSteps();
    CHECK(static_cast<bool>(probe.handle));
    CHECK(!probe.handle.done());
    probe.handle.resume();
    CHECK(!probe.handle.done());
    CoroutineHandle<> erased = probe.handle;
    CHECK(CoroutineHandle<Probe::promise_type>::from_address(erased.address()).address() == erased.address());
    erased.resume();
    CHECK(probe.handle.done());
    probe.handle.destroy();
}
