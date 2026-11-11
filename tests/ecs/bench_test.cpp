#include <catch2/catch_test_macros.hpp>

#include "bench.h"
#include "build_defs.h"
#include "cstr/cstr.h"

using namespace engine_placeholder;
using namespace ecs_bench;

namespace
{

Options Small()
{
    Options o;
    o.entities = 1000;
    o.frames = 3;
    o.warmup = 1;
    o.participants = 2;
    return o;
}

#if ENGINE_SANITIZER_ADDRESS || ENGINE_SANITIZER_THREAD
// (unused under a sanitizer: the ratio guard is skipped there)
#elif defined(ENGINE_BUILD_CONFIG)
constexpr const char* kBuildConfig = ENGINE_BUILD_CONFIG;  // from CMake: debug, release, relwithdebinfo, minsizerel
#else
constexpr const char* kBuildConfig = "debug";
#endif

}  // namespace

TEST_CASE("ecs_bench: both implementations compute the same checksum in every scenario", "[ecs][benchmark]")
{
    const Options o = Small();
    struct Scenario
    {
        const char* name;
        Result (*run)(Impl, const Options&);
    };
    const Scenario scenarios[] = {{"iteration", RunIteration},
                                  {"heavy", RunHeavy},
                                  {"churn", RunChurn},
                                  {"sparse", RunSparse},
                                  {"hierarchy", RunHierarchy}};
    for (const Scenario& s : scenarios)
    {
        INFO(s.name);
        const Result e = s.run(Impl::Ecs, o);
        const Result b = s.run(Impl::Oop, o);
        CHECK(e.valid);
        CHECK(ChecksumsMatch(e.checksum, b.checksum));
        CHECK(e.count == b.count);
    }
}

TEST_CASE("ecs_bench: the Changed scenario never visits fewer rows than were written", "[ecs][benchmark]")
{
    const Options o = Small();
    const Result e = RunChanged(Impl::Ecs, o, true);
    const Result b = RunChanged(Impl::Oop, o, true);
    CHECK(e.valid);
    CHECK(ChecksumsMatch(e.checksum, b.checksum));
    // The ECS filters by chunk, so it may visit more rows than were written (the whole chunk of the written rows),
    // never fewer.
    CHECK(e.count >= b.count);
    // Random writes: the same.
    const Result er = RunChanged(Impl::Ecs, o, false);
    const Result br = RunChanged(Impl::Oop, o, false);
    CHECK(ChecksumsMatch(er.checksum, br.checksum));
    CHECK(er.count >= br.count);
}

TEST_CASE("ecs_bench: churn keeps the world valid and the population constant", "[ecs][benchmark]")
{
    Options o = Small();
    o.frames = 10;
    const Result e = RunChurn(Impl::Ecs, o);
    CHECK(e.valid);
    CHECK(e.count == o.entities);
}

TEST_CASE("ecs_bench: the CSV text round-trips", "[ecs][benchmark]")
{
    const Options o = Small();
    Result r;
    r.time.median_ms = 1.25;
    r.time.p95_ms = 2.5;
    r.checksum = 12345.678901234567;
    Vector<Row> rows;
    rows.Push(MakeRow("iteration", Impl::Ecs, o, r));
    r.time.median_ms = 0.001953125;
    rows.Push(MakeRow("heavy", Impl::Oop, o, r));
    const Vector<char> text = FormatCsv(rows);
    REQUIRE(text.Size() > 0);
    Vector<Row> back;
    REQUIRE(ParseCsv(text.Data(), text.Size(), &back));
    REQUIRE(back.Size() == 2);
    for (usize i = 0; i < 2; ++i)
    {
        CHECK(CStrCompare(back[i].scenario, rows[i].scenario) == 0);
        CHECK(CStrCompare(back[i].impl, rows[i].impl) == 0);
        CHECK(back[i].entities == rows[i].entities);
        CHECK(back[i].participants == rows[i].participants);
        CHECK(back[i].median_ms == rows[i].median_ms);
        CHECK(back[i].p95_ms == rows[i].p95_ms);
        CHECK(back[i].checksum == rows[i].checksum);
    }
    const char bad[] = "h\nonly,three,fields\n";
    CHECK_FALSE(ParseCsv(bad, sizeof(bad) - 1, &back));
}

TEST_CASE("ecs_bench: Summarize takes the median and the 95th percentile", "[ecs][benchmark]")
{
    Vector<double> v;
    for (int i = 100; i >= 1; --i)
    {
        v.Push(static_cast<double>(i));
    }
    const Timing t = Summarize(&v);
    CHECK(t.median_ms == 51.0);
    CHECK(t.p95_ms == 96.0);
}

// The regression guard: the ECS stays well ahead of the baseline in the four-step iteration. Compared as a ratio so the
// machine does not matter; only meaningful when the code is optimized and unsanitized, so it is skipped otherwise (a
// debug build inverts the ratio).
TEST_CASE("ecs_bench: the ECS iterates at least twice as fast as the baseline (optimized builds)", "[ecs][benchmark]")
{
#if ENGINE_SANITIZER_ADDRESS || ENGINE_SANITIZER_THREAD
    SKIP("ratios are not meaningful under a sanitizer");
#else
    if (CStrCompare(kBuildConfig, "debug") == 0)
    {
        SKIP("ratios are not meaningful in a debug build");
    }
    Options o;
    o.entities = 10000;
    o.frames = 30;
    o.warmup = 5;
    const Result e = RunIteration(Impl::Ecs, o);
    const Result b = RunIteration(Impl::Oop, o);
    CHECK(ChecksumsMatch(e.checksum, b.checksum));
    CHECK(b.time.median_ms >= 2.0 * e.time.median_ms);
#endif
}
