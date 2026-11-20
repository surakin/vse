// Latent waits (Phase 27 Day 460): callbacks that wait for time, frames or an event without blocking a thread, the
// order they resume in, cancellation, and the helpers built on the waits.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "os/console.h"
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

struct Trace
{
    String<HeapAllocator> text{HeapAllocator::Default()};
    i32 value = 0;
};

constexpr u32 kTraceId = 2130;

const char* const kLatentScripts = R"LUA(
local function put(self, s)
  local t = component.get(self.entity, 'latent_trace')
  component.set(self.entity, 'latent_trace', { text = t.text .. s, value = t.value + 1 })
end

behavior('sleeper', {
  on_create = function(self)
    put(self, 'a')
    wait(0.25)
    put(self, 'b')
    wait_frames(2)
    put(self, 'c')
  end,
})
behavior('event_waiter', {
  on_create = function(self)
    local payload, sender = wait_event('go')
    put(self, 'g' .. tostring(payload))
  end,
})
behavior('spawner', {
  on_create = function(self)
    self.h = after(0.3, function(me) put(me, 'A') end)
    local e = every(0.2, function(me) put(me, 'e') end)
    self.stopper = e
  end,
  on_update = function(self)
    if time.tick() == 5 then stop(self.stopper) end
  end,
})
behavior('tweener', {
  on_create = function(self)
    tween(0, 10, 0.4, 'linear', function(me, v) put(me, tostring(math.floor(v)) .. ',') end)
  end,
})
behavior('stopped', {
  on_create = function(self)
    self.t = spawn(function(me) wait(0.5) put(me, 'X') end)
    stop(self.t)
  end,
})
behavior('forever', {
  on_update = function(self) wait(100) end,
})
behavior('napper', {
  on_create = function(self) wait(1000) end,
})
behavior('spinner', {
  on_create = function(self) while true do end end,
})
behavior('late_spawner', {
  on_create = function(self)
    wait(0.15)
    spawn(function(me) put(me, 'S') end)
  end,
})
behavior('dicer', {
  on_create = function(self)
    wait(0.05)
    put(self, tostring(random.float()))
  end,
})
behavior('plain_trace', {
  on_update = function(self) end,
})
behavior('stop_forger', {
  on_create = function(self)
    stop(forge_task)
  end,
})
behavior('forge_victim', {
  on_create = function(self)
    forge_victim = self.entity
    spawn(function(me) end)
  end,
})
behavior('forge_owner', {
  on_create = function(self)
    forge_task = spawn(function(me) wait(0.5) put(me, 'T') end)
  end,
})
behavior('forger', {
  on_create = function(self)
    event.emit_to(forge_victim, '__spawn', forge_task)
    event.emit_to(self.entity, '__spawn', forge_task)
  end,
})
behavior('yielder_in_destroy', {
  on_destroy = function(self) wait(1) end,
})
)LUA";

struct LatentRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    UniquePtr<ScriptRuntime> runtime;

    explicit LatentRig(u32 partitions = 8, u32 coroutine_limit = 65536) : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(registry
                    .Register<Trace>(
                        {.id = kTraceId,
                         .name = "latent_trace",
                         .fields = {storage::Field<&Trace::text>("text"), storage::Field<&Trace::value>("value")}})
                    .Ok());
        REQUIRE(RegisterStandardBindings(&bindings));
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.components = &catalog;
        config.coroutine_limit = coroutine_limit;
        config.budget.instructions = 200000;
        runtime = MakeUnique<ScriptRuntime>(config);
        runtime->AddScript("latent_scripts", CopyShared(ByteSpan(reinterpret_cast<const byte*>(kLatentScripts),
                                                                 CStrLength(kLatentScripts))));
    }

    ecs::Entity Make(StringView behavior)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        REQUIRE(world.Add(e, Trace()) == ecs::Status::Ok);
        REQUIRE(world.Add(e, ScriptComponent(behavior)) == ecs::Status::Ok);
        return e;
    }

    String<HeapAllocator> Text(ecs::Entity e) const
    {
        const Trace* t = static_cast<const ecs::World&>(world).Get<Trace>(e);
        return String<HeapAllocator>(HeapAllocator::Default(), t->text.View());
    }

    void Tick(u32 count = 1)
    {
        for (u32 i = 0; i < count; ++i)
        {
            runtime->Tick(&world, 0.1);
        }
    }
};

}  // namespace

TEST_CASE("Latent waits: wait, wait_frames resume in a later tick without blocking", "[script][latent]")
{
    LatentRig rig;
    const ecs::Entity e = rig.Make("sleeper");
    rig.Tick(1);  // tick 0: 'a', then waits 0.25 s (resumes at the first tick whose time is 0.3)
    CHECK(rig.Text(e).View() == StringView("a"));
    CHECK(rig.runtime->Stats().coroutines_waiting == 1);
    rig.Tick(2);  // ticks 1 and 2: time 0.1 and 0.2
    CHECK(rig.Text(e).View() == StringView("a"));
    rig.Tick(1);  // tick 3: time 0.3 >= 0.25 -> 'b', then wait_frames(2)
    CHECK(rig.Text(e).View() == StringView("ab"));
    rig.Tick(1);
    CHECK(rig.Text(e).View() == StringView("ab"));
    rig.Tick(1);  // two ticks later
    CHECK(rig.Text(e).View() == StringView("abc"));
    CHECK(rig.runtime->Stats().coroutines_waiting == 0);
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Latent waits: wait_event resumes with the payload when the event arrives", "[script][latent]")
{
    LatentRig rig;
    const ecs::Entity e = rig.Make("event_waiter");
    rig.Tick(2);
    CHECK(rig.Text(e).View() == StringView(""));
    rig.runtime->Events().Emit("go", Value::FromInt(5));
    rig.Tick(1);
    CHECK(rig.Text(e).View() == StringView("g5"));
    CHECK(rig.runtime->Stats().coroutines_waiting == 0);
}

TEST_CASE("Latent waits: after, every, stop and tween are library code over wait", "[script][latent]")
{
    LatentRig rig;
    const ecs::Entity s = rig.Make("spawner");
    const ecs::Entity t = rig.Make("tweener");
    const ecs::Entity x = rig.Make("stopped");
    rig.Tick(10);
    const String<HeapAllocator> text = rig.Text(s);
    // `every(0.2)` fires at times 0.3, 0.5 (the spawn takes one tick) until `stop` at tick 5; `after(0.3)` once.
    CHECK(text.View().Contains(StringView("A")));
    CHECK(text.View().Contains(StringView("e")));
    CHECK(text.Length() >= 3);
    const usize before = text.Length();
    rig.Tick(10);
    CHECK(rig.Text(s).Length() == before);  // stopped: no more 'e'
    // The tween ends on exactly 10.
    const String<HeapAllocator> tween = rig.Text(t);
    CHECK(tween.View().Contains(StringView("10,")));
    CHECK(rig.Text(x).View() == StringView(""));  // stopped before it started
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Latent waits: destroying an entity drops its coroutines, and on_destroy may not wait", "[script][latent]")
{
    LatentRig rig;
    const ecs::Entity e = rig.Make("sleeper");
    const ecs::Entity y = rig.Make("yielder_in_destroy");
    rig.Tick(1);
    CHECK(rig.runtime->Stats().coroutines_waiting == 1);
    REQUIRE(rig.world.Destroy(e) == ecs::Status::Ok);
    REQUIRE(rig.world.Destroy(y) == ecs::Status::Ok);
    rig.Tick(1);
    CHECK(rig.runtime->Stats().coroutines_waiting == 0);
    rig.Tick(5);
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Latent waits: another script cannot start someone else's spawned coroutine by a forged event",
          "[script][latent]")
{
    LatentRig rig(1);  // one partition: one Lua state, so task ids are shared
    const ecs::Entity victim = rig.Make("forge_victim");
    const ecs::Entity owner = rig.Make("forge_owner");
    rig.Make("forger");
    rig.Tick(12);
    CHECK(rig.Text(owner).View() == StringView("T"));
    CHECK(rig.Text(victim).View() == StringView(""));
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Latent waits: another script cannot stop someone else's coroutine", "[script][latent]")
{
    LatentRig rig(1);
    rig.Make("forge_victim");
    const ecs::Entity owner = rig.Make("forge_owner");
    rig.Make("stop_forger");
    rig.Tick(12);
    CHECK(rig.Text(owner).View() == StringView("T"));
}

TEST_CASE("Latent waits: a coroutine that resumes after other instances ran still acts as itself", "[script][latent]")
{
    LatentRig rig(1);
    const ecs::Entity a = rig.Make("late_spawner");
    const ecs::Entity b = rig.Make("plain_trace");
    rig.Tick(8);
    CHECK(rig.Text(a).View() == StringView("S"));
    CHECK(rig.Text(b).View() == StringView(""));
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Latent waits: a disabled instance's wait keeps its remaining time", "[script][latent]")
{
    LatentRig rig;
    const ecs::Entity e = rig.Make("sleeper");
    rig.Tick(1);  // 'a'; resumes at time 0.25
    rig.world.Get<ScriptComponent>(e)->enabled = false;
    rig.Tick(10);  // disabled: the wait moves later each tick
    CHECK(rig.Text(e).View() == StringView("a"));
    rig.world.Get<ScriptComponent>(e)->enabled = true;
    rig.Tick(6);
    CHECK(rig.Text(e).View().Contains(StringView("b")));
}

TEST_CASE("Latent waits: the per-partition coroutine cap is an error, not a crash", "[script][latent]")
{
    LatentRig rig(1, 3);
    for (u32 i = 0; i < 6; ++i)
    {
        rig.Make("forever");
    }
    rig.Tick(1);
    CHECK(rig.runtime->Stats().coroutines_waiting == 3);
    CHECK(rig.runtime->Stats().errors == 3);
    CHECK(rig.runtime->Stats().disabled_by_error == 3);
}

TEST_CASE("Latent waits: a coroutine that never waits hits the instruction budget", "[script][latent]")
{
    LatentRig rig;
    rig.Make("spinner");
    rig.Tick(1);
    CHECK(rig.runtime->Stats().errors == 1);
    CHECK(rig.runtime->Stats().disabled_by_error == 1);
}

namespace
{

u64 LatentHash(usize threads)
{
    jobs::Scheduler::Config sc;
    sc.participants = threads;
    sc.pin = false;
    jobs::Scheduler scheduler(sc);
    LatentRig rig(16);
    Vector<ecs::Entity> all;
    for (u32 i = 0; i < 400; ++i)
    {
        all.Push(rig.Make(i % 2 == 0 ? "sleeper" : "spawner"));
    }
    for (u32 t = 0; t < 12; ++t)
    {
        rig.runtime->Tick(&rig.world, 0.1, &scheduler);
    }
    u64 h = 1469598103934665603ull;
    for (usize i = 0; i < all.Size(); ++i)
    {
        const String<HeapAllocator> text = rig.Text(all[i]);
        for (usize k = 0; k < text.Length(); ++k)
        {
            h = (h ^ static_cast<u8>(text.CStr()[k])) * 1099511628211ull;
        }
    }
    h = (h ^ rig.runtime->Stats().waits_resumed) * 1099511628211ull;
    return h;
}

}  // namespace

TEST_CASE("Latent waits: a resumed wait draws from its own stream, whatever resumed before it", "[script][latent]")
{
    // One partition; the second entity's number must not depend on whether the first one drew before it.
    String<HeapAllocator> alone(HeapAllocator::Default());
    {
        LatentRig rig(1);
        rig.Make("plain_trace");
        const ecs::Entity e = rig.Make("dicer");
        rig.Tick(3);
        alone = rig.Text(e);
    }
    String<HeapAllocator> after_neighbor(HeapAllocator::Default());
    {
        LatentRig rig(1);
        rig.Make("dicer");
        const ecs::Entity e = rig.Make("dicer");
        rig.Tick(3);
        after_neighbor = rig.Text(e);
    }
    CHECK(alone.Length() > 0);
    CHECK(alone.View() == after_neighbor.View());
}

TEST_CASE("Latent waits: the same state at 1, 2, 7 and 16 threads", "[script][latent][parallel]")
{
    const u64 base = LatentHash(1);
    CHECK(base != 0);
    CHECK(LatentHash(2) == base);
    CHECK(LatentHash(7) == base);
    CHECK(LatentHash(16) == base);
}

TEST_CASE("Latent waits: the cost of 100,000 waiting coroutines (printed, not asserted)", "[.][script][latent][cost]")
{
    LatentRig rig(64, 65536);
    for (u32 i = 0; i < 100000; ++i)
    {
        rig.Make("napper");
    }
    rig.runtime->Tick(&rig.world, 0.1);
    const u64 begin = os::MonotonicTimeNanos();
    for (u32 t = 0; t < 3; ++t)
    {
        rig.runtime->Tick(&rig.world, 0.1);
    }
    const f64 seconds = static_cast<f64>(os::MonotonicTimeNanos() - begin) * 1e-9;
    os::Print(
        "latent: %llu coroutines waiting, %.1f ms per tick with nothing due, %llu bytes of script state per entity\n",
        static_cast<unsigned long long>(rig.runtime->Stats().coroutines_waiting), seconds / 3.0 * 1000.0,
        static_cast<unsigned long long>(rig.runtime->Scripts().Total().memory_bytes / 100000));
}
