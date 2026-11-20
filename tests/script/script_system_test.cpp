// The script component, the behavior lifecycle and the serial script system (Phase 27 Day 457): the order callbacks
// run in, enable and disable, errors, destroys (by the host, by a script, by removing the component) and the system in
// a schedule.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
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

struct Tracer
{
    String<HeapAllocator> trail{HeapAllocator::Default()};
};

struct Tally
{
    i32 count = 0;
};

constexpr u32 kTracerId = 2100;
constexpr u32 kTallyId = 2101;

const char* const kSystemScripts = R"LUA(
local function trail(self, d)
  local t = component.get(self.entity, 'tracer')
  component.set(self.entity, 'tracer', { trail = t.trail .. d })
end
local function bump(e)
  local t = component.get(e, 'tally')
  component.set(e, 'tally', { count = t.count + 1 })
end

behavior('tracer', {
  on_create = function(self) trail(self, '1') end,
  on_enable = function(self) trail(self, '2') end,
  on_start = function(self) trail(self, '3') end,
  on_fixed_update = function(self, dt) trail(self, '4') end,
  on_update = function(self, dt) trail(self, '5') end,
  on_disable = function(self) trail(self, '6') end,
})
behavior('watcher', {
  on_destroy = function(self) bump(self.params.tally) end,
})
behavior('suicide', {
  on_update = function(self) entity.destroy(self.entity) end,
  on_destroy = function(self) bump(self.params.tally) end,
})
behavior('lifecycle', {
  on_disable = function(self) trail(self, '6') end,
  on_destroy = function(self) bump(self.params.tally) end,
})
behavior('breaker', {
  on_update = function(self) error('boom') end,
})
behavior('selfcount', {
  on_update = function(self)
    local t = component.get(self.entity, 'tally')
    component.set(self.entity, 'tally', { count = t.count + 1 })
  end,
})
)LUA";

struct SystemRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    UniquePtr<ScriptRuntime> runtime;

    explicit SystemRig(OnScriptError on_error = OnScriptError::Disable, u32 partitions = 16) : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(registry
                    .Register<Tracer>(
                        {.id = kTracerId, .name = "tracer", .fields = {storage::Field<&Tracer::trail>("trail")}})
                    .Ok());
        REQUIRE(
            registry
                .Register<Tally>({.id = kTallyId, .name = "tally", .fields = {storage::Field<&Tally::count>("count")}})
                .Ok());
        REQUIRE(RegisterStandardBindings(&bindings));
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.on_error = on_error;
        config.components = &catalog;
        runtime = MakeUnique<ScriptRuntime>(config);
        runtime->AddScript("system_scripts", CopyShared(ByteSpan(reinterpret_cast<const byte*>(kSystemScripts),
                                                                 CStrLength(kSystemScripts))));
    }

    ecs::Entity Make(StringView behavior, Value params = Value(), bool enabled = true, bool with_tracer = true)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        if (with_tracer)
        {
            REQUIRE(world.Add(e, Tracer()) == ecs::Status::Ok);
        }
        REQUIRE(world.Add(e, ScriptComponent(behavior, Move(params), enabled)) == ecs::Status::Ok);
        return e;
    }

    ecs::Entity MakeTally()
    {
        const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        REQUIRE(world.Add(e, Tally()) == ecs::Status::Ok);
        return e;
    }

    static Value TallyParams(ecs::Entity tally)
    {
        Value map = Value::MakeMap();
        REQUIRE(map.Set("tally", Value::FromEntity(tally)));
        return map;
    }

    String<HeapAllocator> Trail(ecs::Entity e) const
    {
        const Tracer* t = static_cast<const ecs::World&>(world).Get<Tracer>(e);
        return t != nullptr ? String<HeapAllocator>(HeapAllocator::Default(), t->trail.View())
                            : String<HeapAllocator>(HeapAllocator::Default());
    }

    i32 Count(ecs::Entity e) const { return static_cast<const ecs::World&>(world).Get<Tally>(e)->count; }
};

}  // namespace

TEST_CASE("Script system: callbacks run in the order create, enable, start, fixed, update", "[script][system]")
{
    SystemRig rig;
    const ecs::Entity e = rig.Make("tracer");
    rig.runtime->SetFixedSteps(2, 0.01);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Trail(e).View() == StringView("123445"));
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Trail(e).View() == StringView("123445445"));
    const ScriptRuntimeStats& stats = rig.runtime->Stats();
    CHECK(stats.ticks == 2);
    CHECK(stats.instances == 1);
    CHECK(stats.created == 1);
    CHECK(stats.started == 1);
    CHECK(stats.fixed_updates == 4);
    CHECK(stats.updates == 2);
    CHECK(stats.errors == 0);
    CHECK(stats.commands_applied == 9);
}

TEST_CASE("Script system: a disabled instance is created but does not run, and enabling it starts it",
          "[script][system]")
{
    SystemRig rig;
    const ecs::Entity e = rig.Make("tracer", Value(), false);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Trail(e).View() == StringView("1"));
    rig.world.Get<ScriptComponent>(e)->enabled = true;
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Trail(e).View() == StringView("1235"));
    rig.world.Get<ScriptComponent>(e)->enabled = false;
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Trail(e).View() == StringView("12356"));
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Trail(e).View() == StringView("12356"));
    rig.world.Get<ScriptComponent>(e)->enabled = true;
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Trail(e).View() == StringView("1235625"));  // enabled again: no second on_start
    CHECK(rig.runtime->Stats().started == 1);
}

TEST_CASE("Script system: a failing callback disables its instance and the others keep running", "[script][system]")
{
    SystemRig rig;
    const ecs::Entity bad = rig.Make("breaker", Value(), true, false);
    const ecs::Entity unknown = rig.Make("no_such_behavior", Value(), true, false);
    const ecs::Entity good = rig.Make("tracer");
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Trail(good).View() == StringView("1235"
                                               "5"));
    const ScriptRuntimeStats& stats = rig.runtime->Stats();
    CHECK(stats.errors == 2);  // one each, then they are disabled
    CHECK(stats.disabled_by_error == 2);
    CHECK(stats.instances == 3);
    CHECK(rig.world.Has<ScriptComponent>(bad));
    CHECK(rig.world.Has<ScriptComponent>(unknown));
}

TEST_CASE("Script system: with script.on_error = retry a failing instance is called again", "[script][system]")
{
    SystemRig rig(OnScriptError::Retry);
    rig.Make("breaker", Value(), true, false);
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.runtime->Stats().errors == 3);
    CHECK(rig.runtime->Stats().disabled_by_error == 0);
}

TEST_CASE(
    "Script system: on_destroy runs when the host destroys the entity, removes the component or a script destroys it",
    "[script][system]")
{
    SystemRig rig;
    const ecs::Entity tally = rig.MakeTally();
    const ecs::Entity by_host = rig.Make("watcher", SystemRig::TallyParams(tally), true, false);
    const ecs::Entity by_removal = rig.Make("watcher", SystemRig::TallyParams(tally), true, false);
    const ecs::Entity by_script = rig.Make("suicide", SystemRig::TallyParams(tally), true, false);
    rig.runtime->Tick(&rig.world, 0.1);
    // The suicide destroyed itself in its on_update; the merge applied it and the second phase closed it.
    CHECK_FALSE(rig.world.Exists(by_script));
    CHECK(rig.Count(tally) == 1);
    CHECK(rig.runtime->Stats().instances == 2);

    REQUIRE(rig.world.Destroy(by_host) == ecs::Status::Ok);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Count(tally) == 2);
    CHECK(rig.runtime->Stats().instances == 1);

    REQUIRE(rig.world.Remove<ScriptComponent>(by_removal) == ecs::Status::Ok);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Count(tally) == 3);
    CHECK(rig.runtime->Stats().instances == 0);
    CHECK(rig.runtime->Stats().destroyed == 3);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Count(tally) == 3);
}

TEST_CASE("Script system: a new behavior name replaces the instance on the next tick", "[script][system]")
{
    SystemRig rig;
    const ecs::Entity tally = rig.MakeTally();
    const ecs::Entity e = rig.Make("watcher", SystemRig::TallyParams(tally), true, true);
    rig.runtime->Tick(&rig.world, 0.1);
    rig.world.Get<ScriptComponent>(e)->behavior = String<HeapAllocator>(HeapAllocator::Default(), "tracer");
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Count(tally) == 1);  // the old instance was closed
    CHECK(rig.Trail(e).View() == StringView(""));
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Trail(e).View() == StringView("1235"));
}

TEST_CASE("Script system: ten thousand instances run for three ticks", "[script][system][scale]")
{
    SystemRig rig;
    Vector<ecs::Entity> all;
    for (u32 i = 0; i < 10000; ++i)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
        REQUIRE(rig.world.Add(e, Tally()) == ecs::Status::Ok);
        REQUIRE(rig.world.Add(e, ScriptComponent("selfcount")) == ecs::Status::Ok);
        all.Push(e);
    }
    for (u32 t = 0; t < 3; ++t)
    {
        rig.runtime->Tick(&rig.world, 0.1);
    }
    for (usize i = 0; i < all.Size(); ++i)
    {
        REQUIRE(rig.Count(all[i]) == 3);
    }
    CHECK(rig.runtime->Stats().instances == 10000);
    CHECK(rig.runtime->Stats().updates == 30000);
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Script system: the schedule runs it alone with the resource it declares", "[script][system]")
{
    SystemRig rig;
    const ecs::Entity e = rig.Make("tracer");
    ScriptRuntimeConfig config;
    config.backend = rig.backend.Get();
    config.bindings = &rig.bindings;
    config.components = &rig.catalog;
    config.partitions = 4;
    ScriptRuntime runtime(config);
    runtime.AddScript("system_scripts",
                      CopyShared(ByteSpan(reinterpret_cast<const byte*>(kSystemScripts), CStrLength(kSystemScripts))));
    rig.world.SetResource(Move(runtime));
    ecs::Schedule schedule;
    schedule.Add(MakeScriptSystem());
    REQUIRE(schedule.Build().ok);
    schedule.Run(rig.world, 0.25f);
    schedule.Run(rig.world, 0.25f);
    CHECK(rig.Trail(e).View() == StringView("12355"));
    CHECK(rig.world.Resource<ScriptRuntime>().Stats().ticks == 2);
}

TEST_CASE("Script system: a destroyed entity gets on_destroy but no on_disable", "[script][system]")
{
    SystemRig rig;
    const ecs::Entity tally = rig.MakeTally();
    const ecs::Entity e = rig.Make("lifecycle", SystemRig::TallyParams(tally), true, true);
    rig.runtime->Tick(&rig.world, 0.1);
    REQUIRE(rig.world.Destroy(e) == ecs::Status::Ok);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Count(tally) == 1);  // on_destroy ran; an on_disable on the dead entity would have failed the instance
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Script system: a partition whose scripts do not load is skipped and counted", "[script][system]")
{
    SystemRig rig;
    const char* broken = "this is not lua (";
    rig.runtime->AddScript("broken", CopyShared(ByteSpan(reinterpret_cast<const byte*>(broken), CStrLength(broken))));
    rig.Make("tracer");
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.runtime->Stats().skipped_partitions == 1);
    CHECK(rig.runtime->Stats().created == 0);
}

TEST_CASE("Script system: the last tick's load is reported per partition", "[script][system]")
{
    SystemRig rig;
    Vector<PartitionLoad> load;
    rig.runtime->LastPartitionLoad(&load);
    REQUIRE(load.Size() == 16);  // before any tick: every partition idle
    for (const PartitionLoad& l : load)
    {
        CHECK(l.instances == 0);
        CHECK(l.nanoseconds == 0);
    }
    for (u32 i = 0; i < 1000; ++i)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
        REQUIRE(rig.world.Add(e, Tally()) == ecs::Status::Ok);
        REQUIRE(rig.world.Add(e, ScriptComponent("selfcount")) == ecs::Status::Ok);
    }
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->LastPartitionLoad(&load);
    REQUIRE(load.Size() == 16);
    u32 instances = 0;
    u32 busy = 0;
    for (const PartitionLoad& l : load)
    {
        instances += l.instances;
        busy += l.nanoseconds > 0 ? 1u : 0u;
        CHECK((l.instances == 0) == (l.nanoseconds == 0));
    }
    CHECK(instances == 1000);
    CHECK(busy > 1);  // the instances are spread over the partitions
}
