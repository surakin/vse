// The LuaJIT backend (Phase 27 Day 448): the shared conformance suite, then what is particular to Lua: tagged value
// kinds, error traces, natives that fail, dotted paths, the memory cap and a handover between threads.
#include <catch2/catch_test_macros.hpp>

#include "backend_conformance.h"
#include "script/backend_registry.h"
#include "os/os.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "script/luajit/luajit_backend.h"

using namespace engine_placeholder;
using namespace engine_placeholder::script;
using namespace engine_placeholder::script::test;

namespace
{

// `with_loops` adds the sources that never end: only a budgeted state stops them when it loads them.
ConformanceKit LuaKit(bool with_loops = true)
{
    ConformanceKit kit;
    kit.prelude = "function echo(a) return a end\n"
                  "function multi() return 1, 2.5, 'x' end\n"
                  "function fail() local t = nil; return t.x end\n"
                  "function calls_native() return host_add(2, 3) end\n"
                  "function waiter() local v = wait(0.25); return v end\n"
                  "function setg(v) stored = v end\n"
                  "function getg() return stored end\n"
                  "function spin() while true do end end\n"
                  "function bomb() local t = {} for i = 1, 100000000 do t[i] = {i} end return #t end\n";
    kit.syntax_error = "function (";
    kit.exact_int64 = false;
    kit.hostile.Push("");
    kit.hostile.Push("))))((((");
    kit.hostile.Push("x = 'unterminated");
    kit.hostile.Push("x = 99999999999999999999999999 y = 1/0 z = 5 % 0");
    kit.hostile.Push("local t = {} t[t] = 1 error(t)");
    kit.hostile.Push("error('boom')");
    kit.hostile.Push("error()");
    kit.hostile.Push("\xff\xfe\x01 \x00 garbage");
    kit.hostile.Push("return coroutine");
    if (with_loops)
    {
        kit.hostile.Push("while true do end");
    }
    if (with_loops)
    {
        kit.hostile.Push("while true do pcall(function() while true do end end) end");
    }
    if (with_loops)
    {
        kit.hostile.Push("local s = 'x' while true do s = s .. s end");
    }
    static const String<HeapAllocator> parens = []
    {
        String<HeapAllocator> text(HeapAllocator::Default(), StringView("x = "));
        for (int i = 0; i < 200000; ++i)
        {
            text.Append('(');
        }
        return text;
    }();
    kit.hostile.Push(parens.CStr());
    return kit;
}

Outcome Fails(void*, Span<const Value>, Vector<Value>*)
{
    return Outcome(Status::RuntimeError, "native refused");
}

Outcome SumAll(void*, Span<const Value> args, Vector<Value>* results)
{
    f64 sum = 0;
    for (usize i = 0; i < args.Size(); ++i)
    {
        sum += args[i].IsNumber() ? args[i].ToDouble() : 0.0;
    }
    results->Push(Value::FromFloat(sum));
    return Outcome();
}

UniquePtr<State> Fresh(Backend& backend, const char* source, u64 cap = 0)
{
    UniquePtr<State> state = backend.CreateState(cap);
    REQUIRE(state.Get() != nullptr);
    if (source != nullptr)
    {
        const Outcome o = state->Load("t", source);
        INFO(o.message.CStr());
        REQUIRE(o.IsOk());
    }
    return state;
}

}  // namespace

TEST_CASE("LuaJIT backend: passes the conformance suite", "[script][luajit][conformance]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    const ConformanceReport report = RunConformance(*backend, LuaKit());
    for (usize i = 0; i < report.failed.Size(); ++i)
    {
        UNSCOPED_INFO("failed: " << report.failed[i].CStr());
    }
    CHECK(report.failed.Size() == 0);
    CHECK(report.passed >= 9);
    CHECK(report.skipped.Size() == 0);  // a budgeted state runs every case, the budget ones included
    CHECK_FALSE(report.Failed("instruction_budget"));
    CHECK_FALSE(report.Failed("memory_budget"));
}

TEST_CASE("LuaJIT backend: registers under its name", "[script][luajit][registry]")
{
    ClearBackendRegistryForTest();
    CHECK(luajit::RegisterLuaJitBackend());
    Outcome error;
    UniquePtr<Backend> made = CreateBackend("luajit", &error);
    REQUIRE(made.Get() != nullptr);
    CHECK(made->Name() == StringView("luajit"));
    CHECK(made->Version().StartsWith("LuaJIT 2.1"));
    ClearBackendRegistryForTest();
}

TEST_CASE("LuaJIT backend: tagged kinds and containers round trip", "[script][luajit]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(backend.Get() == nullptr ? *backend : *backend, "function echo(a) return a end");
    Vector<Value> samples;
    samples.Push(Value::FromVec2(Vec2{1.0f, 2.0f}));
    samples.Push(Value::FromVec3(Vec3{1.0f, 2.0f, 3.5f}));
    samples.Push(Value::FromVec4(Vec4{1.0f, 2.0f, 3.5f, -4.0f}));
    Quaternion q;
    q.x = 0.5f;
    q.w = 0.25f;
    samples.Push(Value::FromQuat(q));
    ecs::Entity e;
    e.index = 7;
    e.generation = 3;
    samples.Push(Value::FromEntity(e));
    const u8 raw[4] = {0, 1, 0, 255};
    samples.Push(Value::FromBytes(Span<const u8>(raw, 4)));
    samples.Push(Value::MakeArray());
    samples.Push(Value::MakeMap());
    Value nested = Value::MakeMap();
    Value list = Value::MakeArray();
    list.Push(Value::FromInt(1));
    list.Push(Value::FromVec3(Vec3{0, 1, 0}));
    nested.Set("a", Move(list));
    nested.Set("b", Value::FromText("t"));
    samples.Push(Move(nested));
    for (usize i = 0; i < samples.Size(); ++i)
    {
        Value args[1] = {samples[i]};
        CallOutput out;
        REQUIRE(state->Call("echo", Span<const Value>(args, 1), Budget(), &out).IsOk());
        REQUIRE(out.results.Size() == 1);
        INFO("sample " << i << " came back as kind " << static_cast<int>(out.results[0].Kind()));
        CHECK(out.results[0].Equals(samples[i]));
    }
}

TEST_CASE("LuaJIT backend: a map returns with its keys sorted", "[script][luajit]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend, "function make() return {zeta = 1, alpha = 2, mid = 3} end");
    CallOutput out;
    REQUIRE(state->Call("make", Span<const Value>(), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    REQUIRE(out.results[0].Size() == 3);
    CHECK(out.results[0].KeyAt(0) == StringView("alpha"));
    CHECK(out.results[0].KeyAt(1) == StringView("mid"));
    CHECK(out.results[0].KeyAt(2) == StringView("zeta"));
}

TEST_CASE("LuaJIT backend: values that cannot cross are refused as values", "[script][luajit]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend,
                                   "function fn() return print end\n"
                                   "function mixed() return {1, 2, x = 3} end\n"
                                   "function holes() return {1, nil, 3} end\n"
                                   "function cyc() local t = {} t.self = t return t end\n"
                                   "function wide() local t = {} for i = 1, 70000 do t[i] = i end return t end\n"
                                   "function ident(a) return a end\n");
    CallOutput out;
    CHECK(state->Call("fn", Span<const Value>(), Budget(), &out).status == Status::Invalid);
    CHECK(state->Call("mixed", Span<const Value>(), Budget(), &out).status == Status::Invalid);
    CHECK(state->Call("holes", Span<const Value>(), Budget(), &out).status == Status::Invalid);
    CHECK(state->Call("cyc", Span<const Value>(), Budget(), &out).status == Status::Invalid);
    CHECK(state->Call("wide", Span<const Value>(), Budget(), &out).status == Status::Invalid);
    Value big[1] = {Value::FromInt(i64{1} << 60)};
    CHECK(state->Call("ident", Span<const Value>(big, 1), Budget(), &out).status == Status::Invalid);
    Value ok[1] = {Value::FromInt(5)};
    CHECK(state->Call("ident", Span<const Value>(ok, 1), Budget(), &out).IsOk());  // still usable
}

TEST_CASE("LuaJIT backend: errors carry the message and a script trace", "[script][luajit]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend, "function inner() error('deep failure') end\n"
                                             "function outer() inner() end\n"
                                             "function table_error() error({code = 1}) end\n");
    CallOutput out;
    const Outcome o = state->Call("outer", Span<const Value>(), Budget(), &out);
    CHECK(o.status == Status::RuntimeError);
    CHECK(o.message.View().Contains("t:1:"));
    CHECK(o.message.View().Contains("deep failure"));
    CHECK(o.trace.View().Contains("inner"));
    INFO(o.trace.CStr());
    CHECK(o.trace.View().Contains("t:2:"));
    const Outcome t = state->Call("table_error", Span<const Value>(), Budget(), &out);
    CHECK(t.status == Status::RuntimeError);
    CHECK(t.message.View().Contains("error object is a table"));
    const Outcome syntax = state->Load("bad", "x = = 1");
    CHECK(syntax.status == Status::SyntaxError);
    CHECK(syntax.message.View().StartsWith("bad:1:"));
    const Outcome load_error = state->Load("runtime", "\n\nerror('at load')");
    CHECK(load_error.status == Status::RuntimeError);
    CHECK(load_error.message.View().Contains("runtime:3:"));
    CHECK(load_error.trace.View().Contains("stack traceback"));
}

TEST_CASE("LuaJIT backend: natives report failures to the script, dotted paths work", "[script][luajit]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend, nullptr);
    REQUIRE(state->RegisterFunction("host.fails", &Fails, nullptr).IsOk());
    REQUIRE(state->RegisterFunction("host.sum", &SumAll, nullptr).IsOk());
    REQUIRE(state
                ->Load("t", "game = {}\n"
                            "function game.run() return host.sum(1, 2, 3.5) end\n"
                            "function game.guard() local ok, err = pcall(host.fails) return ok, err end\n"
                            "function game.raw() return host.fails() end\n")
                .IsOk());
    CallOutput out;
    REQUIRE(state->Call("game.run", Span<const Value>(), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    CHECK(out.results[0].Equals(Value::FromFloat(6.5)));
    const Outcome raw = state->Call("game.raw", Span<const Value>(), Budget(), &out);
    CHECK(raw.status == Status::RuntimeError);
    CHECK(raw.message.View().Contains("native refused"));
    CHECK(state->Call("game.missing", Span<const Value>(), Budget(), &out).status == Status::RuntimeError);
    CHECK(state->Call("nothing.here", Span<const Value>(), Budget(), &out).status == Status::RuntimeError);
}

TEST_CASE("LuaJIT backend: the memory cap is an error value and the state survives", "[script][luajit]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend,
                                   "function hog() local t = {} for i = 1, 100000000 do t[i] = {i} end return #t end\n"
                                   "function small() return 1 end\n",
                                   u64{8} << 20);
    CallOutput out;
    CHECK(state->Call("hog", Span<const Value>(), Budget(), &out).status == Status::OutOfMemory);
    state->Collect();
    INFO("used " << state->MemoryUsed());
    CHECK(state->MemoryUsed() < (u64{1} << 20));
    const Outcome again = state->Call("small", Span<const Value>(), Budget(), &out);
    INFO(again.message.CStr());
    CHECK(again.IsOk());
}

TEST_CASE("LuaJIT backend: file and loader functions are not available", "[script][luajit]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend,
                                   "function probe() return dofile, loadfile, load, loadstring, io, os, require end");
    CallOutput out;
    REQUIRE(state->Call("probe", Span<const Value>(), Budget(), &out).IsOk());
    for (usize i = 0; i < out.results.Size(); ++i)
    {
        CHECK(out.results[i].IsNil());
    }
}

TEST_CASE("LuaJIT backend: a state is handed to another thread", "[script][luajit]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend, "n = 0\nfunction bump() n = n + 1 return n end");
    CallOutput out;
    REQUIRE(state->Call("bump", Span<const Value>(), Budget(), &out).IsOk());
    state->HandOver();
    Atomic<u32> result(0);
    State* raw = state.Get();
    Thread worker(
        [raw, &result]()
        {
            raw->Acquire();
            CallOutput o;
            if (raw->Call("bump", Span<const Value>(), Budget(), &o).IsOk() && o.results.Size() == 1 &&
                o.results[0].Equals(Value::FromInt(2)))
            {
                result.Store(1, MemoryOrder::Release);
            }
            raw->HandOver();
        });
    worker.Join();
    state->Acquire();
    CHECK(result.Load(MemoryOrder::Acquire) == 1);
}

TEST_CASE("LuaJIT backend: a huge error message is cut", "[script][luajit]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend, "function big() error(string.rep('x', 10000000)) end");
    CallOutput out;
    const Outcome o = state->Call("big", Span<const Value>(), Budget(), &out);
    CHECK(o.status == Status::RuntimeError);
    CHECK(o.message.Length() <= 4096);
    CHECK(o.trace.Length() <= 16384);
    const Outcome load = state->Load("big2", "error(string.rep('y', 10000000))");
    CHECK(load.status == Status::RuntimeError);
    CHECK(load.message.Length() <= 4096);
    CHECK(load.trace.Length() <= 16384);
}

TEST_CASE("LuaJIT backend: precompiled bytecode is refused and string.dump is gone", "[script][luajit]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend, "function has_dump() return string.dump end");
    const Outcome o = state->Load("bc", "\x1b\x4c\x4a\x02\x00garbage bytecode");
    CHECK(o.status == Status::SyntaxError);
    CallOutput out;
    REQUIRE(state->Call("has_dump", Span<const Value>(), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    CHECK(out.results[0].IsNil());
}

TEST_CASE("LuaJIT backend: many references to one big string do not amplify", "[script][luajit]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend,
                                   "function amp() local s = string.rep('x', 1000000) local t = {} "
                                   "for i = 1, 60 do t[i] = s end return t end\n"
                                   "function keys() local t = {} t[string.rep('k', 2000000)] = 1 return t end\n"
                                   "function fine() local s = string.rep('x', 1000000) return {s, s} end\n");
    CallOutput out;
    CHECK(state->Call("amp", Span<const Value>(), Budget(), &out).status == Status::Invalid);
    CHECK(out.results.Size() == 0);
    CHECK(state->Call("keys", Span<const Value>(), Budget(), &out).status == Status::Invalid);
    CHECK(state->Call("fine", Span<const Value>(), Budget(), &out).IsOk());
}

namespace
{

Budget TimeBudget(u64 ms)
{
    Budget b;
    b.nanoseconds = ms * 1'000'000;
    return b;
}

// Calls `fn`, expecting `want`, within `limit_ms` of wall time (the time budget is 200 ms; the slack is for slow
// machines).
void ExpectStops(State& state, const char* fn, Status want, u64 limit_ms = 3000)
{
    CallOutput out;
    const u64 t0 = os::MonotonicTimeNanos();
    const Outcome o = state.Call(fn, Span<const Value>(), TimeBudget(200), &out);
    const u64 ms = (os::MonotonicTimeNanos() - t0) / 1'000'000;
    INFO(fn << ": " << StatusName(o.status) << " " << o.message.CStr() << " after " << ms << " ms");
    CHECK(o.status == want);
    CHECK(ms < limit_ms);
}

}  // namespace

TEST_CASE("LuaJIT sandbox: loops, swallowed budget errors and coroutines are stopped", "[script][luajit][sandbox]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(
        *backend,
        "function loop() while true do end end\n"
        "function swallow() while true do pcall(function() while true do end end) end end\n"
        "function swallow2() while true do xpcall(function() while true do end end, function() end) end end\n"
        "function nested() local function f() while true do pcall(f) end end f() end\n"
        "function co_loop() local co = coroutine.wrap(function() while true do end end) co() end\n"
        "function co_swallow() while true do coroutine.resume(coroutine.create(function() while true do end end)) end end\n"
        "function rec() local function r() return 1 + r() end return r() end\n"
        "function doubling() local s = 'x' while true do s = s .. s end end\n"
        "function ok() return 1 end\n");
    for (const char* fn : {"loop", "swallow", "swallow2", "nested", "co_loop", "co_swallow"})
    {
        ExpectStops(*state, fn, Status::BudgetExceeded);
    }
    ExpectStops(*state, "rec", Status::RuntimeError);
    CallOutput out;
    Budget memory;
    memory.memory_bytes = 8 << 20;
    memory.nanoseconds = 2'000'000'000;
    const Outcome d = state->Call("doubling", Span<const Value>(), memory, &out);
    CHECK((d.status == Status::OutOfMemory || d.status == Status::RuntimeError));
    CHECK(state->Call("ok", Span<const Value>(), Budget(), &out).IsOk());  // the state survived all of it
}

TEST_CASE("LuaJIT sandbox: an instruction budget is counted", "[script][luajit][sandbox]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend, "function work(n) local x = 0 for i = 1, n do x = x + i end return x end");
    Budget budget;
    budget.instructions = 100000;
    CallOutput out;
    Value small[1] = {Value::FromInt(1000)};
    Value big[1] = {Value::FromInt(100000000)};
    CHECK(state->Call("work", Span<const Value>(small, 1), budget, &out).IsOk());
    CHECK(state->Call("work", Span<const Value>(big, 1), budget, &out).status == Status::BudgetExceeded);
    CHECK(state->Call("work", Span<const Value>(small, 1), budget, &out).IsOk());  // the budget is per call
}

TEST_CASE("LuaJIT sandbox: a trusted state refuses instruction and time budgets, a budgeted one honors them",
          "[script][luajit][sandbox]")
{
    luajit::LuaJitOptions trusted;
    trusted.budgeted = false;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend(trusted);
    CHECK(backend->Caps().jit);
    CHECK_FALSE(backend->Caps().instruction_budget);
    CHECK(backend->Caps().memory_budget);
    UniquePtr<State> state = Fresh(*backend, "function f() return 1 end");
    CallOutput out;
    Budget instructions;
    instructions.instructions = 1000;
    CHECK(state->Call("f", Span<const Value>(), instructions, &out).status == Status::Unsupported);
    CHECK(state->Call("f", Span<const Value>(), TimeBudget(10), &out).status == Status::Unsupported);
    CHECK(state->Call("f", Span<const Value>(), Budget(), &out).IsOk());
    // The shared suite, for the trusted mode: the budget cases that need a hook are skipped, the rest pass.
    const ConformanceReport report = RunConformance(*backend, LuaKit(false));
    CHECK(report.failed.Size() == 0);
}

TEST_CASE("LuaJIT sandbox: pattern matching stops after its step limit", "[script][luajit][sandbox]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(
        *backend,
        "function slow_balance() return (string.rep('(', 200000)):find('%b()') end\n"
        "function slow_plain() return (string.rep('a', 2000000)):find(string.rep('a', 400000) .. 'b', 1, true) end\n"
        "function slow_capture() return (string.rep('a', 3000000)):find('(a*)%1b') end\n"
        "function slow() return (string.rep('a', 3000)):find('.-.-.-.-.-.-b') end\n"
        "function slow_gsub() return (string.rep('a', 3000)):gsub('.-.-.-.-.-.-b', 'x') end\n"
        "function slow_match() return string.match(string.rep('a', 3000) .. 'c', '(.-)(.-)(.-)(.-)(.-)b') end\n"
        "function fine() return ('hello world'):find('o w') end\n");
    for (const char* fn : {"slow", "slow_gsub", "slow_match", "slow_balance", "slow_plain", "slow_capture"})
    {
        CallOutput out;
        const u64 t0 = os::MonotonicTimeNanos();
        const Outcome o = state->Call(fn, Span<const Value>(), Budget(), &out);
        INFO(fn << " " << o.message.CStr());
        CHECK(o.status == Status::RuntimeError);
        CHECK(o.message.View().Contains("pattern too complex"));
        CHECK((os::MonotonicTimeNanos() - t0) / 1'000'000 < 3000);
    }
    CallOutput out;
    REQUIRE(state->Call("fine", Span<const Value>(), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 2);
    CHECK(out.results[0].Equals(Value::FromInt(5)));
    CHECK(out.results[1].Equals(Value::FromInt(7)));
}

TEST_CASE("LuaJIT sandbox: the pattern functions behave like Lua 5.1's", "[script][luajit][sandbox]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(
        *backend,
        "function t1() return string.find('hello world', 'wor') end\n"
        "function t2() return string.find('hello world', 'o', 6) end\n"
        "function t3() return string.find('a.b', '.', 1, true) end\n"
        "function t4() return string.match('key=value', '(%w+)=(%w+)') end\n"
        "function t5() return string.match('  trim  ', '^%s*(.-)%s*$') end\n"
        "function t6() return string.gsub('hello world', 'o', '0') end\n"
        "function t7() return string.gsub('abc', '%w', '%0%0') end\n"
        "function t8() return string.gsub('hello', '', '-') end\n"
        "function t9() local r = {} for k, v in string.gmatch('a=1, b=2', '(%w+)=(%w+)') do r[#r + 1] = k .. v end return table.concat(r, ',') end\n"
        "function t10() return string.find('THE (quick) fox', '%((%a+)%)') end\n"
        "function t11() return string.match('f(a(b)c)d', '%b()') end\n"
        "function t12() return string.gsub('hello world', '(%w+)', function(w) return w:upper() end) end\n"
        "function t13() return string.gsub('$name is $age', '%$(%w+)', {name = 'Bob', age = 42}) end\n"
        "function t14() return string.match('2024-01-05', '(%d+)-(%d+)-(%d+)') end\n"
        "function t15() return string.match('abc', '()b()') end\n"
        "function t16() return ('x'):rep(3, ','), ('abc'):upper(), ('abc'):sub(2) end\n"
        "function t17() return string.find('abc', '[a-', 1) end\n");
    struct Case
    {
        const char* fn;
        const char* expect;  // the results rendered as text, joined by '|'
    };
    const Case cases[] = {{"t1", "7|9"},          {"t2", "8|8"},           {"t3", "2|2"},      {"t4", "key|value"},
                          {"t5", "trim"},         {"t6", "hell0 w0rld|2"}, {"t7", "aabbcc|3"}, {"t8", "-h-e-l-l-o-|6"},
                          {"t9", "a1,b2"},        {"t10", "5|11|quick"},   {"t11", "(a(b)c)"}, {"t12", "HELLO WORLD|2"},
                          {"t13", "Bob is 42|2"}, {"t14", "2024|01|05"},   {"t15", "2|3"},     {"t16", "x,x,x|ABC|bc"}};
    for (const Case& c : cases)
    {
        CallOutput out;
        const Outcome o = state->Call(c.fn, Span<const Value>(), Budget(), &out);
        INFO(c.fn << " " << o.message.CStr());
        REQUIRE(o.IsOk());
        String<HeapAllocator> text(HeapAllocator::Default(), StringView());
        for (usize i = 0; i < out.results.Size(); ++i)
        {
            if (i > 0)
            {
                text.Append('|');
            }
            const Value& v = out.results[i];
            if (v.Kind() == ValueKind::Text)
            {
                text.Append(v.Text());
            }
            else
            {
                char number[32];
                FormatTo(number, sizeof number, "%lld", static_cast<long long>(v.AsInt()));
                text.Append(StringView(number, CStrLength(number)));
            }
        }
        INFO(c.fn << " gave " << text.CStr());
        CHECK(text.View() == StringView(c.expect));
    }
    CallOutput out;
    const Outcome bad = state->Call("t17", Span<const Value>(), Budget(), &out);
    CHECK(bad.status == Status::RuntimeError);
    CHECK(bad.message.View().Contains("malformed pattern"));
}

TEST_CASE("LuaJIT sandbox: scripts see a whitelist and cannot change the engine's globals", "[script][luajit][sandbox]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = Fresh(*backend, nullptr);
    REQUIRE(state->RegisterFunction("host.sum", &SumAll, nullptr).IsOk());
    REQUIRE(
        state
            ->Load(
                "t",
                "function absent()\n"
                "  return io, os, package, debug, ffi, jit, require, module, collectgarbage, getfenv, setfenv, load, loadstring, dofile, loadfile, newproxy, string.dump, coroutine.running\n"
                "end\n"
                "function present() return type(math.floor), type(string.format), type(table.insert), type(bit.band), type(pcall), type(print), type(host.sum) end\n"
                "function own_globals() counter = (counter or 0) + 1 return counter end\n"
                "function try(f) local ok, err = pcall(f) return ok end\n"
                "function write_math() math.floor = nil end\n"
                "function write_string() string.rep = nil end\n"
                "function write_host() host.sum = nil end\n"
                "function new_host() host.extra = 1 end\n"
                "function write_meta() setmetatable(_G, nil) end\n"
                "function raw_math() rawset(math, 'floor', nil) end\n"
                "function raw_host() rawset(host, 'sum', nil) end\n"
                "function raw_own() local t = {} rawset(t, 'a', 1) return t.a end\n"
                "function get_meta() return getmetatable('') end\n"
                "function shadow() print = 5 return print end\n"
                "function say() print('hello', 1, nil, true, {}) end\n")
            .IsOk());
    CallOutput out;
    REQUIRE(state->Call("absent", Span<const Value>(), Budget(), &out).IsOk());
    for (usize i = 0; i < out.results.Size(); ++i)
    {
        CHECK(out.results[i].IsNil());
    }
    REQUIRE(state->Call("present", Span<const Value>(), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 7);
    for (usize i = 0; i < out.results.Size(); ++i)
    {
        CHECK(out.results[i].Equals(Value::FromText("function")));
    }
    REQUIRE(state->Call("own_globals", Span<const Value>(), Budget(), &out).IsOk());
    REQUIRE(state->Call("own_globals", Span<const Value>(), Budget(), &out).IsOk());
    CHECK(out.results[0].Equals(Value::FromInt(2)));
    for (const char* fn :
         {"write_math", "write_string", "write_host", "new_host", "write_meta", "raw_math", "raw_host"})
    {
        CallOutput o;
        const Outcome r = state->Call(fn, Span<const Value>(), Budget(), &o);
        INFO(fn << " " << r.message.CStr());
        CHECK(r.status == Status::RuntimeError);
    }
    REQUIRE(state->Call("raw_own", Span<const Value>(), Budget(), &out).IsOk());
    CHECK(out.results[0].Equals(Value::FromInt(1)));
    REQUIRE(state->Call("get_meta", Span<const Value>(), Budget(), &out).IsOk());
    CHECK(out.results[0].Equals(Value::FromBool(false)));
    // Shadowing a name in the script environment is the script's own business and leaves the engine's alone.
    REQUIRE(state->Call("shadow", Span<const Value>(), Budget(), &out).IsOk());
    CHECK(state->Call("say", Span<const Value>(), Budget(), &out).status ==
          Status::RuntimeError);  // print is shadowed now
}

TEST_CASE("LuaJIT sandbox: print goes to the log and math.random repeats per seed", "[script][luajit][sandbox]")
{
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> a = Fresh(
        *backend,
        "function r() return math.random(1000000), math.random(1000000), math.random() end function say() print('hello', 1, nil, true, {}) end");
    UniquePtr<State> b = Fresh(*backend,
                               "function r() return math.random(1000000), math.random(1000000), math.random() end");
    luajit::LuaJitOptions other;
    other.random_seed = 99;
    UniquePtr<Backend> backend2 = luajit::CreateLuaJitBackend(other);
    UniquePtr<State> c = Fresh(*backend2,
                               "function r() return math.random(1000000), math.random(1000000), math.random() end");
    CallOutput oa;
    CallOutput ob;
    CallOutput oc;
    REQUIRE(a->Call("r", Span<const Value>(), Budget(), &oa).IsOk());
    REQUIRE(b->Call("r", Span<const Value>(), Budget(), &ob).IsOk());
    REQUIRE(c->Call("r", Span<const Value>(), Budget(), &oc).IsOk());
    REQUIRE(oa.results.Size() == 3);
    for (usize i = 0; i < 3; ++i)
    {
        CHECK(oa.results[i].Equals(ob.results[i]));
    }
    CHECK_FALSE(oa.results[0].Equals(oc.results[0]));
    CHECK(a->Call("say", Span<const Value>(), Budget(), &oa).IsOk());
}
