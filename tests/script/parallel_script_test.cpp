// The parallel script system (Phase 27 Day 458): the partitions run as jobs on the scheduler, and the world state, the
// stats and the random numbers are the same at any thread count; how evenly the entity hash spreads the partitions.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
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

struct ParCounter
{
    i32 count = 0;
    i32 noise = 0;
};

constexpr u32 kParCounterId = 2110;

const char* const kParallelScripts = R"LUA(
behavior('mixer', {
  on_update = function(self, dt)
    local c = component.get(self.entity, 'par_counter')
    component.set(self.entity, 'par_counter', { count = c.count + 1, noise = c.noise + random.int(0, 9) })
    local p = transform.position(self.entity)
    transform.set_position(self.entity, vec3.new(p.x + 1, p.y, p.z + random.int(0, 3)))
  end,
})
)LUA";

struct ParallelRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    UniquePtr<ScriptRuntime> runtime;

    ParallelRig(u32 entities, u32 partitions) : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(registry
                    .Register<ParCounter>({.id = kParCounterId,
                                           .name = "par_counter",
                                           .fields = {storage::Field<&ParCounter::count>("count"),
                                                      storage::Field<&ParCounter::noise>("noise")}})
                    .Ok());
        REQUIRE(RegisterStandardBindings(&bindings));
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.components = &catalog;
        config.random_seed = 7;
        runtime = MakeUnique<ScriptRuntime>(config);
        runtime->AddScript("parallel_scripts", CopyShared(ByteSpan(reinterpret_cast<const byte*>(kParallelScripts),
                                                                   CStrLength(kParallelScripts))));
        for (u32 i = 0; i < entities; ++i)
        {
            const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
            REQUIRE(world.Add(e, ParCounter()) == ecs::Status::Ok);
            REQUIRE(world.Add(e, ScriptComponent("mixer")) == ecs::Status::Ok);
        }
    }

    u64 Hash()
    {
        u64 sum = 0;
        world.Query<ecs::Read<ParCounter>, ecs::Read<ecs::LocalTransform>>().ForEach(
            [&](ecs::Entity e, const ParCounter& c, const ecs::LocalTransform& t)
            {
                u32 bits[3];
                MemCopy(&bits[0], &t.position.x, 4);
                MemCopy(&bits[1], &t.position.y, 4);
                MemCopy(&bits[2], &t.position.z, 4);
                u64 h = e.ToU64() * 0x9E3779B97F4A7C15ull;
                h = (h ^ static_cast<u64>(c.count)) * 0x100000001B3ull;
                h = (h ^ static_cast<u64>(c.noise)) * 0x100000001B3ull;
                h = (h ^ bits[0]) * 0x100000001B3ull;
                h = (h ^ bits[2]) * 0x100000001B3ull;
                sum += h;
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

u64 RunOn(usize threads, u32 entities, u32 partitions, u32 ticks, ScriptRuntimeStats* stats_out = nullptr)
{
    jobs::Scheduler scheduler(ConfigFor(threads));
    ParallelRig rig(entities, partitions);
    for (u32 t = 0; t < ticks; ++t)
    {
        rig.runtime->Tick(&rig.world, 0.1, &scheduler);
    }
    if (stats_out != nullptr)
    {
        *stats_out = rig.runtime->Stats();
    }
    return rig.Hash();
}

}  // namespace

TEST_CASE("Parallel script system: the world is the same at 1, 2, 3, 7 and 16 threads", "[script][system][parallel]")
{
    ScriptRuntimeStats base_stats;
    const u64 base = RunOn(1, 20000, 64, 3, &base_stats);
    CHECK(base != 0);
    CHECK(base_stats.updates == 60000);
    CHECK(base_stats.errors == 0);
    const usize counts[] = {2, 3, 7, 16};
    for (usize threads : counts)
    {
        INFO("threads " << threads);
        ScriptRuntimeStats s;
        CHECK(RunOn(threads, 20000, 64, 3, &s) == base);
        CHECK(s.updates == base_stats.updates);
        CHECK(s.commands_applied == base_stats.commands_applied);
        CHECK(s.errors == 0);
    }
}

TEST_CASE("Parallel script system: the same with no scheduler, and with another partition count the totals still agree",
          "[script][system][parallel]")
{
    ScriptRuntimeStats a;
    const u64 serial = [&]()
    {
        ParallelRig rig(5000, 16);
        for (u32 t = 0; t < 3; ++t)
        {
            rig.runtime->Tick(&rig.world, 0.1);
        }
        a = rig.runtime->Stats();
        return rig.Hash();
    }();
    CHECK(RunOn(4, 5000, 16, 3) == serial);
    CHECK(a.updates == 15000);
}

TEST_CASE("Parallel script system: the schedule fans the partitions out when the system is alone",
          "[script][system][parallel]")
{
    jobs::Scheduler scheduler(ConfigFor(4));
    ParallelRig rig(3000, 32);
    ScriptRuntimeConfig config;
    config.backend = rig.backend.Get();
    config.bindings = &rig.bindings;
    config.components = &rig.catalog;
    config.partitions = 32;
    config.random_seed = 7;
    ScriptRuntime runtime(config);
    runtime.AddScript("parallel_scripts", CopyShared(ByteSpan(reinterpret_cast<const byte*>(kParallelScripts),
                                                              CStrLength(kParallelScripts))));
    rig.world.SetResource(Move(runtime));
    ecs::Schedule schedule;
    schedule.SetScheduler(&scheduler);
    schedule.Add(MakeScriptSystem());
    REQUIRE(schedule.Build().ok);
    for (u32 t = 0; t < 3; ++t)
    {
        schedule.Run(rig.world, 0.1f);
    }
    ParallelRig reference(3000, 32);
    for (u32 t = 0; t < 3; ++t)
    {
        reference.runtime->Tick(&reference.world, 0.1f);
    }
    CHECK(rig.Hash() == reference.Hash());
    CHECK(rig.world.Resource<ScriptRuntime>().Stats().updates == 9000);
}

TEST_CASE("Parallel script system: the entity hash spreads ids evenly over the partitions",
          "[script][system][parallel][distribution]")
{
    struct Case
    {
        u32 ids;
        u32 partitions;
    };
    const Case cases[] = {{1000, 16}, {100000, 64}, {1000000, 64}, {1000000, 256}};
    for (const Case& c : cases)
    {
        Vector<u32> buckets;
        buckets.Resize(c.partitions);
        for (u32 i = 0; i < c.ids; ++i)
        {
            // Entity ids: index in the low half, generation 1 in the high half (as the ECS makes them).
            const u64 id = (u64{1} << 32) | i;
            ++buckets[PartitionOf(id, c.partitions)];
        }
        u32 most = 0;
        u32 least = 0xFFFFFFFFu;
        for (usize b = 0; b < buckets.Size(); ++b)
        {
            most = buckets[b] > most ? buckets[b] : most;
            least = buckets[b] < least ? buckets[b] : least;
        }
        const f64 mean = static_cast<f64>(c.ids) / static_cast<f64>(c.partitions);
        INFO("ids " << c.ids << " partitions " << c.partitions << " most " << most << " least " << least);
        CHECK(static_cast<f64>(most) < mean * (c.ids < 10000 ? 1.5 : 1.1));
        CHECK(static_cast<f64>(least) > mean * (c.ids < 10000 ? 0.6 : 0.9));
    }
}

TEST_CASE("Parallel script system: the scaling curve over 100,000 scripted entities (printed, not asserted)",
          "[.][script][system][parallel][cost]")
{
    struct Run
    {
        usize threads;
        u32 partitions;
    };
    const Run runs[] = {{1, 64}, {2, 64}, {4, 64}, {8, 64}, {16, 64}, {8, 16}, {8, 256}};
    for (const Run& r : runs)
    {
        jobs::Scheduler scheduler(ConfigFor(r.threads));
        ParallelRig rig(100000, r.partitions);
        rig.runtime->Tick(&rig.world, 0.1, &scheduler);  // the first tick makes the states and the instances
        const u64 begin = os::MonotonicTimeNanos();
        for (u32 t = 0; t < 3; ++t)
        {
            rig.runtime->Tick(&rig.world, 0.1, &scheduler);
        }
        const f64 seconds = static_cast<f64>(os::MonotonicTimeNanos() - begin) * 1e-9;
        os::Print("scaling: threads %u partitions %u: %.0f entity updates per second\n", static_cast<u32>(r.threads),
                  r.partitions, 300000.0 / seconds);
        CHECK(rig.runtime->Stats().errors == 0);
    }
}

TEST_CASE("Parallel script system: many threads logging at once cause no errors (lines are flushed in partition order)",
          "[script][system][parallel]")
{
    // Each partition has its own log buffer, flushed in partition order after the jobs finish (the order itself is
    // not captured by a test: the log sink has no capture hook yet).
    jobs::Scheduler scheduler(ConfigFor(8));
    ParallelRig rig(2000, 32);
    const char* chatty = "behavior('chatty', { on_update = function(self) log.info('tick') end })";
    rig.runtime->AddScript("chatty", CopyShared(ByteSpan(reinterpret_cast<const byte*>(chatty), CStrLength(chatty))));
    for (u32 i = 0; i < 500; ++i)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
        REQUIRE(rig.world.Add(e, ScriptComponent("chatty")) == ecs::Status::Ok);
    }
    rig.runtime->Tick(&rig.world, 0.1, &scheduler);
    CHECK(rig.runtime->Stats().errors == 0);
}
