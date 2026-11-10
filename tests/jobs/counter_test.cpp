#include "sync/atomic.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "debug/compiler.h"
#include "build_defs.h"
#if ENGINE_COMPILER_MSVC
#include <intrin.h>
#endif

#include "containers/unique_ptr.h"
#include "jobs/counter.h"
#include "jobs/frame_pool.h"
#include "jobs/scheduler.h"
#include "lang/utility.h"

using namespace engine_placeholder;
using namespace engine_placeholder::jobs;

namespace {

UniquePtr<Scheduler> CounterTestMakeScheduler(usize n) {
    Scheduler::Config cfg;
    cfg.participants = n;
    return MakeUnique<Scheduler>(cfg);
}
u64 Live() { return FramePool::Global().GetStats().Live(); }

void SpinFor(u32 n) {
    for (volatile u32 i = 0; i < n; i = i + 1) {
    }
}

// ---- the await/decrement race ------------------------------------------

struct RaceCell {
    Counter counter;
    explicit RaceCell(Scheduler& s) : counter(s) {}
};

Atomic<int> g_race_resumes{0};

// The awaiter FREES the cell (and its Counter) right after being
// resumed, while the decrementer may still be inside Decrement(): any
// touch of the Counter after the wake-up is a use-after-free that ASan
// reports.
Job AwaiterJob(RaceCell* cell, u32 spin, Atomic<int>* done) {
    SpinFor(spin);
    co_await cell->counter;
    engine_placeholder::DefaultDelete<RaceCell>()(cell); // frees the cell made by MakeUnique(...).Release()
    g_race_resumes.FetchAdd(1, MemoryOrder::AcqRel);
    done->FetchAdd(1, MemoryOrder::AcqRel);
}

Job DecrementerJob(RaceCell* cell, u32 spin, Atomic<int>* done) {
    SpinFor(spin);
    cell->counter.Decrement();
    done->FetchAdd(1, MemoryOrder::AcqRel);
    co_return;
}

u32 CounterTestNextRand(u64& s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return static_cast<u32>(s >> 11);
}

// ---- ordering ------------------------------------------------------------

Job SetFlagJob(bool* flag, u32 spin) {
    SpinFor(spin);
    *flag = true;
    co_return;
}

Job ParentJob(Atomic<int>* ok, Atomic<int>* finished) {
    Counter c; // bound to this participant's scheduler; lives in this frame
    bool flags[8] = {};
    for (int i = 0; i < 8; ++i) Spawn(c, SetFlagJob(&flags[i], static_cast<u32>(i * 37)));
    co_await c;
    bool all = true;
    for (bool f : flags) all = all && f;
    if (all) ok->FetchAdd(1, MemoryOrder::AcqRel);
    finished->FetchAdd(1, MemoryOrder::AcqRel);
}

// ---- deep chain ----------------------------------------------------------

// Where the stack is now: the frame of a function of its own (MSVC has no __builtin_frame_address; Phase 13 Day 153).
ENGINE_NOINLINE usize StackMark() {
#if ENGINE_COMPILER_MSVC
    return reinterpret_cast<usize>(_AddressOfReturnAddress());
#else
    return reinterpret_cast<usize>(__builtin_frame_address(0));
#endif
}

struct ChainStats {
    usize lowest = ~usize{0};
    usize highest = 0;
    Atomic<int> finished{0};
};

Job ChainJob(int depth, ChainStats* stats) {
    if (depth > 0) {
        Counter c;
        Spawn(c, ChainJob(depth - 1, stats));
        co_await c;
    }
    // Stats are written only by the single thread running everything (N=1).
    usize m = StackMark();
    if (m < stats->lowest) stats->lowest = m;
    if (m > stats->highest) stats->highest = m;
    stats->finished.FetchAdd(1, MemoryOrder::AcqRel);
}

} // namespace

TEST_CASE("Awaiting an already-zero counter does not suspend", "[jobs][counter]") {
    auto s = CounterTestMakeScheduler(1);
    static Atomic<int> reached;
    reached = 0;
    struct Local {
        static Job Run(Atomic<int>* r) {
            Counter c;
            co_await c; // count is zero
            r->FetchAdd(1);
        }
    };
    s->Submit(Local::Run(&reached));
    s->HelpUntil(reached, 1);
    REQUIRE(reached.Load() == 1);
}

TEST_CASE("Counter counts back to zero and is reusable for a second round", "[jobs][counter]") {
    auto s = CounterTestMakeScheduler(2);
    static Atomic<int> rounds;
    rounds = 0;
    struct Local {
        static Job Child(bool* f) { *f = true; co_return; }
        static Job Run(Atomic<int>* r) {
            Counter c;
            for (int round = 0; round < 3; ++round) {
                bool f1 = false, f2 = false;
                Spawn(c, Child(&f1));
                Spawn(c, Child(&f2));
                co_await c;
                if (f1 && f2 && c.IsZero()) r->FetchAdd(1);
            }
        }
    };
    s->Submit(Local::Run(&rounds));
    s->HelpUntil(rounds, 3);
    REQUIRE(rounds.Load() == 3);
}

TEST_CASE("Job::SetCompletion(Counter&) decrements after the frame is freed", "[jobs][counter]") {
    auto s = CounterTestMakeScheduler(1);
    Counter c(*s);
    const u64 live_before = Live();
    c.Increment();
    bool flag = false;
    Job j = SetFlagJob(&flag, 0);
    j.SetCompletion(c);
    s->Submit(Move(j));
    s->HelpUntil(c);
    REQUIRE(c.IsZero());
    REQUIRE(flag);
    REQUIRE(Live() == live_before);
}

TEST_CASE("Await/decrement race: exactly-once resumption over 100,000 iterations", "[jobs][counter][race]") {
    for (usize n : {usize{4}, usize{2}}) {
        auto s = CounterTestMakeScheduler(n);
        constexpr int kBatches = 100, kPerBatch = 1000;
        u64 rng = 0x1234567ull + n;
        int total_iterations = 0;
        const u64 inline_before = Counter::PathStats().inline_continues.Load();
        const u64 parked_before = Counter::PathStats().parked.Load();
        g_race_resumes = 0;
        for (int b = 0; b < kBatches; ++b) {
            Atomic<int> done{0};
            for (int i = 0; i < kPerBatch; ++i) {
                auto* cell = engine_placeholder::MakeUnique<RaceCell>(*s).Release();
                cell->counter.Increment();
                u32 r = CounterTestNextRand(rng);
                Job a = AwaiterJob(cell, r % 400, &done);
                Job d = DecrementerJob(cell, (r >> 9) % 400, &done);
                if (r & 0x80000) {
                    s->Submit(Move(a));
                    s->Submit(Move(d));
                } else {
                    s->Submit(Move(d));
                    s->Submit(Move(a));
                }
            }
            s->HelpUntil(done, 2 * kPerBatch);
            total_iterations += kPerBatch;
        }
        // Each iteration resumed its coroutine exactly once. The inline
        // path's window (between await_ready and await_suspend) is a few
        // nanoseconds, so it is covered deterministically by the manual
        // protocol test below, not asserted here.
        REQUIRE(g_race_resumes.Load() == total_iterations);
        const u64 inl = Counter::PathStats().inline_continues.Load() - inline_before;
        const u64 prk = Counter::PathStats().parked.Load() - parked_before;
        REQUIRE(prk > 0);
        REQUIRE(inl + prk <= static_cast<u64>(total_iterations));
        REQUIRE(total_iterations == 100'000);
    }
}

TEST_CASE("A parent proceeds past co_await only after all 8 children finished", "[jobs][counter]") {
    for (usize n : {usize{1}, usize{2}, usize{0}}) {
        auto s = CounterTestMakeScheduler(n);
        const u64 live_before = Live();
        Atomic<int> ok{0}, finished{0};
        constexpr int kBatches = 100, kParents = 100; // 10,000 parents per configuration
        for (int b = 0; b < kBatches; ++b) {
            for (int p = 0; p < kParents; ++p) s->Submit(ParentJob(&ok, &finished));
            s->HelpUntil(finished, (b + 1) * kParents);
        }
        REQUIRE(finished.Load() == kBatches * kParents);
        REQUIRE(ok.Load() == kBatches * kParents);
        s->Shutdown();
        REQUIRE(Live() == live_before);
    }
}

TEST_CASE("A deep chain of dependent jobs does not grow the native stack", "[jobs][counter][stack]") {
    auto s = CounterTestMakeScheduler(1); // one thread runs everything: stack marks are comparable
    constexpr int kDepth = 5000;
    ChainStats stats;
    s->Submit(ChainJob(kDepth, &stats));
    s->HelpUntil(stats.finished, kDepth + 1);
    REQUIRE(stats.finished.Load() == kDepth + 1);
    // Inline resumption of each parent from the child's completion would
    // nest ~kDepth levels (hundreds of KiB+); requeuing keeps it flat.
    REQUIRE(stats.highest - stats.lowest < 64 * 1024);
}

TEST_CASE("Awaitable protocol, both orderings driven by hand", "[jobs][counter]") {
    auto s = CounterTestMakeScheduler(1);

    // (1) The count reaches zero between await_ready and await_suspend:
    // await_suspend must report "don't suspend" and nobody else resumes.
    {
        Counter c(*s);
        c.Increment();
        auto aw = c.operator co_await();
        REQUIRE_FALSE(aw.await_ready());
        c.Decrement(); // no waiter yet: must not schedule anything
        bool dummy_done = false;
        Job j = SetFlagJob(&dummy_done, 0);
        CoroutineHandle<> h = j.Release();
        REQUIRE_FALSE(aw.await_suspend(h)); // finished already: continue inline
        aw.await_resume();
        REQUIRE(s->InjectionQueueSizeForTesting() == 0);
        h.destroy(); // never scheduled by the counter: we still own it
    }

    // (2) await_suspend first (parks), then the final Decrement reschedules
    // the handle exactly once.
    {
        Counter c(*s);
        c.Increment();
        bool ran = false;
        Job j = SetFlagJob(&ran, 0);
        CoroutineHandle<> h = j.Release();
        auto aw = c.operator co_await();
        REQUIRE_FALSE(aw.await_ready());
        REQUIRE(aw.await_suspend(h)); // parked
        REQUIRE_FALSE(ran);
        c.Decrement(); // owns the wake-up: schedules h
        Atomic<int> sentinel{0};
        struct Ctx { bool* ran; } ctx{&ran};
        s->HelpUntil([](void* u) { return *static_cast<Ctx*>(u)->ran; }, &ctx);
        REQUIRE(ran);
        aw.await_resume();
        REQUIRE(c.IsZero());
        (void)sentinel;
    }
}
