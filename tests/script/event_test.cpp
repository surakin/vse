// The event bus and the event bindings (Phase 27 Day 459): delivery one tick later, in an order that does not depend on
// threads, one-shot and removed handlers, targeted events, chain limit, cleanup when an instance goes away, native
// emitters and subscribers.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "log/output.h"
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

struct Log
{
    String<HeapAllocator> text{HeapAllocator::Default()};
    i32 hits = 0;
};

constexpr u32 kEventLogId = 2120;

const char* const kEventScripts = R"LUA(
local function say(self, s)
  local l = component.get(self.entity, 'event_log')
  component.set(self.entity, 'event_log', { text = l.text .. s, hits = l.hits + 1 })
end

behavior('listener', {
  on_create = function(self)
    event.on('ping', function(me, payload, sender) say(me, 'p' .. tostring(payload)) end)
  end,
})
behavior('once_listener', {
  on_create = function(self)
    event.once('ping', function(me, payload) say(me, 'o') end)
  end,
})
behavior('emitter', {
  on_update = function(self)
    if time.tick() == 0 then event.emit('ping', 7) end
  end,
})
behavior('target_emitter', {
  on_update = function(self)
    if time.tick() == 0 then event.emit_to(self.params.target, 'ping', 9) end
  end,
})
behavior('sender_check', {
  on_create = function(self)
    event.on('hello', function(me, payload, sender)
      say(me, entity.valid(sender) and 's' or 'x')
    end)
  end,
})
behavior('hello_emitter', {
  on_update = function(self) if time.tick() == 0 then event.emit('hello', nil) end end,
})
behavior('echo', {  -- every ping is answered with another ping: a chain that never ends on its own
  on_create = function(self)
    event.on('ping', function(me, payload) say(me, '.'); event.emit('ping', payload + 1) end)
  end,
})
behavior('quitter', {
  on_create = function(self)
    event.on('ping', function(me) say(me, 'q'); event.off('ping') end)
  end,
})
behavior('native_listener', {
  on_create = function(self)
    event.on('from_native', function(me, payload) say(me, 'n' .. tostring(payload)) end)
  end,
})
behavior('self_waiter', {  -- a handler that waits for the event it is handling
  on_create = function(self)
    event.on('ping', function(me)
      say(me, 'h')
      wait_event('ping')
      say(me, 'w')
    end)
  end,
})
behavior('host_replacer', {  -- tries to replace the behavior host's globals and tables
  on_create = function(self)
    local function try(f) local ok = pcall(f) return ok and 'R' or 'L' end
    say(self, try(function() __behavior_call = function() return true end end))
    say(self, try(function() behavior = nil end))
    say(self, try(function() rawset(_G, 'spawn', print) end))
    say(self, try(function() event.on = function() end end))
    say(self, try(function() rawset(event, 'emit', print) end))
    say(self, try(function() task.run = nil end))
    say(self, try(function() my_global = 1 end))
  end,
})
behavior('clock_reader', {  -- reads the wall clock every tick
  on_update = function(self)
    local ok = pcall(time.real)
    say(self, ok and 'r' or 'x')
  end,
})
behavior('unsubscribed', {
  on_create = function(self)
    event.on('ping', function(me) say(me, 'u') end)
  end,
  on_destroy = function(self) end,
})
)LUA";

struct EventRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    UniquePtr<ScriptRuntime> runtime;

    explicit EventRig(u32 partitions = 8, u32 chain_limit = 8) : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(registry
                    .Register<Log>({.id = kEventLogId,
                                    .name = "event_log",
                                    .fields = {storage::Field<&Log::text>("text"), storage::Field<&Log::hits>("hits")}})
                    .Ok());
        REQUIRE(RegisterStandardBindings(&bindings));
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.components = &catalog;
        config.event_chain_limit = chain_limit;
        runtime = MakeUnique<ScriptRuntime>(config);
        runtime->AddScript("event_scripts", CopyShared(ByteSpan(reinterpret_cast<const byte*>(kEventScripts),
                                                                CStrLength(kEventScripts))));
    }

    ecs::Entity Make(StringView behavior, Value params = Value())
    {
        const ecs::Entity e = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        REQUIRE(world.Add(e, Log()) == ecs::Status::Ok);
        REQUIRE(world.Add(e, ScriptComponent(behavior, Move(params))) == ecs::Status::Ok);
        return e;
    }

    String<HeapAllocator> Text(ecs::Entity e) const
    {
        const Log* l = static_cast<const ecs::World&>(world).Get<Log>(e);
        return String<HeapAllocator>(HeapAllocator::Default(), l->text.View());
    }
};

struct NativeSeen
{
    i32 count = 0;
    u64 last_sender = 0;
    i64 last_payload = 0;
};

void OnNative(void* user, const EventRecord& event)
{
    NativeSeen* seen = static_cast<NativeSeen*>(user);
    ++seen->count;
    seen->last_sender = event.sender_key;
    seen->last_payload = event.payload.AsInt();
}

}  // namespace

TEST_CASE("Events: a handler runs one tick after the emit, never inside it", "[script][event]")
{
    EventRig rig;
    const ecs::Entity listener = rig.Make("listener");
    const ecs::Entity emitter = rig.Make("emitter");
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Text(listener).View() == StringView(""));  // emitted this tick, delivered at the next
    CHECK(rig.runtime->Events().Pending() == 1);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Text(listener).View() == StringView("p7"));
    CHECK(rig.runtime->Events().Pending() == 0);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Text(listener).View() == StringView("p7"));  // delivered once
    CHECK(rig.runtime->Stats().events_emitted == 1);
    CHECK(rig.runtime->Stats().events_delivered == 1);
    CHECK(rig.world.Has<ScriptComponent>(emitter));
}

TEST_CASE("Events: once handlers run once, off removes the subscription", "[script][event]")
{
    EventRig rig;
    const ecs::Entity once = rig.Make("once_listener");
    const ecs::Entity quitter = rig.Make("quitter");
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->Events().Emit("ping", Value::FromInt(1));
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->Events().Emit("ping", Value::FromInt(2));
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Text(once).View() == StringView("o"));
    CHECK(rig.Text(quitter).View() == StringView("q"));
    CHECK(rig.runtime->Stats().events_delivered == 2);  // the second ping reached nobody
}

TEST_CASE("Events: emit_to reaches only its target, and the sender is the emitting entity", "[script][event]")
{
    EventRig rig;
    const ecs::Entity a = rig.Make("listener");
    const ecs::Entity b = rig.Make("listener");
    Value params = Value::MakeMap();
    REQUIRE(params.Set("target", Value::FromEntity(b)));
    rig.Make("target_emitter", Move(params));
    const ecs::Entity checker = rig.Make("sender_check");
    rig.Make("hello_emitter");
    for (u32 t = 0; t < 3; ++t)
    {
        rig.runtime->Tick(&rig.world, 0.1);
    }
    CHECK(rig.Text(a).View() == StringView(""));
    CHECK(rig.Text(b).View() == StringView("p9"));
    CHECK(rig.Text(checker).View() == StringView("s"));
}

TEST_CASE("Events: a chain that answers itself is cut at the chain limit", "[script][event]")
{
    EventRig rig(8, 3);
    const ecs::Entity echo = rig.Make("echo");
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->Events().Emit("ping", Value::FromInt(0));
    for (u32 t = 0; t < 12; ++t)
    {
        rig.runtime->Tick(&rig.world, 0.1);
    }
    // The native ping has chain 0; each handler emits chain + 1; chains 0 to 3 are delivered, chain 4 is dropped.
    CHECK(rig.Text(echo).View() == StringView("...."));
    CHECK(rig.runtime->Events().Dropped() == 1);
    CHECK(rig.runtime->Events().Pending() == 0);
}

TEST_CASE("Events: a destroyed instance is unsubscribed", "[script][event]")
{
    EventRig rig;
    const ecs::Entity u = rig.Make("unsubscribed");
    const ecs::Entity keep = rig.Make("listener");
    rig.runtime->Tick(&rig.world, 0.1);
    REQUIRE(rig.world.Destroy(u) == ecs::Status::Ok);
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->Events().Emit("ping", Value::FromInt(5));
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Text(keep).View() == StringView("p5"));
    CHECK(rig.runtime->Stats().events_delivered == 1);
}

TEST_CASE("Events: native code emits to scripts and subscribes to what scripts emit", "[script][event]")
{
    EventRig rig;
    const ecs::Entity listener = rig.Make("native_listener");
    const ecs::Entity emitter = rig.Make("emitter");
    NativeSeen seen;
    const u32 id = rig.runtime->Events().Subscribe("ping", &OnNative, &seen);
    CHECK(id != 0);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(seen.count == 0);
    rig.runtime->Events().Emit("from_native", Value::FromInt(3));
    CHECK_FALSE(rig.runtime->Events().Emit("", Value()));
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Text(listener).View() == StringView("n3"));
    CHECK(seen.count == 1);  // the emitter's ping reached the native subscriber at the same tick start
    CHECK(seen.last_sender == emitter.ToU64());
    CHECK(seen.last_payload == 7);
    CHECK(rig.runtime->Events().Unsubscribe(id));
    CHECK_FALSE(rig.runtime->Events().Unsubscribe(id));
}

namespace
{

struct SelfRemover
{
    EventBus* bus = nullptr;
    u32 id = 0;
    i32 count = 0;
};

void OnSelfRemover(void* user, const EventRecord&)
{
    SelfRemover* self = static_cast<SelfRemover*>(user);
    ++self->count;
    self->bus->Unsubscribe(self->id);
}

}  // namespace

TEST_CASE("Events: a native handler that unsubscribes during delivery does not cost the next one its event",
          "[script][event]")
{
    EventBus bus(8);
    SelfRemover remover;
    remover.bus = &bus;
    remover.id = bus.Subscribe("x", &OnSelfRemover, &remover);
    NativeSeen seen;
    bus.Subscribe("x", &OnNative, &seen);
    REQUIRE(bus.Emit("x", Value::FromInt(1)));
    REQUIRE(bus.Emit("x", Value::FromInt(2)));
    Vector<EventRecord> taken;
    bus.Take(&taken);
    CHECK(remover.count == 1);  // removed by its own first call: not called for the second event
    CHECK(seen.count == 2);     // the subscription after it got both events
    CHECK(seen.last_payload == 2);
    CHECK_FALSE(bus.Unsubscribe(remover.id));  // gone for good
}

TEST_CASE("Events: a handler that waits for its own event is not woken by that event", "[script][event]")
{
    EventRig rig;
    const ecs::Entity waiter = rig.Make("self_waiter");
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->Events().Emit("ping", Value::FromInt(1));
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Text(waiter).View() == StringView("h"));  // parked in its handler, not woken by the same ping
    rig.runtime->Events().Emit("ping", Value::FromInt(2));
    rig.runtime->Tick(&rig.world, 0.1);
    const String<HeapAllocator> text = rig.Text(waiter);
    INFO(text.CStr());
    CHECK(text.View() != StringView("h"));  // the next ping woke the waiter
    CHECK(text.Length() >= 2);
    CHECK(text.View()[text.Length() - 1] == 'w');
}

TEST_CASE("Events: the behavior host's globals and tables cannot be replaced by a script", "[script][event]")
{
    EventRig rig;
    const ecs::Entity e = rig.Make("host_replacer");
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Text(e).View() == StringView("LLLLLLR"));  // its own globals stay writable
    CHECK(rig.runtime->Stats().errors == 0);
}

TEST_CASE("Runtime: time.real is refused while the host records a replay", "[script][event][replay]")
{
    EventRig rig;
    const ecs::Entity e = rig.Make("clock_reader");
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK_FALSE(rig.runtime->ReplayRecording());
    rig.runtime->SetReplayRecording(true);
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->SetReplayRecording(false);
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.Text(e).View() == StringView("rxr"));
}

TEST_CASE("Events: a handler's error disables its instance and names the event handler", "[script][event]")
{
    EventRig rig;
    const char* bad =
        "behavior('bad_listener', { on_create = function(self) event.on('ping', function() error('x') end) end })";
    rig.runtime->AddScript("bad", CopyShared(ByteSpan(reinterpret_cast<const byte*>(bad), CStrLength(bad))));
    rig.Make("bad_listener");
    rig.runtime->Tick(&rig.world, 0.1);
    rig.runtime->Events().Emit("ping", Value());
    rig.runtime->Tick(&rig.world, 0.1);
    CHECK(rig.runtime->Stats().errors == 1);
    CHECK(rig.runtime->Stats().disabled_by_error == 1);
}

namespace
{

u64 EventHash(usize threads)
{
    jobs::Scheduler::Config sc;
    sc.participants = threads;
    sc.pin = false;
    jobs::Scheduler scheduler(sc);
    EventRig rig(16, 2);
    Vector<ecs::Entity> all;
    for (u32 i = 0; i < 20; ++i)
    {
        all.Push(rig.Make("echo"));
    }
    rig.runtime->Tick(&rig.world, 0.1, &scheduler);
    rig.runtime->Events().Emit("ping", Value::FromInt(0));
    for (u32 t = 0; t < 6; ++t)
    {
        rig.runtime->Tick(&rig.world, 0.1, &scheduler);
    }
    u64 h = 1469598103934665603ull;
    for (usize i = 0; i < all.Size(); ++i)
    {
        const String<HeapAllocator> text = rig.Text(all[i]);
        h = (h ^ text.Length()) * 1099511628211ull;
    }
    h = (h ^ rig.runtime->Stats().events_delivered) * 1099511628211ull;
    h = (h ^ rig.runtime->Events().Dropped()) * 1099511628211ull;
    return h;
}

}  // namespace

TEST_CASE("Events: delivery gives the same state at 1, 2, 7 and 16 threads", "[script][event][parallel]")
{
    const u64 base = EventHash(1);
    CHECK(base != 0);
    CHECK(EventHash(2) == base);
    CHECK(EventHash(7) == base);
    CHECK(EventHash(16) == base);
}

TEST_CASE("Events: the throughput of emit and delivery (printed, not asserted)", "[.][script][event][cost]")
{
    jobs::Scheduler::Config sc;
    sc.participants = 8;
    sc.pin = false;
    jobs::Scheduler scheduler(sc);
    EventRig rig(64, 1000);
    const char* flood =
        "local n = 0 behavior('flood', { on_create = function(self) event.on('tick_ev', function(me, p) n = n + 1 end) end, "
        "on_update = function(self) for i = 1, 100 do event.emit('tick_ev', i) end end })";
    rig.runtime->AddScript("flood", CopyShared(ByteSpan(reinterpret_cast<const byte*>(flood), CStrLength(flood))));
    for (u32 i = 0; i < 100; ++i)
    {
        const ecs::Entity e = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
        REQUIRE(rig.world.Add(e, ScriptComponent("flood")) == ecs::Status::Ok);
    }
    rig.runtime->Tick(&rig.world, 0.1, &scheduler);
    const u64 begin = os::MonotonicTimeNanos();
    for (u32 t = 0; t < 3; ++t)
    {
        rig.runtime->Tick(&rig.world, 0.1, &scheduler);
    }
    const f64 seconds = static_cast<f64>(os::MonotonicTimeNanos() - begin) * 1e-9;
    const f64 delivered = static_cast<f64>(rig.runtime->Stats().events_delivered);
    out::Print("events: %.0f handler calls per second (100 subscribers, 10,000 events a tick)\n", delivered / seconds);
}

TEST_CASE("Events: a map's keys count toward the payload weight", "[script][event]")
{
    Value m = Value::MakeMap();
    const Value plain = Value::FromInt(1);
    REQUIRE(m.Set("a_key_that_is_longer_than_sixteen_bytes_for_sure", Value::FromInt(1)));
    CHECK(EventPayloadWeight(m) >= EventPayloadWeight(plain) + 1 + 2);
}
