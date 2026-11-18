// The LuaJIT backend (Phase 27 Day 448): the shared conformance suite, then what is particular to Lua: tagged value
// kinds, error traces, natives that fail, dotted paths, the memory cap and a handover between threads.
#include <catch2/catch_test_macros.hpp>

#include "backend_conformance.h"
#include "script/backend_registry.h"
#include "script/luajit/luajit_backend.h"

using namespace engine_placeholder;
using namespace engine_placeholder::script;
using namespace engine_placeholder::script::test;

namespace
{

ConformanceKit LuaKit()
{
    ConformanceKit kit;
    kit.prelude = "function echo(a) return a end\n"
                  "function multi() return 1, 2.5, 'x' end\n"
                  "function fail() local t = nil; return t.x end\n"
                  "function calls_native() return host_add(2, 3) end\n"
                  "function waiter() local v = wait(0.25); return v end\n"
                  "function setg(v) stored = v end\n"
                  "function getg() return stored end\n"
                  "function spin() while true do end end\n";
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
    // The budget cases wait for Day 449: they are skipped because the backend does not advertise budgets.
    CHECK(report.skipped.Size() >= 3);
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
