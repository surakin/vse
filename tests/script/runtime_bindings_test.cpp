// The math, random, log and time bindings (Phase 27 Day 453): the value types and their functions, the per-entity
// random stream (the same for any thread count), the log buffer with its rate limit and flush order, and the clocks.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "lang/scalar.h"
#include "log/log.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/log_buffer.h"
#include "script/luajit/luajit_backend.h"
#include "script/partition_context.h"
#include "script/script_world.h"
#include "sync/atomic.h"
#include "sync/thread.h"

using namespace vse;
using namespace vse::script;

namespace
{

SharedBytesPtr RuntimeScriptText(const char* text)
{
    return CopyShared(ByteSpan(reinterpret_cast<const byte*>(text), CStrLength(text)));
}

// A state with the standard bindings, driven from the test's thread.
struct RuntimeMainRig
{
    BindingRegistry registry;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = backend->CreateState(0);
    UniquePtr<Installation> installed;

    RuntimeMainRig()
    {
        REQUIRE(RegisterStandardBindings(&registry));
        Outcome error;
        installed = registry.Install(state.Get(), InstallProfile::Main(ContextEntityCheck()), &error);
        REQUIRE(installed.Get() != nullptr);
    }

    // Runs `body` as the body of a function and returns its results.
    Vector<Value> Run(const char* body)
    {
        String<HeapAllocator> source(HeapAllocator::Default());
        source.Append("function run() ");
        source.Append(StringView(body));
        source.Append(" end");
        REQUIRE(state->Load("test", source.View()).IsOk());
        CallOutput out;
        const Outcome o = state->Call("run", Span<const Value>(), Budget(), &out);
        REQUIRE(o.IsOk());
        return Move(out.results);
    }
};

bool RuntimeNear(f64 a, f64 b)
{
    return Abs(a - b) < 1.0e-4;
}

// Lines the LogScript channel carries, in order.
struct RuntimeCaptured
{
    Vector<String<HeapAllocator>> lines;
    Vector<LogVerbosity> levels;
};

bool RuntimeObserve(void* user, const char* channel, LogVerbosity level, StringView message)
{
    if (CStrCompare(channel, "LogScript") != 0)
    {
        return false;
    }
    RuntimeCaptured* captured = static_cast<RuntimeCaptured*>(user);
    String<HeapAllocator> line(HeapAllocator::Default());
    line.Append(message);
    captured->lines.Push(Move(line));
    captured->levels.Push(level);
    return true;
}

struct RuntimeCapture
{
    RuntimeCaptured captured;
    RuntimeCapture()
    {
        LogScript.SetVerbosity(LogVerbosity::Debug);
        SetLogObserver(&RuntimeObserve, &captured);
    }
    ~RuntimeCapture()
    {
        SetLogObserver(nullptr, nullptr);
        LogScript.SetVerbosity(LogVerbosity::Info);
    }
};

}  // namespace

TEST_CASE("Math bindings: vec2, vec4 and the num helpers", "[script][bindings][runtime]")
{
    RuntimeMainRig rig;
    Vector<Value> r = rig.Run("return vec2.length(vec2.new(3, 4)), vec2.dot(vec2.new(1, 2), vec2.new(3, 4)), "
                              "vec4.length(vec4.new(1, 2, 2, 4)), vec2.normalize(vec2.new(0, 0)), "
                              "vec4.lerp(vec4.new(0, 0, 0, 0), vec4.new(2, 4, 6, 8), 0.5)");
    REQUIRE(r.Size() == 5);
    CHECK(RuntimeNear(r[0].ToDouble(), 5.0));
    CHECK(RuntimeNear(r[1].ToDouble(), 11.0));
    CHECK(RuntimeNear(r[2].ToDouble(), 5.0));
    CHECK(r[3].AsVec2().x == 0.0f);
    CHECK(r[3].AsVec2().y == 0.0f);
    CHECK(RuntimeNear(r[4].AsVec4().w, 4.0));

    r = rig.Run("return num.sign(-3), num.sign(0), num.sign(2), num.smoothstep(0, 1, 0.5), num.smoothstep(0, 1, -1), "
                "num.smoothstep(2, 2, 3), num.approach(0, 10, 3), num.approach(9, 10, 3), num.approach(5, 0, -2)");
    REQUIRE(r.Size() == 9);
    CHECK(r[0].ToDouble() == -1.0);
    CHECK(r[1].ToDouble() == 0.0);
    CHECK(r[2].ToDouble() == 1.0);
    CHECK(RuntimeNear(r[3].ToDouble(), 0.5));
    CHECK(r[4].ToDouble() == 0.0);
    CHECK(r[5].ToDouble() == 1.0);
    CHECK(r[6].ToDouble() == 3.0);
    CHECK(r[7].ToDouble() == 10.0);
    CHECK(r[8].ToDouble() == 3.0);
}

TEST_CASE("Math bindings: arithmetic on vectors and rotations", "[script][bindings][runtime]")
{
    RuntimeMainRig rig;
    const Vector<Value> r = rig.Run("local a = vec3.new(1, 2, 3) local b = vec3.new(4, 5, 6) "
                                    "return a + b, b - a, a * 2, vec3.cross(a, b), vec3.dot(a, b)");
    REQUIRE(r.Size() == 5);
    CHECK(r[0].AsVec3().z == 9.0f);
    CHECK(r[1].AsVec3().x == 3.0f);
    CHECK(r[2].AsVec3().y == 4.0f);
    CHECK(r[3].AsVec3().x == -3.0f);
    CHECK(r[4].ToDouble() == 32.0);
}

TEST_CASE("Math bindings: quaternions", "[script][bindings][runtime]")
{
    RuntimeMainRig rig;
    const Vector<Value> r = rig.Run(
        "local q = quat.from_axis_angle(vec3.new(0, 0, 1), 1.5707963) "
        "return quat.rotate(q, vec3.new(1, 0, 0)), quat.slerp(quat.identity(), q, 0.5), "
        "quat.rotate(quat.from_euler(0, 0, 1.5707963), vec3.new(1, 0, 0)), "
        "quat.rotate(quat.mul(quat.conjugate(q), q), vec3.new(1, 2, 3)), quat.normalize(q)");
    REQUIRE(r.Size() == 5);
    CHECK(RuntimeNear(r[0].AsVec3().x, 0.0));
    CHECK(RuntimeNear(r[0].AsVec3().y, 1.0));
    const Quaternion half = r[1].AsQuat();
    CHECK(RuntimeNear(half.z, Sin(0.5 * 0.5 * 1.5707963)));
    CHECK(RuntimeNear(half.w, Cos(0.5 * 0.5 * 1.5707963)));
    CHECK(RuntimeNear(r[2].AsVec3().y, 1.0));
    CHECK(RuntimeNear(r[3].AsVec3().x, 1.0));
    CHECK(RuntimeNear(r[3].AsVec3().z, 3.0));
}

TEST_CASE("Math bindings: mat4 builds, multiplies and looks at", "[script][bindings][runtime]")
{
    RuntimeMainRig rig;
    Vector<Value> r = rig.Run(
        "local m = mat4.from_trs(vec3.new(10, 0, 0), quat.from_axis_angle(vec3.new(0, 0, 1), 1.5707963), "
        "vec3.new(2, 2, 2)) "
        "local id = mat4.mul(mat4.identity(), m) "
        "local view = mat4.look_at(vec3.new(0, 0, 5), vec3.new(0, 0, 0), vec3.new(0, 1, 0)) "
        "return mat4.transform_point(m, vec3.new(1, 0, 0)), mat4.transform_direction(m, vec3.new(1, 0, 0)), "
        "mat4.transform_point(id, vec3.new(1, 0, 0)), mat4.transform_point(view, vec3.new(0, 0, 0)), "
        "mat4.transform_point(view, vec3.new(0, 0, 5)), mat4.transform_point(mat4.transpose(mat4.identity()), "
        "vec3.new(1, 2, 3)), mat4.look_at(vec3.new(1, 1, 1), vec3.new(1, 1, 1), vec3.new(0, 1, 0))");
    REQUIRE(r.Size() == 7);
    CHECK(RuntimeNear(r[0].AsVec3().x, 10.0));  // scaled to (2,0,0), turned to (0,2,0), moved to (10,2,0)
    CHECK(RuntimeNear(r[0].AsVec3().y, 2.0));
    CHECK(RuntimeNear(r[1].AsVec3().y, 2.0));
    CHECK(RuntimeNear(r[1].AsVec3().x, 0.0));
    CHECK(RuntimeNear(r[2].AsVec3().x, 10.0));
    CHECK(RuntimeNear(r[3].AsVec3().z, -5.0));  // the target is 5 in front of the camera, down -Z
    CHECK(RuntimeNear(r[4].AsVec3().z, 0.0));   // the eye is the origin of the view
    CHECK(RuntimeNear(r[5].AsVec3().z, 3.0));
    CHECK(r[6].Kind() == ValueKind::Array);
    CHECK(r[6].Size() == 16);
}

TEST_CASE("Math bindings: a matrix argument that is not 16 numbers is a script error", "[script][bindings][runtime]")
{
    RuntimeMainRig rig;
    REQUIRE(
        rig.state->Load("test", "function run() return mat4.transform_point({1, 2, 3}, vec3.new(0, 0, 0)) end").IsOk());
    CallOutput out;
    const Outcome o = rig.state->Call("run", Span<const Value>(), Budget(), &out);
    CHECK_FALSE(o.IsOk());
}

TEST_CASE("Random: SplitMix64 gives its published numbers and floats stay below 1", "[script][bindings][runtime]")
{
    u64 state = 0;
    CHECK(AdvanceRandom(&state) == 0xE220A8397B1DCDAFull);
    CHECK(AdvanceRandom(&state) == 0x6E789E6AA1B965F4ull);
    CHECK(AdvanceRandom(&state) == 0x06C45D188009454Full);
    // The seed mixes all three inputs.
    CHECK(RandomSeedFor(1, 2, 3) != RandomSeedFor(2, 2, 3));
    CHECK(RandomSeedFor(1, 2, 3) != RandomSeedFor(1, 3, 3));
    CHECK(RandomSeedFor(1, 2, 3) != RandomSeedFor(1, 2, 4));
    CHECK(RandomSeedFor(1, 2, 3) == RandomSeedFor(1, 2, 3));
}

TEST_CASE("Random: the stream depends on seed, entity and tick, and each number is in range",
          "[script][bindings][runtime]")
{
    RuntimeMainRig rig;
    auto roll = [&rig](u32 seed, u64 entity, u64 tick)
    {
        PartitionContext context;
        context.world_seed = seed;
        context.current_entity_key = entity;
        context.tick = tick;
        SeedPartitionRandom(&context);
        ScopedPartitionContext scope(&context);
        return rig.Run("return random.float(), random.int(1, 6), random.vec3_unit(), random.float()");
    };
    const Vector<Value> a = roll(1, 42, 7);
    const Vector<Value> again = roll(1, 42, 7);
    REQUIRE(a.Size() == 4);
    for (usize i = 0; i < a.Size(); ++i)
    {
        CHECK(a[i].Equals(again[i]));
    }
    const Vector<Value> other_entity = roll(1, 43, 7);
    CHECK_FALSE(a[0].Equals(other_entity[0]));
    CHECK_FALSE(roll(2, 42, 7)[0].Equals(a[0]));
    CHECK_FALSE(roll(1, 42, 8)[0].Equals(a[0]));
    CHECK(a[0].ToDouble() >= 0.0);
    CHECK(a[0].ToDouble() < 1.0);
    CHECK_FALSE(a[0].Equals(a[3]));  // the stream advances

    u32 seen[7] = {};
    for (u64 e = 0; e < 600; ++e)
    {
        const Vector<Value> r = roll(1, e, 0);
        const i64 d = r[1].AsInt();
        REQUIRE(d >= 1);
        REQUIRE(d <= 6);
        ++seen[d];
        CHECK(RuntimeNear(r[2].AsVec3().Length(), 1.0));
    }
    for (u32 face = 1; face <= 6; ++face)
    {
        CHECK(seen[face] > 40);  // roughly 100 each
    }
    CHECK(rig.Run("return random.int(5, 5), random.int(9, 9)")[0].AsInt() == 5);
    const Vector<Value> swapped = rig.Run("return random.int(3, 1)");
    CHECK(swapped[0].AsInt() >= 1);
    CHECK(swapped[0].AsInt() <= 3);
}

TEST_CASE("Random: 64 partitions on 8 threads draw what one thread draws", "[script][bindings][runtime][world]")
{
    BindingRegistry registry;
    REQUIRE(RegisterStandardBindings(&registry));
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ScriptWorldConfig config;
    config.backend = backend.Get();
    config.bindings = &registry;
    config.profile = InstallProfile::Partition(ContextEntityCheck());
    config.partitions = 64;
    ScriptWorld world(config);
    world.AddScript(
        "rolls", RuntimeScriptText("function roll(n) return random.float(), random.int(1, 1000), random.vec3_unit(), "
                                   "time.now(), time.dt(), n end"));

    auto draw = [&world](u32 partition, Vector<Value>* out)
    {
        PartitionLease lease;
        REQUIRE(world.Checkout(partition, &lease, nullptr) == CheckoutStatus::Ok);
        PartitionContext context;
        context.world_seed = 9;
        context.tick = 12;
        context.dt = 0.5;
        context.sim_time = 6.0;
        context.current_entity_key = 5000 + partition;
        SeedPartitionRandom(&context);
        ScopedPartitionContext scope(&context);
        const Value arg = Value::FromInt(partition);
        CallOutput call;
        REQUIRE(lease.Call("roll", Span<const Value>(&arg, 1), Budget(), &call).IsOk());
        *out = Move(call.results);
    };

    Vector<Vector<Value>> serial;
    for (u32 p = 0; p < 64; ++p)
    {
        serial.Push(Vector<Value>());
        draw(p, &serial[p]);
    }
    Vector<Vector<Value>> parallel;
    for (u32 p = 0; p < 64; ++p)
    {
        parallel.Push(Vector<Value>());
    }
    Vector<UniquePtr<Thread>> threads;
    for (u32 t = 0; t < 8; ++t)
    {
        threads.Push(MakeUnique<Thread>(
            [&draw, &parallel, t]()
            {
                for (u32 p = t; p < 64; p += 8)
                {
                    draw(p, &parallel[p]);
                }
            }));
    }
    for (usize i = 0; i < threads.Size(); ++i)
    {
        threads[i]->Join();
    }
    for (u32 p = 0; p < 64; ++p)
    {
        REQUIRE(serial[p].Size() == 6);
        REQUIRE(parallel[p].Size() == 6);
        for (usize i = 0; i < 6; ++i)
        {
            CHECK(serial[p][i].Equals(parallel[p][i]));
        }
        CHECK(serial[p][3].ToDouble() == 6.0);
        CHECK(serial[p][4].ToDouble() == 0.5);
    }
}

TEST_CASE("Log: messages carry the script and line, and buffered ones wait for the flush",
          "[script][bindings][runtime][log]")
{
    RuntimeMainRig rig;
    RuntimeCapture capture;
    LogBuffer buffer(64);
    PartitionContext context;
    context.log = &buffer;
    {
        ScopedPartitionContext scope(&context);
        REQUIRE(rig.state
                    ->Load("probe",
                           "function run()\n  log.debug('d')\n  log.info('i')\n  log.warn('w')\n  log.error('e')\nend")
                    .IsOk());
        CallOutput out;
        REQUIRE(rig.state->Call("run", Span<const Value>(), Budget(), &out).IsOk());
    }
    CHECK(capture.captured.lines.Size() == 0);  // buffered, not written
    CHECK(buffer.Pending() == 4);
    CHECK(buffer.Flush() == 4);
    REQUIRE(capture.captured.lines.Size() == 4);
    CHECK(capture.captured.levels[0] == LogVerbosity::Debug);
    CHECK(capture.captured.levels[1] == LogVerbosity::Info);
    CHECK(capture.captured.levels[2] == LogVerbosity::Warn);
    CHECK(capture.captured.levels[3] == LogVerbosity::Error);
    CHECK(capture.captured.lines[1].View() == StringView("[probe:3] i"));
    CHECK(capture.captured.lines[3].View() == StringView("[probe:5] e"));
    CHECK(buffer.Pending() == 0);
}

TEST_CASE("Log: a level the channel drops is not buffered and does not use up the limit",
          "[script][bindings][runtime][log]")
{
    RuntimeMainRig rig;
    RuntimeCapture capture;
    LogScript.SetVerbosity(LogVerbosity::Info);
    LogBuffer buffer(2);
    PartitionContext context;
    context.log = &buffer;
    {
        ScopedPartitionContext scope(&context);
        REQUIRE(
            rig.state->Load("dbg", "function run() for i = 1, 50 do log.debug('x') end log.info('a') log.info('b') end")
                .IsOk());
        CallOutput out;
        REQUIRE(rig.state->Call("run", Span<const Value>(), Budget(), &out).IsOk());
    }
    CHECK(buffer.Pending() == 2);
    CHECK(buffer.DroppedSinceFlush() == 0);
}

TEST_CASE("Log: with no buffer the message goes straight to the log", "[script][bindings][runtime][log]")
{
    RuntimeMainRig rig;
    RuntimeCapture capture;
    const Vector<Value> r = rig.Run("log.info('now') return 1");
    REQUIRE(r.Size() == 1);
    REQUIRE(capture.captured.lines.Size() == 1);
    CHECK(capture.captured.lines[0].View() == StringView("[test:1] now"));
}

TEST_CASE("Log: a script that writes more than the limit loses the rest and one warning counts them",
          "[script][bindings][runtime][log]")
{
    RuntimeMainRig rig;
    RuntimeCapture capture;
    LogBuffer buffer(5);
    PartitionContext context;
    context.log = &buffer;
    {
        ScopedPartitionContext scope(&context);
        REQUIRE(rig.state->Load("noisy", "function run() for i = 1, 100 do log.info('spam ' .. i) end end").IsOk());
        REQUIRE(rig.state->Load("quiet", "function quiet() log.info('hello') end").IsOk());
        CallOutput out;
        REQUIRE(rig.state->Call("run", Span<const Value>(), Budget(), &out).IsOk());
        REQUIRE(rig.state->Call("quiet", Span<const Value>(), Budget(), &out).IsOk());
    }
    CHECK(buffer.Pending() == 6);  // five spam lines and the quiet script's own, which has its own limit
    CHECK(buffer.DroppedSinceFlush() == 95);
    CHECK(buffer.Flush() == 7);
    REQUIRE(capture.captured.lines.Size() == 7);
    CHECK(capture.captured.levels[6] == LogVerbosity::Warn);
    CHECK(capture.captured.lines[6].View() == StringView("[noisy] 95 more messages dropped (limit 5 per tick)"));
    // The limits start again after a flush.
    buffer.Add(ScriptLogLevel::Info, "noisy", 1, "again");
    CHECK(buffer.Pending() == 1);
    CHECK(buffer.DroppedSinceFlush() == 0);
}

TEST_CASE("Log: a limit of zero is no limit, and long text is cut", "[script][bindings][runtime][log]")
{
    RuntimeCapture capture;
    LogBuffer buffer(0);
    for (u32 i = 0; i < 500; ++i)
    {
        CHECK(buffer.Add(ScriptLogLevel::Info, "s", 1, "x"));
    }
    String<HeapAllocator> big(HeapAllocator::Default());
    for (u32 i = 0; i < 1000; ++i)
    {
        big.Append('y');
    }
    buffer.Add(ScriptLogLevel::Info, "s", 1, big.View());
    CHECK(buffer.Flush() == 501);
    CHECK(capture.captured.lines[500].Length() <= 8 + kMaxScriptLogText);
}

TEST_CASE("Log: 64 partitions flush in partition order whatever order they ran in",
          "[script][bindings][runtime][log][world]")
{
    BindingRegistry registry;
    REQUIRE(RegisterStandardBindings(&registry));
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ScriptWorldConfig config;
    config.backend = backend.Get();
    config.bindings = &registry;
    config.profile = InstallProfile::Partition(ContextEntityCheck());
    config.partitions = 64;
    ScriptWorld world(config);
    world.AddScript("talker", RuntimeScriptText("function say(n) log.info('part ' .. n) log.warn('again ' .. n) end"));

    Vector<UniquePtr<LogBuffer>> buffers;
    for (u32 p = 0; p < 64; ++p)
    {
        buffers.Push(MakeUnique<LogBuffer>(10));
    }
    Vector<UniquePtr<Thread>> threads;
    for (u32 t = 0; t < 8; ++t)
    {
        threads.Push(MakeUnique<Thread>(
            [&world, &buffers, t]()
            {
                // The threads take their partitions from the high end down, so completion order differs from partition
                // order.
                for (u32 k = 0; k < 8; ++k)
                {
                    const u32 p = 63 - (t + k * 8);
                    PartitionLease lease;
                    REQUIRE(world.Checkout(p, &lease, nullptr) == CheckoutStatus::Ok);
                    PartitionContext context;
                    context.log = buffers[p].Get();
                    ScopedPartitionContext scope(&context);
                    const Value arg = Value::FromInt(p);
                    CallOutput out;
                    REQUIRE(lease.Call("say", Span<const Value>(&arg, 1), Budget(), &out).IsOk());
                }
            }));
    }
    for (usize i = 0; i < threads.Size(); ++i)
    {
        threads[i]->Join();
    }
    RuntimeCapture capture;
    Vector<LogBuffer*> order;
    for (u32 p = 0; p < 64; ++p)
    {
        order.Push(buffers[p].Get());
    }
    CHECK(FlushLogBuffers(Span<LogBuffer* const>(order.Data(), order.Size())) == 128);
    REQUIRE(capture.captured.lines.Size() == 128);
    for (u32 p = 0; p < 64; ++p)
    {
        char expected[64];
        FormatTo(expected, sizeof(expected), "part %u", static_cast<unsigned>(p));
        const String<HeapAllocator>& line = capture.captured.lines[p * 2];
        CHECK(line.View().Length() >= CStrLength(expected));
        CHECK(StringView(line.CStr() + line.Length() - CStrLength(expected), CStrLength(expected)) ==
              StringView(expected, CStrLength(expected)));
    }
}

TEST_CASE("Time: the tick's clock, and time.real refused while a replay records", "[script][bindings][runtime][time]")
{
    RuntimeMainRig rig;
    // Outside a job: zeros.
    Vector<Value> r = rig.Run("return time.dt(), time.now(), time.frame(), time.tick()");
    REQUIRE(r.Size() == 4);
    CHECK(r[0].ToDouble() == 0.0);
    CHECK(r[2].AsInt() == 0);

    PartitionContext context;
    context.dt = 1.0 / 60.0;
    context.sim_time = 2.5;
    context.frame = 77;
    context.tick = 150;
    {
        ScopedPartitionContext scope(&context);
        r = rig.Run("return time.dt(), time.now(), time.frame(), time.tick(), time.real()");
    }
    REQUIRE(r.Size() == 5);
    CHECK(RuntimeNear(r[0].ToDouble(), 1.0 / 60.0));
    CHECK(r[1].ToDouble() == 2.5);
    CHECK(r[2].AsInt() == 77);
    CHECK(r[3].AsInt() == 150);
    REQUIRE(r[4].Kind() == ValueKind::Float);
    CHECK(r[4].ToDouble() > 1.0e9);  // seconds since 1970

    context.replay_recording = true;
    {
        ScopedPartitionContext scope(&context);
        REQUIRE(rig.state->Load("test", "function bad() return time.real() end function ok() return time.now() end")
                    .IsOk());
        CallOutput out;
        const Outcome refused = rig.state->Call("bad", Span<const Value>(), Budget(), &out);
        CHECK_FALSE(refused.IsOk());
        CHECK(refused.message.View().Contains("time.real"));
        CHECK(out.results.Size() == 0);
        REQUIRE(rig.state->Call("ok", Span<const Value>(), Budget(), &out).IsOk());
        CHECK(out.results[0].ToDouble() == 2.5);
    }
}

TEST_CASE("Bindings: the runtime bindings are all marked pure and documented", "[script][bindings][runtime]")
{
    BindingRegistry registry;
    REQUIRE(RegisterStandardBindings(&registry));
    const char* names[] = {"random.float",  "random.int", "random.vec3_unit", "log.debug",       "log.info",
                           "log.warn",      "log.error",  "time.dt",          "time.now",        "time.real",
                           "time.frame",    "time.tick",  "num.sign",         "num.smoothstep",  "num.approach",
                           "vec2.new",      "vec4.new",   "quat.slerp",       "quat.from_euler", "mat4.look_at",
                           "mat4.from_trs", "mat4.mul"};
    for (const char* name : names)
    {
        const BindingInfo* info = registry.Find(name);
        REQUIRE(info != nullptr);
        CHECK(info->rule == ThreadRule::Pure);
        CHECK(info->doc.Length() > 10);
    }
}
