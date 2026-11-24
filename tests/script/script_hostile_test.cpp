// The hostile-script suite as one run (Phase 27 Day 476): every kind of misbehaving behavior at once, in 64 partitions
// on 16 threads, for a minute under a sanitizer (ten seconds otherwise). No crash, no leak, no race, no hang: the ticks
// go on, every misbehaving behavior ends each of its callbacks with a status (an error record), and the well-behaved
// neighbor keeps running.
#include <catch2/catch_test_macros.hpp>

#include "soak.h"
#include "build_defs.h"
#include "cstr/cstr.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "os/os.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/component_catalog.h"
#include "script/luajit/luajit_backend.h"
#include "script/script_runtime.h"
#include "storage/builtin_components.h"

using namespace vse;
using namespace vse::script;

namespace
{

const char* const kHostileScripts = R"LUA(
behavior('spinner', { on_update = function() while true do end end })
behavior('hog', { on_update = function(self)
  self.t = self.t or {}
  for i = 1, 100000000 do self.t[#self.t + 1] = string.rep('x', 1000) end
end })
behavior('recurser', { on_update = function() local function f(n) return 1 + f(n + 1) end f(1) end })
behavior('thrower', { on_update = function() error('boom') end })
behavior('stringbomb', { on_update = function() local s = 'x' for i = 1, 40 do s = s .. s end end })
behavior('flooder', { on_update = function() for i = 1, 2000 do event.emit('flood', i) end end })
behavior('chainer', {
  on_create = function(self) event.on('chain', function(me, n) event.emit('chain', n + 1) end) end,
  on_update = function() event.emit('chain', 0) end,
})
behavior('waiter_spam', { on_update = function() for i = 1, 200 do spawn(function() wait_frames(1000) end) end end })
behavior('task_flood', { on_update = function() for i = 1, 3000 do task.run('return 1') end end })
behavior('task_spin', { on_update = function() task.run('while true do end') end })
behavior('swallower', { on_update = function() while true do pcall(function() end) end end })
behavior('bad_component', { on_update = function(self) component.set(self.entity, 'nonexistent', {}) end })
behavior('self_destruct', { on_update = function(self) entity.destroy(self.entity) end })
behavior('mover_storm', { on_update = function(self) transform.set_position(self.params.target, vec3.new(1, 2, 3)) end })
behavior('nan', { on_update = function(self) transform.set_position(self.entity, vec3.new(0 / 0, 0, 0)) end })
behavior('good', { on_update = function(self) self.n = (self.n or 0) + 1 end })
)LUA";

// Behaviors whose every callback ends in a script error (a status), and those the runtime absorbs by refusing, capping
// or dropping (an event flood past its budget, a task queue that is full, a conflicting write): they must not crash or
// stall, and they need not report an error.
const char* const kMustFail[] = {"spinner", "hog", "recurser", "thrower", "bad_component", "nan", "swallower"};
const char* const kAbsorbed[] = {"stringbomb", "flooder", "waiter_spam", "task_flood", "mover_storm"};

}  // namespace

TEST_CASE(
    "Hostile scripts: every kind of misbehavior at once, for ten seconds (a minute in soak), and the world goes on",
    "[script][hostile][stress]")
{
    storage::ComponentRegistry registry;
    REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
    ComponentCatalog catalog(&registry);
    BindingRegistry bindings;
    REQUIRE(RegisterStandardBindings(&bindings));
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ScriptRuntimeConfig config;
    config.backend = backend.Get();
    config.bindings = &bindings;
    config.partitions = 64;
    config.components = &catalog;
    config.partition_memory = 16u << 20;
    config.on_error = OnScriptError::Retry;
    config.budget.instructions = 200000;
    config.log_limit = 4;
    ScriptRuntime runtime(config);
    runtime.AddScript(
        "hostile", CopyShared(ByteSpan(reinterpret_cast<const byte*>(kHostileScripts), CStrLength(kHostileScripts))));
    ecs::World world;
    const ecs::Entity victim = ecs::CreateTransformEntity(world, ecs::LocalTransform());
    for (u32 round = 0; round < 3; ++round)
    {
        for (const char* name : kAbsorbed)
        {
            const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
            Value params = Value::MakeMap();
            REQUIRE(params.Set("target", Value::FromEntity(victim)));
            REQUIRE(world.Add(e, ScriptComponent(StringView(name), Move(params))) == ecs::Status::Ok);
        }
        for (const char* name : kMustFail)
        {
            const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
            Value params = Value::MakeMap();
            REQUIRE(params.Set("target", Value::FromEntity(victim)));
            REQUIRE(world.Add(e, ScriptComponent(StringView(name), Move(params))) == ecs::Status::Ok);
        }
        const ecs::Entity chain = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        REQUIRE(world.Add(chain, ScriptComponent(StringView("chainer"))) == ecs::Status::Ok);
        const ecs::Entity spin = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        REQUIRE(world.Add(spin, ScriptComponent(StringView("task_spin"))) == ecs::Status::Ok);
        const ecs::Entity gone = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        REQUIRE(world.Add(gone, ScriptComponent(StringView("self_destruct"))) == ecs::Status::Ok);
    }
    Vector<ecs::Entity> good;
    for (u32 i = 0; i < 64; ++i)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        REQUIRE(world.Add(e, ScriptComponent(StringView("good"))) == ecs::Status::Ok);
        good.Push(e);
    }
    jobs::Scheduler::Config scheduler_config;
    scheduler_config.participants = 16;
    scheduler_config.pin = false;
    jobs::Scheduler scheduler(scheduler_config);
    // Ten seconds in the ordinary run, every flavor; the soak form ("... [soak]", ENGINE_TEST_SOAK=1) runs the full
    // minute. Ten seconds is hundreds of ticks plain and dozens sanitized, so every behavior misbehaves many times over
    // (the checks below need 3).
    const u64 limit_ns = static_cast<u64>(test_support::SoakCount(10, 60)) * 1000000000ull;
    const u64 start = os::MonotonicTimeNanos();
    u64 ticks = 0;
    // The time budget asks for ticks; a slow machine (two cores, a sanitizer, a loaded Mac) gets fewer in it, so the
    // loop also runs until the checks' minimum of 3 ticks has happened: what is claimed below does not depend on the
    // machine's speed.
    constexpr u64 kMinTicks = 3;
    while (os::MonotonicTimeNanos() - start < limit_ns || ticks < kMinTicks)
    {
        runtime.Tick(&world, 0.016, &scheduler);
        ++ticks;
    }
    const ScriptRuntimeStats stats = runtime.Stats();
    INFO("ticks " << ticks << ", errors " << stats.errors << ", distinct " << stats.distinct_errors);
    CHECK(ticks >= 3);
    CHECK(stats.ticks == ticks);
    CHECK(stats.errors > ticks);  // the thrower alone fails every tick
    CHECK(stats.skipped_partitions == 0);
    // Every misbehaving behavior ended its callbacks with a status that was recorded.
    const Vector<ScriptErrorRecord>& errors = runtime.RecentErrors();
    for (const char* name : kMustFail)
    {
        bool seen = false;
        for (usize i = 0; i < errors.Size(); ++i)
        {
            seen = seen || errors[i].behavior.View() == StringView(name);
        }
        INFO("behavior " << name);
        CHECK(seen);
    }
    // The well-behaved neighbors ran on every tick.
    CHECK(stats.updates >= ticks * 64);
}
