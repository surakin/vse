// script_bench: the Phase 27 Day 476 benchmark of the script module. One line per measurement. The numbers are recorded
// in plans/phase-27-scripting/day-476-benchmarks-and-limits.md and docs/guide/scripting.md ("Cost"). `--scene check` is
// the regression check (generous bounds: a 3x slowdown fails it), `--scene stress` the large run (a million scripted
// entities, state checked); both are registered with ctest in an optimized build only.
//
//   script_bench [--scene S]      calls, tick, partitions, events, waits, tasks, memory, check, stress or all (default
//   all, which
//                                 leaves out check and stress)
//                [--entities N]   the entity count of `tick` (default: 10000, 100000 and 1000000)
//                [--ticks N]      timed ticks per measurement (default 5)
#include "containers/vector.h"
#include "log/output.h"
#include "containers/hash.h"
#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "mem/heap_allocator.h"
#include "os/command_line.h"
#include "os/console.h"
#include "os/os.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/component_catalog.h"
#include "script/luajit/luajit_backend.h"
#include "script/script_runtime.h"
#include "script/script_world.h"
#include "storage/builtin_components.h"

using namespace vse;
using namespace vse::script;

namespace
{

struct BenchCounter
{
    i32 count = 0;
    i32 noise = 0;
};

constexpr u32 kBenchCounterId = 2900;
u32 g_ticks = 5;
int g_failures = 0;

const char* const kScripts = R"LUA(
function noop() end
function native_ns(n)
  local s = 0
  for i = 1, n do s = s + num.clamp(i, 0, 1) end
  return s
end
behavior('mixer', {
  on_update = function(self, dt)
    local c = component.get(self.entity, 'bench_counter')
    component.set(self.entity, 'bench_counter', { count = c.count + 1, noise = c.noise + random.int(0, 9) })
    local p = transform.position(self.entity)
    transform.set_position(self.entity, vec3.new(p.x + 1, p.y, p.z + random.int(0, 3)))
  end,
})
behavior('lean', {
  on_update = function(self, dt) self.n = (self.n or 0) + 1 end,
})
behavior('listener', {
  on_create = function(self) self.hits = 0 event.on('ping', function(me, payload) me.hits = me.hits + 1 end) end,
})
behavior('sleeper', {
  on_create = function(self) while true do wait_frames(1) end end,
})
behavior('holder', {
  on_create = function(self) self.data = { 1, 2, 3, 4, 5, 6, 7, 8 } self.name = 'holder' end,
})
behavior('stress', {
  on_update = function(self, dt)
    self.n = (self.n or 0) + 1
    if self.n % 50 == 0 then event.emit('stress_ping', self.n) end
    if self.n == 100 and self.params == nil then end
  end,
  on_create = function(self) event.on('stress_ping', function(me, payload) me.heard = (me.heard or 0) + 1 end) end,
})
behavior('tasker', {
  on_create = function(self)
    while true do
      local parts = wait_task(task.parallel_for(8000000, 'local s = 0 for i = first, last do s = s + i end return s', nil, 4096))
      wait_frames(1)
    end
  end,
})
)LUA";

SharedBytesPtr ScriptText()
{
    return CopyShared(ByteSpan(reinterpret_cast<const byte*>(kScripts), CStrLength(kScripts)));
}

double Ms(u64 ns)
{
    return static_cast<double>(ns) / 1.0e6;
}

struct Rig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    UniquePtr<ScriptRuntime> runtime;

    explicit Rig(u32 partitions, u32 task_states = 0) : catalog(&registry)
    {
        ENGINE_ASSERT(registry
                          .Register<BenchCounter>({.id = kBenchCounterId,
                                                   .name = "bench_counter",
                                                   .fields = {storage::Field<&BenchCounter::count>("count"),
                                                              storage::Field<&BenchCounter::noise>("noise")}})
                          .Ok(),
                      "the bench component did not register");
        ENGINE_ASSERT(storage::RegisterBuiltinComponents(&registry).Ok(), "the builtin components did not register");
        ENGINE_ASSERT(RegisterStandardBindings(&bindings), "the standard bindings did not register");
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.components = &catalog;
        config.random_seed = 7;
        config.task_states = task_states;
        config.on_error = OnScriptError::Retry;
        runtime = MakeUnique<ScriptRuntime>(config);
        runtime->AddScript("bench_scripts", ScriptText());
    }

    void Fill(u32 entities, const char* behavior, bool counter)
    {
        for (u32 i = 0; i < entities; ++i)
        {
            const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
            if (counter)
            {
                world.Add(e, BenchCounter());
            }
            world.Add(e, ScriptComponent(StringView(behavior)));
        }
    }

    u64 Hash()
    {
        u64 sum = 0;
        world.Query<ecs::Read<BenchCounter>, ecs::Read<ecs::LocalTransform>>().ForEach(
            [&](ecs::Entity e, const BenchCounter& c, const ecs::LocalTransform& t)
            {
                u32 bits[2];
                MemCopy(&bits[0], &t.position.x, 4);
                MemCopy(&bits[1], &t.position.z, 4);
                u64 h = e.ToU64() * 0x9E3779B97F4A7C15ull;
                sum += Fnv1a64Builder(h)
                           .AddWord(static_cast<u64>(c.count))
                           .AddWord(static_cast<u64>(c.noise))
                           .AddWord(bits[0])
                           .AddWord(bits[1])
                           .Value();
            });
        return sum;
    }
};

jobs::Scheduler::Config ConfigFor(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

// Runs `ticks` ticks (the first one is a warm-up that makes the instances) and returns the mean of the rest in
// nanoseconds.
u64 TimeTicks(Rig& rig, jobs::Scheduler* scheduler, u32 ticks)
{
    rig.runtime->Tick(&rig.world, 0.016, scheduler);
    const u64 t0 = os::MonotonicTimeNanos();
    for (u32 i = 0; i < ticks; ++i)
    {
        rig.runtime->Tick(&rig.world, 0.016, scheduler);
    }
    return (os::MonotonicTimeNanos() - t0) / (ticks == 0 ? 1 : ticks);
}

// ---- calls: the cost of crossing between the languages

void SceneCalls()
{
    Rig rig(1);
    PartitionLease lease;
    Outcome error;
    if (rig.runtime->Scripts().TryCheckout(0, &lease, &error) != CheckoutStatus::Ok)
    {
        out::PrintError("script_bench: no state\n");
        ++g_failures;
        return;
    }
    Budget budget;
    CallOutput out;
    const u32 calls = 200000;
    u64 t0 = os::MonotonicTimeNanos();
    for (u32 i = 0; i < calls; ++i)
    {
        lease.Call("noop", Span<const Value>(), budget, &out);
    }
    const u64 c_to_lua = os::MonotonicTimeNanos() - t0;
    Value n = Value::FromInt(1000000);
    t0 = os::MonotonicTimeNanos();
    lease.Call("native_ns", Span<const Value>(&n, 1), budget, &out);
    const u64 native_ns = os::MonotonicTimeNanos() - t0;
    out::Print(
        "calls: C to Lua %.0f ns per call; Lua to a pure binding %.0f ns per call (1,000,000 calls of num.clamp)\n",
        static_cast<double>(c_to_lua) / calls, static_cast<double>(native_ns) / 1000000.0);
}

// ---- tick: a mixer behavior (two component reads and writes, a transform write, random) over many entities

void SceneTick(u32 entities, const usize* threads, usize thread_count, u32 partitions)
{
    for (usize k = 0; k < thread_count; ++k)
    {
        jobs::Scheduler scheduler(ConfigFor(threads[k]));
        Rig rig(partitions);
        rig.Fill(entities, "mixer", true);
        const u64 ns = TimeTicks(rig, &scheduler, g_ticks);
        out::Print("tick: %u entities, %u partitions, %zu threads: %.2f ms per tick, %.0f ns per entity\n", entities,
                   partitions, static_cast<size_t>(threads[k]), Ms(ns), static_cast<double>(ns) / entities);
    }
}

void ScenePartitions(u32 entities)
{
    const u32 counts[] = {16, 64, 256, 1024};
    for (u32 p : counts)
    {
        for (usize threads : {usize{1}, usize{16}})
        {
            jobs::Scheduler scheduler(ConfigFor(threads));
            Rig rig(p);
            rig.Fill(entities, "mixer", true);
            const u64 ns = TimeTicks(rig, &scheduler, g_ticks);
            out::Print("partitions: %u entities, P=%u, %zu threads: %.2f ms per tick\n", entities, p,
                       static_cast<size_t>(threads), Ms(ns));
        }
    }
}

// ---- events: one native event delivered to every listener

void SceneEvents(u32 entities)
{
    jobs::Scheduler scheduler(ConfigFor(16));
    Rig rig(64);
    rig.Fill(entities, "listener", false);
    rig.runtime->Tick(&rig.world, 0.016, &scheduler);
    const u32 events = 10;
    u64 total = 0;
    for (u32 t = 0; t < g_ticks; ++t)
    {
        for (u32 e = 0; e < events; ++e)
        {
            rig.runtime->Events().Emit(StringView("ping"), Value::FromInt(e));
        }
        const u64 t0 = os::MonotonicTimeNanos();
        rig.runtime->Tick(&rig.world, 0.016, &scheduler);
        total += os::MonotonicTimeNanos() - t0;
    }
    const double per_tick = static_cast<double>(total) / g_ticks;
    out::Print("events: %u listeners, %u events per tick: %.2f ms per tick, %.2f million handler calls per second\n",
               entities, events, Ms(static_cast<u64>(per_tick)),
               static_cast<double>(entities) * events / per_tick * 1000.0);
}

// ---- waits: coroutines that wake every tick

void SceneWaits(u32 entities)
{
    jobs::Scheduler scheduler(ConfigFor(16));
    Rig rig(64);
    rig.Fill(entities, "sleeper", false);
    const u64 ns = TimeTicks(rig, &scheduler, g_ticks);
    out::Print("waits: %u coroutines waking every tick: %.2f ms per tick, %.0f ns per wake\n", entities, Ms(ns),
               static_cast<double>(ns) / entities);
}

// ---- tasks: parallel_for over 8 million indices

void SceneTasks()
{
    for (usize threads : {usize{1}, usize{2}, usize{4}, usize{8}, usize{16}})
    {
        jobs::Scheduler scheduler(ConfigFor(threads));
        Rig rig(16, static_cast<u32>(threads));
        rig.Fill(1, "tasker", false);
        const u64 ns = TimeTicks(rig, &scheduler, 3);
        out::Print("tasks: parallel_for over 8,000,000 indices, %zu threads: %.2f ms per round\n",
                   static_cast<size_t>(threads), Ms(ns));
    }
}

// ---- memory: bytes per state, per instance, per coroutine

usize StateBytes(Rig& rig)
{
    usize total = 0;
    for (u32 p = 0; p < rig.runtime->Scripts().PartitionCount(); ++p)
    {
        PartitionLease lease;
        Outcome error;
        if (rig.runtime->Scripts().TryCheckout(p, &lease, &error) == CheckoutStatus::Ok && lease.GetState() != nullptr)
        {
            total += lease.GetState()->MemoryUsed();
        }
    }
    return total;
}

void SceneMemory()
{
    {
        Rig rig(1);
        const usize empty = StateBytes(rig);
        out::Print("memory: one state with the behavior host and the bench script: %zu bytes\n",
                   static_cast<size_t>(empty));
    }
    const u32 n = 20000;
    Rig base(4);
    base.Fill(1, "holder", false);
    jobs::Scheduler scheduler(ConfigFor(1));
    base.runtime->Tick(&base.world, 0.016, &scheduler);
    const usize before = StateBytes(base);
    Rig many(4);
    many.Fill(n, "holder", false);
    many.runtime->Tick(&many.world, 0.016, &scheduler);
    const usize after = StateBytes(many);
    out::Print("memory: %u instances of a behavior holding a table of eight numbers: %.0f bytes per instance\n", n,
               static_cast<double>(after - before) / (n - 1));
    Rig sleepers(4);
    sleepers.Fill(1, "sleeper", false);
    sleepers.runtime->Tick(&sleepers.world, 0.016, &scheduler);
    const usize s1 = StateBytes(sleepers);
    Rig sleepers2(4);
    sleepers2.Fill(n, "sleeper", false);
    sleepers2.runtime->Tick(&sleepers2.world, 0.016, &scheduler);
    const usize s2 = StateBytes(sleepers2);
    out::Print("memory: %u waiting coroutines (an instance each): %.0f bytes per instance and coroutine\n", n,
               static_cast<double>(s2 - s1) / (n - 1));
}

// ---- check: generous bounds; a 3x slowdown fails

bool Bound(const char* what, double measured, double limit)
{
    const bool ok = measured <= limit;
    out::Print("check: %s %.3f (limit %.3f) %s\n", what, measured, limit, ok ? "ok" : "FAILED");
    return ok;
}

void SceneCheck()
{
    bool ok = true;
    {
        jobs::Scheduler scheduler(ConfigFor(1));
        Rig rig(64);
        rig.Fill(20000, "mixer", true);
        const double ns_per_entity = static_cast<double>(TimeTicks(rig, &scheduler, 3)) / 20000.0;
        ok &= Bound("mixer tick, 1 thread, ns per entity", ns_per_entity, 24000.0);
    }
    {
        Rig rig(1);
        PartitionLease lease;
        Outcome error;
        rig.runtime->Scripts().TryCheckout(0, &lease, &error);
        Budget budget;
        CallOutput out;
        const u64 t0 = os::MonotonicTimeNanos();
        for (u32 i = 0; i < 100000; ++i)
        {
            lease.Call("noop", Span<const Value>(), budget, &out);
        }
        ok &= Bound("C to Lua call, ns", static_cast<double>(os::MonotonicTimeNanos() - t0) / 100000.0, 1400.0);
    }
    if (!ok)
    {
        ++g_failures;
    }
}

// ---- stress: a million scripted entities, with churn, events and tasks; the state is checked

struct StressResult
{
    u64 hash = 0;
    u64 updates = 0;
    u64 errors = 0;
};

StressResult RunStress(usize threads, u32 entities, u32 ticks, u32 partitions)
{
    jobs::Scheduler scheduler(ConfigFor(threads));
    Rig rig(partitions);
    rig.Fill(entities, "mixer", true);
    // Some entities listen and emit, and one plans with tasks.
    rig.Fill(entities / 100 + 1, "stress", false);
    rig.Fill(1, "tasker", false);
    Vector<ecs::Entity> churn;
    for (u32 t = 0; t < ticks; ++t)
    {
        rig.runtime->Tick(&rig.world, 0.016, &scheduler);
        if (t % 10 == 9)
        {
            // Churn: destroy a few entities and make as many new ones, at positions fixed by the tick (no dependence on
            // threads).
            for (u32 i = 0; i < entities / 1000; ++i)
            {
                const ecs::Entity e = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
                rig.world.Add(e, BenchCounter());
                rig.world.Add(e, ScriptComponent(StringView("mixer")));
                churn.Push(e);
            }
            if (churn.Size() > entities / 500)
            {
                for (u32 i = 0; i < entities / 1000; ++i)
                {
                    rig.world.Destroy(churn[i]);
                }
                churn.RemoveRange(0, entities / 1000);
            }
        }
    }
    StressResult r;
    r.hash = rig.Hash();
    r.updates = rig.runtime->Stats().updates;
    r.errors = rig.runtime->Stats().errors;
    return r;
}

void SceneStress()
{
    // Small scale first: one thread and sixteen compare by hash.
    const StressResult a = RunStress(1, 20000, 200, 64);
    const StressResult b = RunStress(16, 20000, 200, 64);
    out::Print("stress: 20,000 entities, 200 ticks: hash %016llx (1 thread), %016llx (16 threads), errors %llu %llu\n",
               static_cast<unsigned long long>(a.hash), static_cast<unsigned long long>(b.hash),
               static_cast<unsigned long long>(a.errors), static_cast<unsigned long long>(b.errors));
    if (a.hash != b.hash || a.errors != 0 || b.errors != 0 || a.updates != b.updates)
    {
        out::PrintError("script_bench: the small stress run differs between thread counts\n");
        ++g_failures;
        return;
    }
    // The large run: a million entities. Every instance ran `ticks` updates (plus the churn), no error.
    const u32 big_ticks = 60;
    const u64 t0 = os::MonotonicTimeNanos();
    const StressResult big = RunStress(16, 1000000, big_ticks, 256);
    out::Print("stress: 1,000,000 entities, %u ticks on 16 threads: %.1f s, updates %llu, errors %llu\n", big_ticks,
               static_cast<double>(os::MonotonicTimeNanos() - t0) * 1.0e-9,
               static_cast<unsigned long long>(big.updates), static_cast<unsigned long long>(big.errors));
    if (big.errors != 0 || big.updates < static_cast<u64>(1000000) * (big_ticks - 1))
    {
        out::PrintError("script_bench: the large stress run failed its checks\n");
        ++g_failures;
    }
}

}  // namespace

int main(int argc, char** argv)
{
    static const os::CommandLineOption kCliOptions[] = {
        {"scene", true}, {"entities", true}, {"ticks", true}, debug::kCrashHandlerOption, debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), argc, argv,
                                              Span<const os::CommandLineOption>(kCliOptions, 5));
    if (!line.Ok() || line.ArgCount() != 0)
    {
        out::PrintError(
            "usage: %s [--scene calls|tick|partitions|events|waits|tasks|memory|check|stress|all] [--entities N] [--ticks N]\n",
            line.ProgramName().Data());
        return 2;
    }
    const char* scene = "all";
    StringView value;
    u32 entities = 0;
    if (line.TryGetOption("scene", value))
    {
        scene = value.Data();
    }
    if (line.TryGetOption("entities", value))
    {
        entities = static_cast<u32>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("ticks", value))
    {
        g_ticks = static_cast<u32>(CStrToLong(value.Data()));
    }
    const bool all = CStrCompare(scene, "all") == 0;
    if (all || CStrCompare(scene, "calls") == 0)
    {
        SceneCalls();
    }
    if (all || CStrCompare(scene, "tick") == 0)
    {
        const usize threads[] = {1, 2, 4, 8, 16};
        if (entities != 0)
        {
            SceneTick(entities, threads, 5, 64);
        }
        else
        {
            for (u32 n : {10000u, 100000u, 1000000u})
            {
                SceneTick(n, threads, 5, 64);
            }
        }
    }
    if (all || CStrCompare(scene, "partitions") == 0)
    {
        ScenePartitions(entities != 0 ? entities : 100000);
    }
    if (all || CStrCompare(scene, "events") == 0)
    {
        SceneEvents(entities != 0 ? entities : 100000);
    }
    if (all || CStrCompare(scene, "waits") == 0)
    {
        SceneWaits(entities != 0 ? entities : 100000);
    }
    if (all || CStrCompare(scene, "tasks") == 0)
    {
        SceneTasks();
    }
    if (all || CStrCompare(scene, "memory") == 0)
    {
        SceneMemory();
    }
    if (CStrCompare(scene, "check") == 0)
    {
        SceneCheck();
    }
    if (CStrCompare(scene, "stress") == 0)
    {
        SceneStress();
    }
    return g_failures == 0 ? 0 : 1;
}
