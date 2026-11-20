#include "sync/atomic.h"
#include "os/console.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "test_env.h"
#include "test_text.h"

#include "jobs/frame_pool.h"
#include "jobs/parallel_for.h"

using namespace vse;
using namespace vse::jobs;

namespace
{

UniquePtr<Scheduler> ParallelForTestMakeScheduler(usize n)
{
    Scheduler::Config cfg;
    cfg.participants = n;
    return MakeUnique<Scheduler>(cfg);
}

// `n` zeros.
Vector<u64> Zeros(usize n)
{
    Vector<u64> v;
    v.Resize(n);
    for (usize i = 0; i < n; ++i)
    {
        v[i] = 0;
    }
    return v;
}

struct SumCtx
{
    const u64* data;
    u64* partials;  // one slot per chunk
    usize chunk;
    Atomic<u64> invocations{0};
};

void SumChunk(void* user, usize begin, usize end)
{
    auto* c = static_cast<SumCtx*>(user);
    u64 sum = 0;
    for (usize i = begin; i < end; ++i)
    {
        sum += c->data[i];
    }
    c->partials[begin / c->chunk] = sum;  // each chunk owns its own slot
    c->invocations.FetchAdd(1, MemoryOrder::Relaxed);
}

// Runs the sum with the given scheduler/chunk; returns the combined sum
// and the number of chunk invocations.
u64 ParallelSum(Scheduler& s, const Vector<u64>& data, usize chunk, u64* invocations = nullptr)
{
    if (chunk == 0)
    {
        chunk = kDefaultChunkSize;
    }
    usize chunks = data.Empty() ? 0 : (data.Size() + chunk - 1) / chunk;
    Vector<u64> partials = Zeros(chunks ? chunks : 1);
    SumCtx ctx{data.Data(), partials.Data(), chunk, {0}};
    ParallelForBlocking(s, data.Size(), chunk, SumChunk, &ctx);
    u64 total = 0;
    for (usize i = 0; i < chunks; ++i)
    {
        total += partials[i];
    }
    if (invocations)
    {
        *invocations = ctx.invocations.Load();
    }
    return total;
}

Vector<u64> MakeData(usize n)
{
    Vector<u64> v;
    v.Resize(n);
    for (usize i = 0; i < n; ++i)
    {
        v[i] = i;  // closed form: n(n-1)/2
    }
    return v;
}

}  // namespace

TEST_CASE("parallel_for sums 100,000 elements exactly, for N = 1, 2, default", "[jobs][parallel_for]")
{
    const usize n = 100'000;
    auto data = MakeData(n);
    const u64 expected = static_cast<u64>(n) * (n - 1) / 2;
    for (usize workers : {usize{1}, usize{2}, usize{0}})
    {
        auto s = ParallelForTestMakeScheduler(workers);
        for (usize chunk : {usize{0}, usize{1000}, usize{4096}, usize{99'999}, usize{100'000}, usize{1'000'000}})
        {
            REQUIRE(ParallelSum(*s, data, chunk) == expected);
        }
    }
}

TEST_CASE("parallel_for honors ENGINE_JOB_THREADS=3 via the default scheduler config", "[jobs][parallel_for]")
{
    struct EnvRestore
    {
        engine_test::Text saved = engine_test::MakeText();
        bool had;
        EnvRestore()
        {
            const char* old = os::GetEnvironment("ENGINE_JOB_THREADS");
            had = old != nullptr;
            if (had)
            {
                saved = engine_test::MakeText(StringView(old));
            }
        }
        ~EnvRestore()
        {
            if (had)
            {
                test_support::SetEnv("ENGINE_JOB_THREADS", saved.CStr());
            }
            else
            {
                test_support::UnsetEnv("ENGINE_JOB_THREADS");
            }
        }
    } restore;
    test_support::SetEnv("ENGINE_JOB_THREADS", "3");
    {
        auto s = ParallelForTestMakeScheduler(0);  // default => reads the env override
        REQUIRE(s->ParticipantCount() == 3);
        auto data = MakeData(100'000);
        REQUIRE(ParallelSum(*s, data, 0) == 100'000ull * 99'999ull / 2);
    }
}

TEST_CASE("parallel_for edge cases", "[jobs][parallel_for]")
{
    for (usize workers : {usize{1}, usize{2}, usize{0}})
    {
        auto s = ParallelForTestMakeScheduler(workers);

        SECTION("count == 0 runs nothing")
        {
            Vector<u64> empty;
            u64 inv = 99;
            REQUIRE(ParallelSum(*s, empty, 10, &inv) == 0);
            REQUIRE(inv == 0);
        }
        SECTION("count == 1")
        {
            auto d = MakeData(1);
            u64 inv = 0;
            REQUIRE(ParallelSum(*s, d, 10, &inv) == 0);
            REQUIRE(inv == 1);
        }
        SECTION("chunk_size == 1 (one job per element)")
        {
            auto d = MakeData(1000);
            u64 inv = 0;
            REQUIRE(ParallelSum(*s, d, 1, &inv) == 1000ull * 999 / 2);
            REQUIRE(inv == 1000);
        }
        SECTION("chunk_size > count (single chunk)")
        {
            auto d = MakeData(50);
            u64 inv = 0;
            REQUIRE(ParallelSum(*s, d, 500, &inv) == 50ull * 49 / 2);
            REQUIRE(inv == 1);
        }
        SECTION("count not a multiple of chunk_size (short last chunk)")
        {
            auto d = MakeData(1001);
            u64 inv = 0;
            REQUIRE(ParallelSum(*s, d, 100, &inv) == 1001ull * 1000 / 2);
            REQUIRE(inv == 11);
        }
    }
}

TEST_CASE("The Job-returning ParallelFor joins inside a job and via Spawn", "[jobs][parallel_for]")
{
    struct Local
    {
        static Job Parent(const Vector<u64>* data, u64* out, Atomic<int>* done)
        {
            Vector<u64> partials = Zeros((data->Size() + 99) / 100);
            SumCtx ctx{data->Data(), partials.Data(), 100, {0}};
            Counter c;
            Spawn(c, ParallelFor(data->Size(), 100, SumChunk, &ctx));
            co_await c;
            u64 t = 0;
            for (usize i = 0; i < partials.Size(); ++i)
            {
                t += partials[i];
            }
            *out = t;
            done->FetchAdd(1);
        }
    };
    for (usize workers : {usize{1}, usize{2}, usize{0}})
    {
        auto s = ParallelForTestMakeScheduler(workers);
        auto data = MakeData(10'000);
        u64 out = 0;
        Atomic<int> done{0};
        s->Submit(Local::Parent(&data, &out, &done));
        s->HelpUntil(done, 1);
        REQUIRE(out == 10'000ull * 9'999 / 2);
    }
}

TEST_CASE("The typed callable wrapper forwards (begin, end)", "[jobs][parallel_for]")
{
    auto s = ParallelForTestMakeScheduler(2);
    Vector<Atomic<u8>> hit;  // sized once while empty, so the atomics never move
    hit.Resize(5000);
    for (usize i = 0; i < hit.Size(); ++i)
    {
        hit[i].Store(0);
    }
    auto fn = [&](usize begin, usize end)
    {
        for (usize i = begin; i < end; ++i)
        {
            hit[i].FetchAdd(1);
        }
    };
    ParallelForBlocking(*s, hit.Size(), 64, fn);
    for (usize i = 0; i < hit.Size(); ++i)
    {
        REQUIRE(hit[i].Load() == 1);
    }
}

TEST_CASE("Frame stats balance after parallel_for", "[jobs][parallel_for]")
{
    const u64 before = FramePool::Global().GetStats().Live();
    {
        auto s = ParallelForTestMakeScheduler(2);
        auto data = MakeData(20'000);
        ParallelSum(*s, data, 100);
    }
    REQUIRE(FramePool::Global().GetStats().Live() == before);
}

namespace
{
struct JobFormResult
{
    u64 sum = 0;
    u64 invocations = 0;
};

// Runs the Job form of ParallelFor (spawned and joined from the main
// thread via a Counter) over `n` elements.
JobFormResult RunJobForm(Scheduler& s, usize n, usize chunk)
{
    auto data = MakeData(n);
    usize eff = EffectiveChunkSize(n, chunk);
    usize chunks = n == 0 ? 1 : (n + eff - 1) / eff;
    Vector<u64> partials = Zeros(chunks);
    SumCtx ctx{data.Data(), partials.Data(), eff, {0}};
    Counter c(s);
    Spawn(c, ParallelFor(n, chunk, SumChunk, &ctx));
    s.HelpUntil(c);
    JobFormResult r;
    for (usize i = 0; i < partials.Size(); ++i)
    {
        r.sum += partials[i];
    }
    r.invocations = ctx.invocations.Load();
    return r;
}
}  // namespace

TEST_CASE("The Job form handles the same edge cases and configurations", "[jobs][parallel_for]")
{
    for (usize workers : {usize{1}, usize{2}, usize{0}})
    {
        auto s = ParallelForTestMakeScheduler(workers);
        auto r0 = RunJobForm(*s, 0, 10);
        REQUIRE(r0.invocations == 0);
        auto r1 = RunJobForm(*s, 1, 10);
        REQUIRE(r1.invocations == 1);
        REQUIRE(r1.sum == 0);
        auto rd = RunJobForm(*s, 100'000, 0);  // default chunk
        REQUIRE(rd.sum == 100'000ull * 99'999 / 2);
        auto rg = RunJobForm(*s, 50, 500);  // chunk > count
        REQUIRE(rg.invocations == 1);
        REQUIRE(rg.sum == 50ull * 49 / 2);
        auto rs = RunJobForm(*s, 1001, 100);  // short last chunk
        REQUIRE(rs.invocations == 11);
        REQUIRE(rs.sum == 1001ull * 1000 / 2);
    }
}

TEST_CASE("A huge chunk count is clamped instead of overflowing the queues", "[jobs][parallel_for]")
{
    CHECK(EffectiveChunkSize(100, 0) == kDefaultChunkSize);
    CHECK(EffectiveChunkSize(kMaxChunks * 10, 1) == 10);
    CHECK(EffectiveChunkSize(kMaxChunks * 10 + 1, 1) == 11);
    for (usize workers : {usize{1}, usize{4}})
    {
        auto s = ParallelForTestMakeScheduler(workers);
        // 200,000 single-element requests would be 200,000 jobs unclamped.
        auto r = RunJobForm(*s, 200'000, 1);  // from the main thread, joined via Counter
        REQUIRE(r.sum == 200'000ull * 199'999 / 2);
        REQUIRE(r.invocations <= kMaxChunks);
        // And the Job form really spawning from a worker: run it inside a job.
        struct Local
        {
            static Job Parent(usize n, u64* out, Atomic<int>* done)
            {
                Vector<u64> data = MakeData(n);
                usize eff = EffectiveChunkSize(n, 1);
                Vector<u64> partials = Zeros((n + eff - 1) / eff);
                SumCtx ctx{data.Data(), partials.Data(), eff, {0}};
                Counter c;
                Spawn(c, ParallelFor(n, 1, SumChunk, &ctx));
                co_await c;
                u64 t = 0;
                for (usize i = 0; i < partials.Size(); ++i)
                {
                    t += partials[i];
                }
                *out = t;
                done->FetchAdd(1);
            }
        };
        u64 out = 0;
        Atomic<int> done{0};
        s->Submit(Local::Parent(200'000, &out, &done));
        s->HelpUntil(done, 1);
        REQUIRE(out == 200'000ull * 199'999 / 2);
    }
}
