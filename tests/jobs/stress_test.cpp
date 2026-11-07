// Phase 2 Day 35: stress & throughput suite for the whole job system.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "jobs/counter.h"
#include "jobs/frame_pool.h"
#include "jobs/scheduler.h"
#include "jobs/topology.h"

using namespace engine_placeholder;
using namespace engine_placeholder::jobs;
using Clock = std::chrono::steady_clock;

#if defined(__SANITIZE_THREAD__)
#define JOBS_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define JOBS_TSAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define JOBS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define JOBS_ASAN 1
#endif
#endif

#if defined(JOBS_TSAN)
inline constexpr int kGraphSeeds = 20;
inline constexpr int kPipelineBatches = 20; // x100 pipelines
inline constexpr bool kSanitized = true;
#elif defined(JOBS_ASAN)
inline constexpr int kGraphSeeds = 60;
inline constexpr int kPipelineBatches = 100;
inline constexpr bool kSanitized = true;
#else
inline constexpr int kGraphSeeds = 200;
inline constexpr int kPipelineBatches = 100;
inline constexpr bool kSanitized = false;
#endif

namespace {

std::unique_ptr<Scheduler> MakeScheduler(usize n) {
    Scheduler::Config cfg;
    cfg.participants = n;
    return std::make_unique<Scheduler>(cfg);
}

u64 NextRand(u64& s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

// Snapshot of FramePool counters across a test, for the histogram.
struct PoolWatch {
    FramePool::Stats before = FramePool::Global().GetStats();
    void Report(const char* what) const {
        FramePool::Stats now = FramePool::Global().GetStats();
        std::printf("[frame sizes] %s:", what);
        for (usize c = 0; c < FramePool::kClassCount; ++c) {
            std::printf("  <=%zuB: %llu", static_cast<size_t>(FramePool::kBlockSizes[c] - FramePool::kHeaderBytes),
                        static_cast<unsigned long long>(now.pooled_allocs[c] - before.pooled_allocs[c]));
        }
        std::printf("  fallback: %llu\n", static_cast<unsigned long long>(now.fallback_allocs - before.fallback_allocs));
    }
    u64 FallbackDelta() const { return FramePool::Global().GetStats().fallback_allocs - before.fallback_allocs; }
};

// ---- randomized dependency graphs ------------------------------------------

struct GraphNode {
    u32 first_a = 0, count_a = 0; // child group A (spawned first)
    u32 first_b = 0, count_b = 0; // child group B (spawned after A joined)
    u32 spin = 0;
};

struct Graph {
    std::vector<GraphNode> nodes;
    std::vector<u32> children; // child ids, referenced by ranges
    std::unique_ptr<std::atomic<u32>[]> exec;
    std::unique_ptr<std::atomic<u32>[]> post;
    std::atomic<u32> violations{0};
};

void BuildGraph(Graph& g, u64 seed, u32 max_nodes) {
    u64 rng = seed * 0x9E3779B97F4A7C15ull + 1;
    // Per-seed shape parameters so graphs range from long skinny chains
    // to wide bushy trees and from tiny to the node cap.
    const u32 max_fan = 1 + static_cast<u32>(NextRand(rng) % 6);       // 1..6
    const u32 max_depth = 2 + static_cast<u32>(NextRand(rng) % 40);    // 2..41
    max_nodes = 20 + static_cast<u32>(NextRand(rng) % (max_nodes - 19));
    g.nodes.clear();
    g.children.clear();
    g.nodes.push_back({});
    struct Item {
        u32 id, depth;
    };
    std::vector<Item> queue{{0, 0}};
    for (usize qi = 0; qi < queue.size(); ++qi) {
        Item it = queue[qi];
        u32 fan_a = it.depth < max_depth ? static_cast<u32>(NextRand(rng) % (max_fan + 1)) : 0;
        u32 fan_b = it.depth < max_depth ? static_cast<u32>(NextRand(rng) % (max_fan / 2 + 1)) : 0;
        if (g.nodes.size() + fan_a + fan_b > max_nodes) fan_a = fan_b = 0;
        GraphNode& n = g.nodes[it.id];
        n.spin = static_cast<u32>(NextRand(rng) % 200);
        n.first_a = static_cast<u32>(g.children.size());
        n.count_a = fan_a;
        for (u32 i = 0; i < fan_a; ++i) {
            u32 cid = static_cast<u32>(g.nodes.size());
            g.nodes.push_back({});
            g.children.push_back(cid);
            queue.push_back({cid, it.depth + 1});
        }
        // (g.nodes may have reallocated: re-fetch before touching n again)
        GraphNode& n2 = g.nodes[it.id];
        n2.first_b = static_cast<u32>(g.children.size());
        n2.count_b = fan_b;
        for (u32 i = 0; i < fan_b; ++i) {
            u32 cid = static_cast<u32>(g.nodes.size());
            g.nodes.push_back({});
            g.children.push_back(cid);
            queue.push_back({cid, it.depth + 1});
        }
    }
    g.exec = std::make_unique<std::atomic<u32>[]>(g.nodes.size());
    g.post = std::make_unique<std::atomic<u32>[]>(g.nodes.size());
    for (usize i = 0; i < g.nodes.size(); ++i) {
        g.exec[i].store(0);
        g.post[i].store(0);
    }
}

void Spin(u32 n) {
    for (volatile u32 i = 0; i < n; i = i + 1) {
    }
}

bool GroupComplete(Graph* g, u32 first, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        u32 c = g->children[first + i];
        if (g->exec[c].load(std::memory_order_acquire) != 1 || g->post[c].load(std::memory_order_acquire) != 1)
            return false;
    }
    return true;
}

Job NodeJob(Graph* g, u32 id) {
    g->exec[id].fetch_add(1, std::memory_order_acq_rel);
    const GraphNode n = g->nodes[id];
    Spin(n.spin);
    if (n.count_a > 0) {
        Counter c;
        for (u32 i = 0; i < n.count_a; ++i) Spawn(c, NodeJob(g, g->children[n.first_a + i]));
        co_await c;
        // Dependency check: everything in group A finished before we continue.
        if (!GroupComplete(g, n.first_a, n.count_a)) g->violations.fetch_add(1);
    }
    if (n.count_b > 0) {
        Counter c;
        for (u32 i = 0; i < n.count_b; ++i) Spawn(c, NodeJob(g, g->children[n.first_b + i]));
        co_await c;
        if (!GroupComplete(g, n.first_b, n.count_b) || !GroupComplete(g, n.first_a, n.count_a))
            g->violations.fetch_add(1);
    }
    g->post[id].fetch_add(1, std::memory_order_acq_rel);
}

// Helping wait with a watchdog: a deadlock/lost wakeup must fail the test
// with a diagnostic, never hang ctest. On timeout the live jobs still
// reference the caller's stack, so the only safe reaction is to report and
// abort the (per-test-case) process.
struct WaitCtx {
    bool (*done)(void*);
    void* user;
    Clock::time_point deadline;
    bool timed_out = false;
};

void HelpOrAbort(Scheduler& s, bool (*done)(void*), void* user, int seconds, const char* what) {
    WaitCtx w{done, user, Clock::now() + std::chrono::seconds(seconds)};
    s.HelpUntil(
        [](void* u) {
            auto* c = static_cast<WaitCtx*>(u);
            if (c->done(c->user)) return true;
            if (Clock::now() >= c->deadline) {
                c->timed_out = true;
                return true;
            }
            return false;
        },
        &w);
    if (w.timed_out) {
        std::printf("WATCHDOG: %s did not complete within %ds (deadlock or lost wakeup)\n", what, seconds);
        std::fflush(stdout);
        std::abort();
    }
}

bool CounterZero(void* c) { return static_cast<Counter*>(c)->IsZero(); }
bool AtomicAtLeast(void* p) {
    auto* pr = static_cast<std::pair<std::atomic<int>*, int>*>(p);
    return pr->first->load(std::memory_order_acquire) >= pr->second;
}
void HelpCounter(Scheduler& s, Counter& c, int seconds, const char* what) { HelpOrAbort(s, CounterZero, &c, seconds, what); }
void HelpAtLeast(Scheduler& s, std::atomic<int>& v, int target, int seconds, const char* what) {
    std::pair<std::atomic<int>*, int> pr{&v, target};
    HelpOrAbort(s, AtomicAtLeast, &pr, seconds, what);
}

// Runs one graph under the watchdog.
void RunGraph(Scheduler& s, Graph* g, u64 seed) {
    Counter root(s);
    Spawn(root, NodeJob(g, 0));
    char what[96];
    std::snprintf(what, sizeof(what), "graph seed %llu (replay: ENGINE_STRESS_SEED=%llu)",
                  static_cast<unsigned long long>(seed), static_cast<unsigned long long>(seed));
    HelpCounter(s, root, 60, what);
}

u64 SeedBase() {
    const char* e = std::getenv("ENGINE_STRESS_SEED");
    return e ? std::strtoull(e, nullptr, 10) : 1;
}

// ---- skewed load -------------------------------------------------------------

volatile u64 g_sink = 0;

u64 Burn(u64 iters) {
    u64 x = 0x12345;
    for (u64 i = 0; i < iters; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    return x;
}

// Iteration count that takes roughly `micros` microseconds.
u64 Calibrate(u64 micros) {
    u64 iters = 100'000;
    for (int attempt = 0; attempt < 5; ++attempt) {
        auto t0 = Clock::now();
        g_sink = g_sink ^ Burn(iters);
        double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
        if (us > 2000) return static_cast<u64>(static_cast<double>(iters) * micros / us) + 1;
        iters *= 4;
    }
    return iters;
}

struct SkewCtx {
    Counter* counter;
    u64 iters;
    std::atomic<u64>* sink;
    std::atomic<i32>* producer_index;
};

Job WorkJob(u64 iters, std::atomic<u64>* sink) {
    sink->fetch_xor(Burn(iters), std::memory_order_relaxed);
    co_return;
}

// Everything is spawned from ONE participant's deque: the producer.
Job ProducerJob(SkewCtx ctx, int jobs) {
    ctx.producer_index->store(WorkerPool::CurrentParticipantIndex());
    for (int i = 0; i < jobs; ++i) Spawn(*ctx.counter, WorkJob(ctx.iters, ctx.sink));
    co_return;
}

struct SkewRun {
    double seconds = 0;
    std::vector<u64> resumes; // per participant
    std::vector<u64> steals;
    i32 producer = -1;
};

SkewRun RunSkew(usize participants, int jobs, u64 iters) {
    auto s = MakeScheduler(participants);
    std::atomic<u64> sink{0};
    std::atomic<i32> producer{-1};
    Counter c(*s);
    SkewCtx ctx{&c, iters, &sink, &producer};
    auto t0 = Clock::now();
    Spawn(c, ProducerJob(ctx, jobs));
    HelpCounter(*s, c, 120, "skewed-load run");
    SkewRun r;
    r.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
    s->Shutdown(); // quiesce so the stats are exact
    for (usize i = 0; i < s->ParticipantCount(); ++i) {
        r.resumes.push_back(s->ResumesBy(i));
        r.steals.push_back(s->StealsBy(i));
    }
    r.producer = producer.load();
    return r;
}

// ---- nested co_await ordering ---------------------------------------------------

struct Pipeline {
    std::atomic<u32> slot[5];
    std::atomic<u32> seq[5];
    std::atomic<u32> next{0};
    std::atomic<u32> inner_hits{0};
    std::atomic<u32> bad{0};
    Pipeline() {
        for (auto& s : slot) s.store(0);
        for (auto& s : seq) s.store(~0u);
    }
};

Job InnerJob(Pipeline* p) {
    p->inner_hits.fetch_add(1, std::memory_order_acq_rel);
    co_return;
}

Job StepJob(Pipeline* p, int step, u32 fanout) {
    // Reads the previous step's output slot: must already be written.
    if (step > 0 && p->slot[step - 1].load(std::memory_order_acquire) != static_cast<u32>(step)) p->bad.fetch_add(1);
    p->seq[step].store(p->next.fetch_add(1, std::memory_order_acq_rel), std::memory_order_release);
    if (fanout > 0) {
        Counter c;
        u32 before = p->inner_hits.load(std::memory_order_acquire);
        for (u32 i = 0; i < fanout; ++i) Spawn(c, InnerJob(p));
        co_await c;
        if (p->inner_hits.load(std::memory_order_acquire) < before + fanout) p->bad.fetch_add(1);
    }
    p->slot[step].store(static_cast<u32>(step) + 1, std::memory_order_release);
}

Job PipelineJob(Pipeline* p, u32 variant, std::atomic<int>* done) {
    for (int step = 0; step < 5; ++step) {
        Counter c;
        u32 fanout = ((variant >> step) & 1u) ? 3 : 0; // some steps fan out and join
        Spawn(c, StepJob(p, step, fanout));
        co_await c;
        if (p->slot[step].load(std::memory_order_acquire) != static_cast<u32>(step) + 1) p->bad.fetch_add(1);
    }
    done->fetch_add(1, std::memory_order_acq_rel);
}

} // namespace

TEST_CASE("Randomized dependency graphs: exactly-once, ordered joins, no deadlock", "[jobs][stress]") {
    PoolWatch watch;
    const u64 base = SeedBase();
    const u64 live_before = FramePool::Global().GetStats().Live();
    u64 total_jobs = 0;
    for (usize n : {usize{0}, usize{2}}) {
        auto s = MakeScheduler(n);
        for (int i = 0; i < kGraphSeeds; ++i) {
            const u64 seed = base + static_cast<u64>(i);
            auto g = std::make_unique<Graph>();
            BuildGraph(*g, seed, 4000);
            INFO("replay with ENGINE_STRESS_SEED=" << seed << " (graph of " << g->nodes.size() << " nodes, N=" << s->ParticipantCount() << ")");
            RunGraph(*s, g.get(), seed);
            u32 bad_exec = 0, bad_post = 0;
            for (usize k = 0; k < g->nodes.size(); ++k) {
                bad_exec += g->exec[k].load() != 1;
                bad_post += g->post[k].load() != 1;
            }
            REQUIRE(bad_exec == 0);
            REQUIRE(bad_post == 0);
            REQUIRE(g->violations.load() == 0);
            total_jobs += g->nodes.size();
        }
        s->Shutdown();
    }
    std::printf("[stress] %llu graph jobs over %d seeds x 2 configs\n", static_cast<unsigned long long>(total_jobs), kGraphSeeds);
    REQUIRE(FramePool::Global().GetStats().Live() == live_before);
    watch.Report("dependency graphs");
    REQUIRE(watch.FallbackDelta() == 0);
}

TEST_CASE("Skewed load: other participants execute stolen work", "[jobs][stress][steal]") {
    PoolWatch watch;
    const u64 iters = Calibrate(kSanitized ? 20 : 60);
    u32 cpus[kMaxParticipants];
    const usize allowed = GetAllowedCpus(cpus, kMaxParticipants);
    if (allowed < 2) SKIP("needs >= 2 allowed CPUs so participants are not pinned onto one core");
    for (usize n : {usize{2}, usize{4}}) {
        if (n > allowed) continue; // oversubscribed pins make "every participant ran" scheduling-dependent
        constexpr int kJobs = 3000;
        SkewRun r = RunSkew(n, kJobs, iters);
        REQUIRE(r.producer >= 0);
        u64 total = 0;
        for (u64 v : r.resumes) total += v;
        // All jobs + the producer itself ran, each exactly once (no job
        // suspends, so resumes == jobs).
        REQUIRE(total == static_cast<u64>(kJobs) + 1);
        u64 steals = 0;
        for (usize i = 0; i < r.resumes.size(); ++i) {
            steals += r.steals[i];
            if (static_cast<i32>(i) != r.producer) {
                INFO("participant " << i << " of " << n << " (producer was " << r.producer << ")");
                REQUIRE(r.resumes[i] > 0); // stole and executed work
            }
        }
        REQUIRE(steals > 0);
    }
    watch.Report("skewed load");
    REQUIRE(watch.FallbackDelta() == 0);
}

TEST_CASE("Throughput scales with participant count", "[jobs][stress][throughput]") {
    if (kSanitized) SKIP("timing is meaningless under sanitizers; run the plain jobs_tests for the speedup table");
    u32 cpus[kMaxParticipants];
    const usize allowed = GetAllowedCpus(cpus, kMaxParticipants);
    const u64 iters = Calibrate(60);
    constexpr int kJobs = 3000;

    std::vector<usize> configs;
    for (usize n : {usize{1}, usize{2}, usize{4}}) {
        if (n <= allowed) configs.push_back(n);
    }
    if (allowed > 4) configs.push_back(allowed);

    // Timing on a shared machine is noisy: measure up to 3 rounds (best of
    // 5 each) and accept the first round whose speedups meet the bound.
    bool ok = false;
    for (int round = 0; round < 3 && !ok; ++round) {
        std::vector<double> best(configs.size(), 1e30);
        for (usize ci = 0; ci < configs.size(); ++ci) {
            for (int rep = 0; rep < 5; ++rep) {
                SkewRun r = RunSkew(configs[ci], kJobs, iters);
                if (r.seconds < best[ci]) best[ci] = r.seconds;
            }
        }
        std::printf("\n[throughput] round %d: %d CPU-bound jobs (~60us each), best of 5\n  N   seconds   speedup\n", round + 1, kJobs);
        for (usize ci = 0; ci < configs.size(); ++ci) {
            std::printf("  %-3zu %-9.4f %.2fx\n", static_cast<size_t>(configs[ci]), best[ci], best[0] / best[ci]);
        }
        ok = true;
        for (usize ci = 0; ci < configs.size(); ++ci) {
            usize n = configs[ci];
            if (n != 2 && n != 4) continue; // larger N: logged only (SMT siblings make 0.5*N unreliable)
            if (best[0] / best[ci] < 0.5 * static_cast<double>(n)) ok = false;
        }
    }
    if (allowed < 4) SKIP("speedup asserted only with >= 4 allowed logical CPUs (table logged above)");
    REQUIRE(ok); // speedup(N) >= 0.5 * N for N = 2 and 4
}

TEST_CASE("Nested co_await chain of 5 dependent steps keeps strict order, 10,000 runs", "[jobs][stress][order]") {
    PoolWatch watch;
    for (usize n : {usize{1}, usize{2}, usize{0}}) {
        auto s = MakeScheduler(n);
        u64 rng = 77 + n;
        for (int b = 0; b < kPipelineBatches; ++b) {
            constexpr int kPerBatch = 100;
            std::vector<std::unique_ptr<Pipeline>> ps;
            std::atomic<int> done{0};
            for (int i = 0; i < kPerBatch; ++i) {
                ps.push_back(std::make_unique<Pipeline>());
                s->Submit(PipelineJob(ps.back().get(), static_cast<u32>(NextRand(rng)), &done));
            }
            HelpAtLeast(*s, done, kPerBatch, 60, "co_await pipeline batch");
            u32 bad = 0;
            for (auto& p : ps) {
                bad += p->bad.load();
                // Strict order: step k's sequence stamp precedes step k+1's.
                for (int step = 1; step < 5; ++step) bad += !(p->seq[step - 1].load() < p->seq[step].load());
            }
            REQUIRE(bad == 0);
        }
    }
    watch.Report("co_await chains");
    REQUIRE(watch.FallbackDelta() == 0);
}
