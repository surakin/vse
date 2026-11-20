// The JIT-versus-interpreter seam (Phase 27 Day 470): the JIT is a configuration choice (`script.jit`, a budgeted world
// never has it, a build can compile it out), every mode gives the same results, a state with the JIT off compiles no
// machine code, and the machine code of many states is bounded by a setting.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "os/console.h"
#include "os/os.h"
#include "script/luajit/luajit_backend.h"
#include "script/script_runtime.h"
#include "script/script_settings.h"
#include "settings/registry.h"

using namespace vse;
using namespace vse::script;

namespace jit_test
{

const char* const kHot = R"LUA(
function hot(n)
  local s = 0
  for i = 1, n do
    s = s + (i % 7) * 3
  end
  return s
end
)LUA";

i64 Expected(i64 n)
{
    i64 s = 0;
    for (i64 i = 1; i <= n; ++i)
    {
        s += (i % 7) * 3;
    }
    return s;
}

luajit::LuaJitOptions Options(bool budgeted, bool jit, u32 code_kb = 0)
{
    luajit::LuaJitOptions options;
    options.budgeted = budgeted;
    options.jit = jit;
    options.jit_code_kb = code_kb;
    return options;
}

UniquePtr<State> MakeState(const luajit::LuaJitOptions& options, StringView source = StringView(kHot, CStrLength(kHot)))
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend(options);
    UniquePtr<State> state = backend->CreateState(0);
    REQUIRE(state.Get() != nullptr);
    REQUIRE(state->Load("jit_scripts", source).IsOk());
    return state;
}

i64 RunHot(State* state, i64 n)
{
    Value args[1] = {Value::FromInt(n)};
    CallOutput out;
    REQUIRE(state->Call("hot", Span<const Value>(args, 1), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    return out.results[0].AsInt();
}

// Source of `count` functions, each its own hot loop, so many traces compile.
String<HeapAllocator> ManyLoops(u32 count)
{
    String<HeapAllocator> source(HeapAllocator::Default());
    for (u32 i = 0; i < count; ++i)
    {
        char line[256];
        FormatTo(line, sizeof(line),
                 "function loop%u(n) local s = 0 for i = 1, n do s = s + (i %% %u) * %u + i * %u end return s end\n", i,
                 i % 11 + 2, i % 5 + 1, i % 3 + 1);
        source.Append(StringView(line, CStrLength(line)));
    }
    source.Append(StringView("function run_all(rounds, n)\n  local t = 0\n  for r = 1, rounds do\n"));
    for (u32 i = 0; i < count; ++i)
    {
        char line[96];
        FormatTo(line, sizeof(line), "    t = t + loop%u(n)\n", i);
        source.Append(StringView(line, CStrLength(line)));
    }
    source.Append(StringView("  end\n  return t\nend\n"));
    return source;
}

i64 RunAll(State* state, i64 rounds, i64 n)
{
    Value args[2] = {Value::FromInt(rounds), Value::FromInt(n)};
    CallOutput out;
    REQUIRE(state->Call("run_all", Span<const Value>(args, 2), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    return out.results[0].AsInt();
}

}  // namespace jit_test

using namespace jit_test;

TEST_CASE("JIT: the capability tells the truth, in every mode", "[script][jit]")
{
    struct Mode
    {
        bool budgeted;
        bool jit;
        bool expect;
    };
    const Mode modes[] = {
        {true, true, false}, {true, false, false}, {false, true, luajit::JitCompiledIn()}, {false, false, false}};
    for (const Mode& mode : modes)
    {
        INFO("budgeted " << mode.budgeted << " jit " << mode.jit);
        UniquePtr<Backend> backend = luajit::CreateLuaJitBackend(Options(mode.budgeted, mode.jit));
        CHECK(backend->Caps().jit == mode.expect);
        UniquePtr<State> state = backend->CreateState(0);
        REQUIRE(state.Get() != nullptr);
        CHECK(state->Caps().jit == mode.expect);
        JitStats stats;
        REQUIRE(state->JitReport(&stats).IsOk());
        CHECK(stats.enabled == mode.expect);
    }
}

TEST_CASE("JIT: a hot loop gives the same result in every mode, and only the JIT compiles it", "[script][jit]")
{
    const i64 n = 3000000;
    const i64 expected = Expected(n);
    const bool compiled = luajit::JitCompiledIn();
    struct Mode
    {
        bool budgeted;
        bool jit;
    };
    const Mode modes[] = {{true, true}, {true, false}, {false, true}, {false, false}};
    for (const Mode& mode : modes)
    {
        INFO("budgeted " << mode.budgeted << " jit " << mode.jit);
        UniquePtr<State> state = MakeState(Options(mode.budgeted, mode.jit));
        CHECK(RunHot(state.Get(), n) == expected);
        JitStats stats;
        REQUIRE(state->JitReport(&stats).IsOk());
        const bool on = compiled && !mode.budgeted && mode.jit;
        CHECK(stats.enabled == on);
        if (on)
        {
            CHECK(stats.traces > 0);
            CHECK(stats.code_bytes > 0);
        }
        else
        {  // a state with the JIT off compiled no machine code
            CHECK(stats.traces == 0);
            CHECK(stats.code_bytes == 0);
        }
    }
}

TEST_CASE("JIT: the settings choose the options", "[script][jit][settings]")
{
    settings::Registry registry;
    RegisterScriptSettings(&registry);
    CHECK(ScriptJit(registry));
    CHECK(ScriptJitCodeKb(registry) == 0);
    luajit::LuaJitOptions defaults = luajit::OptionsFromSettings(registry);
    CHECK(defaults.budgeted);
    CHECK(defaults.jit);
    CHECK(defaults.jit_code_kb == 0);
    CHECK(registry.Find(StringView("script.jit")).IsValid());
    CHECK(registry.Find(StringView("script.jit_code_kb")).IsValid());
}

TEST_CASE("JIT: 256 states compile the same loop, and the code cap bounds each one", "[script][jit][parallel]")
{
    if (!luajit::JitCompiledIn())
    {
        SUCCEED("this build has no JIT compiler");
        return;
    }
    const String<HeapAllocator> source = ManyLoops(300);
    const i64 reference = [&]
    {
        UniquePtr<State> plain = MakeState(Options(true, false), source.View());
        return RunAll(plain.Get(), 25, 600);
    }();

    u64 uncapped_bytes = 0;
    u64 capped_bytes = 0;
    u64 capped_max = 0;
    for (u32 i = 0; i < 256; ++i)
    {
        // Every state is a separate, independent compiler: they are made and measured one after another here, and
        // dropped, so the test's own memory stays small; the totals are what a world of 256 partitions would hold.
        UniquePtr<State> capped = MakeState(Options(false, true, 64), source.View());
        CHECK(RunAll(capped.Get(), 25, 600) == reference);
        JitStats stats;
        REQUIRE(capped->JitReport(&stats).IsOk());
        CHECK(stats.traces > 0);
        CHECK(stats.code_bytes <= 64u * 1024u);
        capped_bytes += stats.code_bytes;
        capped_max = stats.code_bytes > capped_max ? stats.code_bytes : capped_max;
        if (i < 4)
        {
            UniquePtr<State> free_state = MakeState(Options(false, true, 0), source.View());
            CHECK(RunAll(free_state.Get(), 25, 600) == reference);
            JitStats free_stats;
            REQUIRE(free_state->JitReport(&free_stats).IsOk());
            uncapped_bytes += free_stats.code_bytes;
        }
    }
    INFO("per-state code, capped at 64 KiB: largest " << capped_max << " bytes, 256 states " << capped_bytes
                                                      << " bytes; uncapped, 4 states " << uncapped_bytes << " bytes");
    CHECK(capped_bytes > 0);
    CHECK(capped_bytes <= 256u * 64u * 1024u);
    CHECK(uncapped_bytes / 4 > capped_max);  // the cap bites: a free state keeps more than the largest capped one
}

TEST_CASE("JIT: a trusted world refuses an instruction budget, a budgeted one counts it", "[script][jit]")
{
    UniquePtr<State> trusted = MakeState(Options(false, true));
    Value args[1] = {Value::FromInt(1000)};
    CallOutput out;
    Budget budget;
    budget.instructions = 100;
    CHECK(trusted->Call("hot", Span<const Value>(args, 1), budget, &out).status == Status::Unsupported);
    UniquePtr<State> budgeted = MakeState(Options(true, true));
    const Outcome over = budgeted->Call("hot", Span<const Value>(args, 1), budget, &out);
    CHECK(!over.IsOk());
    CHECK(over.status != Status::Unsupported);
}

TEST_CASE("JIT: the runtime adds up the machine code of its partitions", "[script][jit][runtime]")
{
    // A budgeted world has none; the totals say so.
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend(Options(true, true));
    BindingRegistry bindings;
    REQUIRE(RegisterStandardBindings(&bindings));
    ScriptRuntimeConfig config;
    config.backend = backend.Get();
    config.bindings = &bindings;
    config.partitions = 4;
    ScriptRuntime runtime(config);
    ecs::World world;
    runtime.Tick(&world, 0.1);
    JitStats totals;
    REQUIRE(runtime.JitTotals(&totals).IsOk());
    CHECK(!totals.enabled);
    CHECK(totals.traces == 0);
    CHECK(totals.code_bytes == 0);
}

// Measurements, not checks: run with `script_tests "[.jit_bench]"` and read the lines it prints. The numbers of
// docs/guide/scripting.md ("JIT or interpreter") come from here.
TEST_CASE("JIT: bench of the modes and the code of 256 states", "[.jit_bench]")
{
    const i64 n = 200000000;
    struct Mode
    {
        const char* name;
        bool budgeted;
        bool jit;
        u64 instructions;
    };
    const Mode modes[] = {{"JIT (trusted)", false, true, 0},
                          {"interpreter, no budget (trusted, script.jit off)", false, false, 0},
                          {"interpreter with the count hook (budgeted)", true, true, 4000000000ull}};
    for (const Mode& mode : modes)
    {
        UniquePtr<State> state = MakeState(Options(mode.budgeted, mode.jit));
        Value args[1] = {Value::FromInt(n)};
        CallOutput out;
        Budget budget;
        budget.instructions = mode.instructions;
        const u64 start = os::MonotonicTimeNanos();
        const Outcome o = state->Call("hot", Span<const Value>(args, 1), budget, &out);
        const u64 end = os::MonotonicTimeNanos();
        char line[256];
        FormatTo(line, sizeof(line), "%s: %llu ms (ok %d)\n", mode.name,
                 static_cast<unsigned long long>((end - start) / 1000000), o.IsOk() ? 1 : 0);
        os::PrintText(line);
    }
    const String<HeapAllocator> source = ManyLoops(300);
    u64 total = 0;
    u32 traces = 0;
    for (u32 i = 0; i < 256; ++i)
    {
        UniquePtr<State> state = MakeState(Options(false, true, 0), source.View());
        RunAll(state.Get(), 25, 600);
        JitStats stats;
        REQUIRE(state->JitReport(&stats).IsOk());
        total += stats.code_bytes;
        traces += stats.traces;
    }
    char line[256];
    FormatTo(line, sizeof(line), "256 states, uncapped: %u traces, %llu bytes of machine code in all\n", traces,
             static_cast<unsigned long long>(total));
    os::PrintText(line);
}
