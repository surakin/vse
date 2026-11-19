// The binding registry (Phase 27 Day 450): typed signatures, argument errors, entity handle generations, thread rules,
// subset installs, the value-type operators and installing from many threads.
#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/luajit/luajit_backend.h"
#include "sync/atomic.h"
#include "sync/thread.h"

using namespace engine_placeholder;
using namespace engine_placeholder::script;

namespace
{

f32 Add3(f32 a, f32 b, f32 c)
{
    return a + b + c;
}

i32 Twice(i32 x)
{
    return x * 2;
}

bool Not(bool b)
{
    return !b;
}

i32 TextLength(StringView s)
{
    return static_cast<i32>(s.Length());
}

i32 EntityIndex(ecs::Entity e)
{
    return static_cast<i32>(e.index);
}

Value Pass(Value v)
{
    return v;
}

void Nothing() {}

i32 Forty()
{
    return 40;
}

struct World
{
    u32 live_generation = 1;  // the entity with index 7 is alive at this generation
    static bool Alive(void* user, ecs::Entity e)
    {
        const World* w = static_cast<const World*>(user);
        return e.index == 7 && e.generation == w->live_generation;
    }
};

struct Rig
{
    BindingRegistry registry;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = backend->CreateState(0);
    UniquePtr<Installation> installed;
    World world;

    Rig()
    {
        REQUIRE(RegisterStandardBindings(&registry));
        REQUIRE(registry.Add<&Add3>("test", "add3", ThreadRule::Pure, "a, b, c", "Adds three numbers."));
        REQUIRE(registry.Add<&Twice>("test", "twice", ThreadRule::Pure, "x", "Doubles an integer."));
        REQUIRE(registry.Add<&Not>("test", "negate", ThreadRule::Pure, "b", "Negates a bool."));
        REQUIRE(registry.Add<&TextLength>("test", "length", ThreadRule::Pure, "s", "Counts bytes."));
        REQUIRE(registry.Add<&EntityIndex>("test", "index", ThreadRule::WorldRead, "e", "The entity's slot."));
        REQUIRE(registry.Add<&Pass>("test", "pass", ThreadRule::Pure, "v", "Returns its argument."));
        REQUIRE(registry.Add<&Nothing>("test", "nothing", ThreadRule::Pure, "", "Does nothing."));
        REQUIRE(registry.Add<&Forty>("test", "forty", ThreadRule::MainThreadOnly, "", "Main thread only."));
        REQUIRE(registry.Add<&Forty>("test", "write", ThreadRule::WorldWrite, "", "A world write."));
    }

    void Install(const InstallProfile& profile)
    {
        Outcome error;
        installed = registry.Install(state.Get(), profile, &error);
        REQUIRE(installed.Get() != nullptr);
        REQUIRE(error.IsOk());
    }

    EntityCheck Check() { return EntityCheck{&World::Alive, &world}; }

    Outcome Run(StringView source, StringView call, Span<const Value> args, CallOutput* out)
    {
        const Outcome loaded = state->Load("test", source);
        if (!loaded.IsOk())
        {
            return Outcome(loaded.status, loaded.message.View());
        }
        return state->Call(call, args, Budget(), out);
    }
};

}  // namespace

TEST_CASE("Bindings: a typed function is called with converted arguments and results", "[script][bindings]")
{
    Rig rig;
    rig.Install(InstallProfile::Main(rig.Check()));
    CallOutput out;
    REQUIRE(rig.Run("function run() return test.add3(1, 2.5, 3) end", "run", Span<const Value>(), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    CHECK(out.results[0].ToDouble() == 6.5);
    REQUIRE(rig.Run("function run() return test.twice(21), test.negate(false), test.length('abcd') end", "run",
                    Span<const Value>(), &out)
                .IsOk());
    REQUIRE(out.results.Size() == 3);
    CHECK(out.results[0].Equals(Value::FromInt(42)));
    CHECK(out.results[1].Equals(Value::FromBool(true)));
    CHECK(out.results[2].Equals(Value::FromInt(4)));
    REQUIRE(rig.Run("function run() return test.nothing() end", "run", Span<const Value>(), &out).IsOk());
    CHECK(out.results.Size() == 0);
    REQUIRE(
        rig.Run("function run() return test.pass({1, 'a', {k = true}}) end", "run", Span<const Value>(), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    CHECK(out.results[0].Kind() == ValueKind::Array);
}

TEST_CASE("Bindings: the stable id and the signature come from the declaration", "[script][bindings]")
{
    Rig a;
    Rig b;
    const BindingInfo* info = a.registry.Find("test.add3");
    REQUIRE(info != nullptr);
    CHECK(info->signature == StringView("(f32 a, f32 b, f32 c) -> f32"));
    CHECK(info->arity == 3);
    CHECK(info->id != 0);
    CHECK(info->id == b.registry.Find("test.add3")->id);  // the same name gives the same id in another registry
    CHECK(a.registry.FindById(info->id) == info);
    CHECK(a.registry.Find("test.nope") == nullptr);
    CHECK(a.registry.Find("test.nothing")->signature == StringView("() -> void"));
    CHECK(a.registry.Find("test.pass")->signature == StringView("(Value v) -> Value"));
}

TEST_CASE("Bindings: a wrong argument count or type is a script error that names the function", "[script][bindings]")
{
    Rig rig;
    rig.Install(InstallProfile::Main(rig.Check()));
    CallOutput out;
    Outcome o = rig.Run("function run() return test.add3(1, 2) end", "run", Span<const Value>(), &out);
    CHECK(o.status == Status::RuntimeError);
    CHECK(o.message.View().Contains("test.add3: expected 3 arguments, got 2"));
    o = rig.Run("function run() return test.twice(1, 2) end", "run", Span<const Value>(), &out);
    CHECK(o.message.View().Contains("test.twice: expected 1 argument, got 2"));
    o = rig.Run("function run() return test.add3(1, 'x', 3) end", "run", Span<const Value>(), &out);
    CHECK(o.status == Status::RuntimeError);
    CHECK(o.message.View().Contains("test.add3: argument 2 (b): expected f32, got text"));
    o = rig.Run("function run() return test.twice(1.5) end", "run", Span<const Value>(), &out);
    CHECK(o.message.View().Contains("test.twice: argument 1 (x): expected i32, got number"));
    o = rig.Run("function run() return test.twice(3000000000) end", "run", Span<const Value>(), &out);
    CHECK(o.status == Status::RuntimeError);
    o = rig.Run("function run() return test.negate(nil) end", "run", Span<const Value>(), &out);
    CHECK(o.message.View().Contains("expected bool, got nil"));
    o = rig.Run("function run() return test.length(5) end", "run", Span<const Value>(), &out);
    CHECK(o.message.View().Contains("expected StringView, got integer"));
    o = rig.Run("function run() return vec3.length({x = 1, y = 2, z = 3}) end", "run", Span<const Value>(), &out);
    CHECK(o.status == Status::RuntimeError);  // an untagged table is not a vec3
    // The script can catch it, and the state stays usable.
    REQUIRE(
        rig.Run("function run() local ok, e = pcall(test.add3, 1) return ok, e end", "run", Span<const Value>(), &out)
            .IsOk());
    REQUIRE(out.results.Size() == 2);
    CHECK(out.results[0].Equals(Value::FromBool(false)));
    REQUIRE(rig.Run("function run() return test.twice(4) end", "run", Span<const Value>(), &out).IsOk());
}

TEST_CASE("Bindings: a handle to a destroyed entity fails every call", "[script][bindings]")
{
    Rig rig;
    rig.Install(InstallProfile::Main(rig.Check()));
    CallOutput out;
    const Value alive = Value::FromEntity(ecs::Entity{7, 1});
    REQUIRE(rig.Run("function run(e) return test.index(e) end", "run", Span<const Value>(&alive, 1), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    CHECK(out.results[0].Equals(Value::FromInt(7)));
    // A handle kept in a table and in a global keeps working while the entity lives ...
    REQUIRE(rig.Run("held = { e = nil } function keep(e) held.e = e end function use() return test.index(held.e) end",
                    "keep", Span<const Value>(&alive, 1), &out)
                .IsOk());
    REQUIRE(rig.state->Call("use", Span<const Value>(), Budget(), &out).IsOk());
    // ... and every use fails once the generation moved on.
    rig.world.live_generation = 2;
    const Outcome o = rig.state->Call("use", Span<const Value>(), Budget(), &out);
    CHECK(o.status == Status::RuntimeError);
    CHECK(o.message.View().Contains("test.index: argument 1 (e): the entity 7:1 no longer exists"));
    const Outcome again = rig.state->Call("use", Span<const Value>(), Budget(), &out);
    CHECK(again.status == Status::RuntimeError);
    // The invalid handle (generation 0) is refused too, and a handle the world never had.
    const Value invalid = Value::FromEntity(ecs::Entity{7, 0});
    CHECK(rig.state->Call("test.index", Span<const Value>(&invalid, 1), Budget(), &out).status == Status::RuntimeError);
    rig.world.live_generation = 1;
    const Value other = Value::FromEntity(ecs::Entity{8, 1});
    CHECK(rig.state->Call("test.index", Span<const Value>(&other, 1), Budget(), &out).status == Status::RuntimeError);
}

TEST_CASE("Bindings: the registry refuses what it cannot document or mark", "[script][bindings]")
{
    BindingRegistry registry;
    Outcome error;
    CHECK_FALSE(registry.Add<&Twice>("test", "twice", ThreadRule::Unmarked, "x", "No mark.", &error));
    CHECK(error.status == Status::Invalid);
    CHECK(error.message.View().Contains("no thread rule"));
    CHECK_FALSE(registry.Add<&Twice>("Test", "twice", ThreadRule::Pure, "x", "Upper case.", &error));
    CHECK_FALSE(registry.Add<&Twice>("test", "", ThreadRule::Pure, "x", "Empty.", &error));
    CHECK_FALSE(registry.Add<&Twice>("test", "twice", ThreadRule::Pure, "x, y", "Wrong name count.", &error));
    CHECK_FALSE(registry.Add<&Twice>("test", "twice", ThreadRule::Pure, "", "Missing name.", &error));
    CHECK_FALSE(registry.Add<&Twice>("math", "twice", ThreadRule::Pure, "x", "A Lua library's name.", &error));
    CHECK(registry.Count() == 0);
    REQUIRE(registry.Add<&Twice>("test", "twice", ThreadRule::Pure, "x", "Fine."));
    CHECK_FALSE(registry.Add<&Twice>("test", "twice", ThreadRule::Pure, "x", "Again.", &error));
    CHECK(error.message.View().Contains("already registered"));
    CHECK(registry.Count() == 1);
    CHECK(StringView(ThreadRuleName(ThreadRule::WorldRead)) == StringView("world-read"));
    CHECK(StringView(ThreadRuleName(ThreadRule::MainThreadOnly)) == StringView("main-thread-only"));
}

TEST_CASE("Bindings: a main-thread-only function raises in a partition state", "[script][bindings]")
{
    Rig partition;
    partition.Install(InstallProfile::Partition(partition.Check()));
    CHECK(partition.installed->Refusing() == 1);
    CallOutput out;
    const Outcome o = partition.Run("function run() return test.forty() end", "run", Span<const Value>(), &out);
    CHECK(o.status == Status::RuntimeError);
    CHECK(o.message.View().Contains("test.forty is main-thread-only"));
    REQUIRE(partition.Run("function run() return test.write() end", "run", Span<const Value>(), &out).IsOk());
    Rig main;
    main.Install(InstallProfile::Main(main.Check()));
    CHECK(main.installed->Refusing() == 0);
    REQUIRE(main.Run("function run() return test.forty() end", "run", Span<const Value>(), &out).IsOk());
    CHECK(out.results[0].Equals(Value::FromInt(40)));
}

TEST_CASE("Bindings: a state is built from a subset of the registry", "[script][bindings]")
{
    Rig rig;
    rig.Install(InstallProfile::Task());
    usize not_pure = 0;  // everything that touches the world: the test's three, and the settings, entity and transform
                         // writes and reads
    for (usize i = 0; i < rig.registry.Count(); ++i)
    {
        not_pure += rig.registry.At(i).rule == ThreadRule::Pure ? 0 : 1;
    }
    CHECK(rig.installed->Skipped() == not_pure);
    CHECK(not_pure >= 5);
    CallOutput out;
    REQUIRE(rig.Run("function run() return test.twice(2), num.clamp(5, 0, 1) end", "run", Span<const Value>(), &out)
                .IsOk());
    const Value alive = Value::FromEntity(ecs::Entity{7, 1});
    CHECK(rig.Run("function run(e) return test.index(e) end", "run", Span<const Value>(&alive, 1), &out).status ==
          Status::RuntimeError);
    CHECK(rig.Run("function run() return test.write() end", "run", Span<const Value>(), &out).status ==
          Status::RuntimeError);
}

TEST_CASE("Bindings: vectors and quaternions behave like the math types", "[script][bindings]")
{
    Rig rig;
    rig.Install(InstallProfile::Task());
    CallOutput out;
    REQUIRE(rig.Run("function run()\n"
                    "  local a, b = vec3.new(1, 2, 3), vec3.new(4, 5, 6)\n"
                    "  local s = a + b\n"
                    "  return s, a * 2, 2 * a, b - a, -a, b / 2, a == vec3.new(1, 2, 3), a == b, tostring(a),\n"
                    "         vec3.dot(a, b), vec3.cross(a, b), vec3.length(vec3.new(3, 4, 0))\n"
                    "end",
                    "run", Span<const Value>(), &out)
                .IsOk());
    REQUIRE(out.results.Size() == 12);
    CHECK(out.results[0].AsVec3() == Vec3(5, 7, 9));
    CHECK(out.results[1].AsVec3() == Vec3(2, 4, 6));
    CHECK(out.results[2].AsVec3() == Vec3(2, 4, 6));
    CHECK(out.results[3].AsVec3() == Vec3(3, 3, 3));
    CHECK(out.results[4].AsVec3() == Vec3(-1, -2, -3));
    CHECK(out.results[5].AsVec3() == Vec3(2, 2.5f, 3));
    CHECK(out.results[6].Equals(Value::FromBool(true)));
    CHECK(out.results[7].Equals(Value::FromBool(false)));
    CHECK(out.results[8].Text() == StringView("vec3(1, 2, 3)"));
    CHECK(out.results[9].ToDouble() == 32.0);
    CHECK(out.results[10].AsVec3() == Vec3(-3, 6, -3));
    CHECK(out.results[11].ToDouble() == 5.0);
    REQUIRE(rig.Run("function run()\n"
                    "  local q = quat.from_axis_angle(vec3.new(0, 0, 1), 1.5707964)\n"
                    "  local v = q * vec3.new(1, 0, 0)\n"
                    "  local r = quat.rotate(q, vec3.new(1, 0, 0))\n"
                    "  return v, r, tostring(quat.identity()), q * q == quat.mul(q, q)\n"
                    "end",
                    "run", Span<const Value>(), &out)
                .IsOk());
    REQUIRE(out.results.Size() == 4);
    const Vec3 v = out.results[0].AsVec3();
    const Vec3 r = out.results[1].AsVec3();
    CHECK(v.x < 0.001f);
    CHECK(v.y > 0.999f);
    CHECK((r - v).Length() < 0.001f);
    CHECK(out.results[2].Text() == StringView("quat(0, 0, 0, 1)"));
    // Mixed or wrong operands are script errors.
    CHECK(rig.Run("function run() return vec3.new(1, 2, 3) + 1 end", "run", Span<const Value>(), &out).status ==
          Status::RuntimeError);
    CHECK(rig.Run("function run() return vec3.new(1, 2, 3) / vec3.new(1, 1, 1) end", "run", Span<const Value>(), &out)
              .status == Status::RuntimeError);
    // Operators work on values that came in as arguments, and the metatable is not reachable.
    const Value arg = Value::FromVec3(Vec3(1, 1, 1));
    REQUIRE(
        rig.Run("function run(v) return v + v, getmetatable(v) end", "run", Span<const Value>(&arg, 1), &out).IsOk());
    CHECK(out.results[0].AsVec3() == Vec3(2, 2, 2));
    CHECK(out.results[1].Equals(Value::FromBool(false)));
}

TEST_CASE("Bindings: the documentation lists every function and does not depend on the order", "[script][bindings]")
{
    BindingRegistry one;
    BindingRegistry two;
    REQUIRE(one.Add<&Twice>("b", "twice", ThreadRule::Pure, "x", "Doubles."));
    REQUIRE(one.Add<&Not>("a", "negate", ThreadRule::WorldRead, "b", "Negates."));
    REQUIRE(two.Add<&Not>("a", "negate", ThreadRule::WorldRead, "b", "Negates."));
    REQUIRE(two.Add<&Twice>("b", "twice", ThreadRule::Pure, "x", "Doubles."));
    const String<HeapAllocator> text = one.RenderMarkdown();
    CHECK(text.View() == two.RenderMarkdown().View());
    CHECK(text.View().Contains("### `a.negate`"));
    CHECK(text.View().Contains("`(bool b) -> bool`"));
    CHECK(text.View().Contains("Thread rule: **world-read**"));
    CHECK(text.View().Find("### `a.negate`") < text.View().Find("### `b.twice`"));
}

TEST_CASE("Bindings: eight threads install the same registry into their own states", "[script][bindings]")
{
    BindingRegistry registry;
    REQUIRE(RegisterStandardBindings(&registry));
    REQUIRE(registry.Add<&Add3>("test", "add3", ThreadRule::Pure, "a, b, c", "Adds three numbers."));
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    Atomic<u32> good(0);
    Vector<UniquePtr<Thread>> threads;
    for (int t = 0; t < 8; ++t)
    {
        Backend* b = backend.Get();
        BindingRegistry* r = &registry;
        Atomic<u32>* g = &good;
        threads.Push(MakeUnique<Thread>(
            [b, r, g, t]()
            {
                UniquePtr<State> state = b->CreateState(0);
                World world;
                Outcome error;
                UniquePtr<Installation> installed = r->Install(
                    state.Get(), InstallProfile::Partition(EntityCheck{&World::Alive, &world}), &error);
                if (installed.Get() == nullptr || installed->Installed() != r->Count())
                {
                    return;
                }
                if (!state
                         ->Load(
                             "t",
                             "function run(n) local s = 0 for i = 1, 50 do s = s + test.add3(i, n, 0) end return s end")
                         .IsOk())
                {
                    return;
                }
                const Value arg = Value::FromInt(t);
                CallOutput out;
                if (state->Call("run", Span<const Value>(&arg, 1), Budget(), &out).IsOk() && out.results.Size() == 1 &&
                    out.results[0].ToDouble() == 1275.0 + 50.0 * t)
                {
                    g->FetchAdd(1, MemoryOrder::AcqRel);
                }
            }));
    }
    for (usize i = 0; i < threads.Size(); ++i)
    {
        threads[i]->Join();
    }
    CHECK(good.Load(MemoryOrder::Acquire) == 8);
}

namespace
{

struct DynamicCounter
{
    int calls = 0;
};

// A host decides at run time: sums its integer arguments, counts its calls, fails on a negative sum.
Outcome DynamicSum(void* user, Span<const Value> args, Vector<Value>* results)
{
    DynamicCounter* counter = static_cast<DynamicCounter*>(user);
    ++counter->calls;
    i64 sum = 0;
    for (usize i = 0; i < args.Size(); ++i)
    {
        sum += args[i].Kind() == ValueKind::Int ? args[i].AsInt() : 0;
    }
    if (sum < 0)
    {
        return Outcome(Status::RuntimeError, "negative sum");
    }
    results->Push(Value::FromInt(sum));
    return Outcome();
}

}  // namespace

TEST_CASE("Bindings: a function registered at run time is installed and called like a typed one",
          "[script][bindings][dynamic]")
{
    BindingRegistry registry;
    DynamicCounter counter;
    Outcome error;
    REQUIRE(registry.AddDynamic("host", "sum", ThreadRule::WorldRead, "a, b", "Adds two integers.", &DynamicSum,
                                &counter, &error));
    CHECK(registry.Find("host.sum") != nullptr);
    CHECK(registry.Find("host.sum")->arity == 2);
    CHECK(registry.Find("host.sum")->rule == ThreadRule::WorldRead);
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = backend->CreateState(0);
    Outcome install_error;
    UniquePtr<Installation> installed = registry.Install(state.Get(), InstallProfile::Partition(EntityCheck()),
                                                         &install_error);
    REQUIRE(installed.Get() != nullptr);
    REQUIRE(
        state->Load("dynamic", "function run(a, b) return host.sum(a, b) end function wrong() return host.sum(1) end")
            .IsOk());
    CallOutput out;
    Value args[2] = {Value::FromInt(40), Value::FromInt(2)};
    REQUIRE(state->Call("run", Span<const Value>(args, 2), Budget(), &out).IsOk());
    REQUIRE(out.results.Size() == 1);
    CHECK(out.results[0].Equals(Value::FromInt(42)));
    CHECK(counter.calls == 1);
    // The count is checked before the callback runs; a failure of the callback is a script error.
    const Outcome wrong = state->Call("wrong", Span<const Value>(), Budget(), &out);
    CHECK(wrong.status == Status::RuntimeError);
    CHECK(counter.calls == 1);
    Value negative[2] = {Value::FromInt(-5), Value::FromInt(1)};
    const Outcome failed = state->Call("run", Span<const Value>(negative, 2), Budget(), &out);
    CHECK(failed.status == Status::RuntimeError);
    CHECK(counter.calls == 2);
}

TEST_CASE("Bindings: a run-time registration is refused for a null callback, a taken name or a bad rule",
          "[script][bindings][dynamic]")
{
    BindingRegistry registry;
    DynamicCounter counter;
    Outcome error;
    CHECK_FALSE(registry.AddDynamic("host", "none", ThreadRule::Pure, "", "no callback", nullptr, &counter, &error));
    CHECK_FALSE(error.IsOk());
    REQUIRE(registry.AddDynamic("host", "sum", ThreadRule::Pure, "a", "one", &DynamicSum, &counter, &error));
    CHECK_FALSE(registry.AddDynamic("host", "sum", ThreadRule::Pure, "a", "again", &DynamicSum, &counter, &error));
    CHECK_FALSE(
        registry.AddDynamic("host", "unmarked", ThreadRule::Unmarked, "a", "no rule", &DynamicSum, &counter, &error));
    CHECK_FALSE(registry.AddDynamic("math", "sum", ThreadRule::Pure, "a", "a Lua library's name", &DynamicSum, &counter,
                                    &error));
    CHECK_FALSE(
        registry.AddDynamic("host", "Upper", ThreadRule::Pure, "a", "a bad name", &DynamicSum, &counter, &error));
    CHECK(registry.Count() == 1);
}
