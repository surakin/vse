// Physics events and characters for scripts (Phase 27 Day 467): contact and trigger events reach the scripts that
// listen through the event bus, nothing is built for the entities that do not, and a character is driven from a script.
// Every scene runs through the real runtime and the documented frame order.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "physics/joint_sync.h"
#include "physics/sync.h"
#include "physics/system.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/component_bindings.h"
#include "script/component_catalog.h"
#include "script/entity_bindings.h"
#include "script/luajit/luajit_backend.h"
#include "script/script_runtime.h"
#include "bytes/byte_writer.h"
#include "physics/character_set.h"
#include "physics/snapshot.h"
#include "script_physics/physics_bindings.h"
#include "script_physics/physics_events.h"
#include "storage/builtin_components.h"

using namespace vse;
using namespace vse::script;
using namespace vse::script_physics;

// A named namespace: the test files of one unity build must not share names through anonymous namespaces.
namespace events_test
{

// What the scripts write: counts of what they were told.
struct Probe
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    i32 begins = 0;
    i32 stays = 0;
    i32 ends = 0;
    i32 enters = 0;
    i32 exits = 0;
};

constexpr u32 kProbeId = 2300;

const char* const kScripts = R"LUA(
-- Counts the events it is told about in its own table (several events reach it in one tick, and the own-entity view
-- starts again for each handler), and writes the counts to its probe.
local function count(me, field, hit)
  me[field] = (me[field] or 0) + 1
  local fields = { begins = me.begins, ends = me.ends, stays = me.stays, enters = me.enters, exits = me.exits }
  if hit and hit.normal then
    fields.x = hit.normal.y
    fields.y = hit.depth
  end
  component.set(me.entity, 'probe', fields)
end
behavior('listener', {
  on_create = function(self)
    event.on('on_collision_begin', function(me, hit) count(me, 'begins', hit) end)
    event.on('on_collision_end', function(me, hit) count(me, 'ends', hit) end)
    event.on('on_collision_stay', function(me, hit) count(me, 'stays', hit) end)
  end,
})
behavior('zone', {
  on_create = function(self)
    event.on('on_trigger_enter', function(me, info) count(me, 'enters', nil) end)
    event.on('on_trigger_exit', function(me, info) count(me, 'exits', nil) end)
  end,
})
-- Makes its character on the first update, then walks east after `delay` ticks.
-- Parks in wait_event for the trigger instead of subscribing: that is a listener too.
behavior('waiter', {
  on_create = function(self)
    local info = wait_event('on_trigger_enter')
    self.got = true
    component.set(self.entity, 'probe', { enters = 1, x = info.other and 1 or 0 })
  end,
})
behavior('walker', {
  on_update = function(self, dt)
    if not character.exists(self.entity) then
      character.create(self.entity, self.params.config)
      return
    end
    self.n = (self.n or 0) + 1
    local speed = self.n > (self.params.delay or 0) and self.params.speed or 0
    character.move(self.entity, vec3.new(speed, 0, 0))
    self.grounded = (self.grounded or 0) + (character.is_grounded(self.entity) and 1 or 0)
    component.set(self.entity, 'probe', { stays = self.grounded })
  end,
})
behavior('jumper', {
  on_update = function(self, dt)
    if not character.exists(self.entity) then
      character.create(self.entity, nil)
      return
    end
    if character.is_grounded(self.entity) and not self.jumped then
      self.jumped = true
      character.jump(self.entity, 5)
    end
    component.set(self.entity, 'probe', { y = character.vertical_speed(self.entity) })
  end,
})
behavior('crouch', {
  on_update = function(self, dt)
    if not character.exists(self.entity) then
      character.create(self.entity, nil)
    else
      character.set_height(self.entity, 1.0)
    end
  end,
})
behavior('bad', {
  on_update = function(self, dt)
    local ok1 = pcall(character.move, self.entity, vec3.new(1, 0, 0))
    local ok2 = pcall(character.set_height, self.entity, 0.1)
    local ok3 = pcall(character.create, self.entity, { radius = -1 })
    component.set(self.entity, 'probe', { begins = (ok1 and 1 or 0) + (ok2 and 1 or 0) + (ok3 and 1 or 0) })
  end,
})
)LUA";

bool Near(f32 a, f32 b, f32 tolerance = 1.0e-3f)
{
    return Abs(a - b) <= tolerance;
}

physics::Collider Box(const Vec3& half, u8 layer = 0, bool trigger = false)
{
    physics::Collider c;
    c.shape = physics::Shape::MakeBox(half);
    c.layer = layer;
    c.is_trigger = trigger;
    return c;
}

physics::Collider Ball(f32 radius, u8 layer = 0)
{
    physics::Collider c;
    c.shape = physics::Shape::MakeSphere(radius);
    c.layer = layer;
    return c;
}

physics::RigidBody BodyOf(physics::BodyType type, f32 mass = 0.0f)
{
    physics::RigidBody b;
    b.type = type;
    b.mass = mass;
    return b;
}

ecs::LocalTransform At(f32 x, f32 y, f32 z)
{
    ecs::LocalTransform t;
    t.position = Vec3(x, y, z);
    return t;
}

jobs::Scheduler::Config ConfigFor(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

struct Rig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    physics::PhysicsWorld physics;
    PhysicsLayers layers;
    PhysicsScriptServices services;
    UniquePtr<physics::CharacterSet> characters;
    UniquePtr<ScriptRuntime> runtime;
    PhysicsEventOptions options;
    PhysicsEventReport last_report;
    u32 emitted = 0;
    u32 unhandled = 0;
    u32 filtered = 0;

    Rig(u32 partitions, bool gravity, bool with_characters = true, bool stay = false) : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(RegisterPhysicsComponents(&registry));
        REQUIRE(registry
                    .Register<Probe>(
                        {.id = kProbeId,
                         .name = "probe",
                         .fields = {storage::Field<&Probe::x>("x"), storage::Field<&Probe::y>("y"),
                                    storage::Field<&Probe::begins>("begins"), storage::Field<&Probe::stays>("stays"),
                                    storage::Field<&Probe::ends>("ends"), storage::Field<&Probe::enters>("enters"),
                                    storage::Field<&Probe::exits>("exits")}})
                    .Ok());
        RegisterPhysicsValidators(&catalog);
        REQUIRE(RegisterStandardBindings(&bindings));
        REQUIRE(RegisterPhysicsBindings(&bindings));
        String<HeapAllocator> why(HeapAllocator::Default());
        REQUIRE(layers.Parse(StringView("default,world,enemy"), &why));
        physics::StepConfig step;
        step.gravity = gravity ? Vec3(0.0f, -9.81f, 0.0f) : Vec3(0.0f, 0.0f, 0.0f);
        step.allow_sleep = false;
        step.stay_events = stay;
        REQUIRE(physics.SetStepConfig(step) == physics::BodyStatus::Ok);
        services.world = &physics;
        services.layers = &layers;
        if (with_characters)
        {
            characters = MakeUnique<physics::CharacterSet>(&physics);
            services.characters = characters.Get();
        }
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.components = &catalog;
        config.random_seed = 3;
        runtime = MakeUnique<ScriptRuntime>(config);
        runtime->Services().extensions[kServicePhysics] = &services;
        REQUIRE(RegisterPhysicsCommandKinds(&runtime->Kinds()));
        runtime->AddScript("events_scripts",
                           CopyShared(ByteSpan(reinterpret_cast<const byte*>(kScripts), CStrLength(kScripts))));
    }

    ecs::Entity Body(const Vec3& at, physics::BodyType type, const physics::Collider& collider, f32 mass = 0.0f)
    {
        return world.Create(BodyOf(type, mass), collider, At(at.x, at.y, at.z), Probe());
    }

    void Script(ecs::Entity e, StringView behavior, Value params = Value())
    {
        if (params.IsNil())
        {
            params = Value::MakeMap();
        }
        REQUIRE(world.Add(e, ScriptComponent(behavior, Move(params))) == ecs::Status::Ok);
    }

    // The documented frame: scripts and the merge, the sync, the drive, the step (the characters move inside it), the
    // write-back, and the events of the step on the bus for the next tick.
    void Frame(jobs::Scheduler* scheduler = nullptr)
    {
        runtime->Tick(&world, 1.0 / 60.0, scheduler);
        physics::SyncBodies(world, physics);
        physics::SyncJoints(world, physics);
        physics::DriveKinematicBodies(world, physics, 1.0f / 60.0f);
        physics.Update(1.0f / 60.0f);
        physics::WriteBackTransforms(world, physics, 1.0f);
        if (characters.Get() != nullptr)
        {
            characters->Prune(world);
            characters->WriteBack(world);
        }
        options.characters = characters.Get();
        last_report = EmitPhysicsEvents(physics, runtime.Get(), options);
        emitted += last_report.emitted;
        unhandled += last_report.unhandled;
        filtered += last_report.filtered;
    }

    const Probe& ProbeOf(ecs::Entity e) { return *world.Get<Probe>(e); }
};

TEST_CASE("Script events: a trigger volume fires enter once per entering entity and exit when one leaves",
          "[script_physics][events]")
{
    for (const usize threads : {usize{1}, usize{16}})
    {
        INFO("threads " << threads);
        jobs::Scheduler scheduler(ConfigFor(threads));
        Rig rig(8, false);
        const ecs::Entity zone = rig.Body(Vec3(0, 0, 0), physics::BodyType::Static, Box(Vec3(3, 3, 3), 0, true));
        rig.Script(zone, StringView("zone"));
        // Three bodies inside the volume; one flies out of it.
        for (u32 i = 0; i < 3; ++i)
        {
            const ecs::Entity ball = rig.Body(Vec3(-1.0f + static_cast<f32>(i), 0, 0), physics::BodyType::Dynamic,
                                              Ball(0.25f), 1.0f);
            if (i == 2)
            {
                physics::Velocity v;
                v.linear = Vec3(0, 40.0f, 0);
                rig.world.Add(ball, v);
            }
        }
        for (u32 frame = 0; frame < 30; ++frame)
        {
            rig.Frame(&scheduler);
        }
        CHECK(rig.ProbeOf(zone).enters == 3);
        CHECK(rig.ProbeOf(zone).exits == 1);
    }
}

TEST_CASE("Script events: a stack of boxes delivers one begin per touching pair and the normal and depth",
          "[script_physics][events]")
{
    Rig rig(4, true);
    rig.Body(Vec3(0, -0.5f, 0), physics::BodyType::Static, Box(Vec3(20, 0.5f, 20)));
    const ecs::Entity low = rig.Body(Vec3(0, 0.51f, 0), physics::BodyType::Dynamic, Box(Vec3(0.5f, 0.5f, 0.5f)), 1.0f);
    const ecs::Entity high = rig.Body(Vec3(0, 1.52f, 0), physics::BodyType::Dynamic, Box(Vec3(0.5f, 0.5f, 0.5f)), 1.0f);
    rig.Script(low, StringView("listener"));
    rig.Script(high, StringView("listener"));
    for (u32 frame = 0; frame < 90; ++frame)
    {
        rig.Frame();
    }
    // The lower box touches the floor and the upper box; the upper touches the lower only.
    CHECK(rig.ProbeOf(low).begins == 2);
    CHECK(rig.ProbeOf(high).begins == 1);
    CHECK(rig.ProbeOf(low).ends == 0);
    CHECK(rig.ProbeOf(high).ends == 0);
    // `normal` points from self toward other: the upper box sees the lower one below it.
    CHECK(rig.ProbeOf(high).x < -0.9f);
}

TEST_CASE("Script events: stay events arrive only when the world makes them", "[script_physics][events]")
{
    for (const bool stay : {false, true})
    {
        Rig rig(2, true, true, stay);
        rig.Body(Vec3(0, -0.5f, 0), physics::BodyType::Static, Box(Vec3(20, 0.5f, 20)));
        const ecs::Entity box = rig.Body(Vec3(0, 0.5f, 0), physics::BodyType::Dynamic, Box(Vec3(0.5f, 0.5f, 0.5f)),
                                         1.0f);
        rig.Script(box, StringView("listener"));
        for (u32 frame = 0; frame < 40; ++frame)
        {
            rig.Frame();
        }
        INFO("stay " << stay);
        CHECK(rig.ProbeOf(box).begins == 1);
        CHECK((rig.ProbeOf(box).stays > 10) == stay);
    }
}

TEST_CASE("Script events: an entity with no handler costs no event", "[script_physics][events]")
{
    Rig rig(4, true);
    rig.Body(Vec3(0, -0.5f, 0), physics::BodyType::Static, Box(Vec3(40, 0.5f, 40)));
    // 100 boxes with no script land on the floor and on each other.
    for (u32 i = 0; i < 100; ++i)
    {
        rig.Body(Vec3(static_cast<f32>(i % 10) * 1.2f, 0.5f + static_cast<f32>(i / 10) * 1.02f, 0),
                 physics::BodyType::Dynamic, Box(Vec3(0.5f, 0.5f, 0.5f)), 1.0f);
    }
    for (u32 frame = 0; frame < 20; ++frame)
    {
        rig.Frame();
    }
    CHECK(rig.unhandled > 100);
    CHECK(rig.emitted == 0);
    CHECK(rig.runtime->Events().Pending() == 0);
    CHECK(rig.physics.Events().Count() < 1000000);  // the world made the events; the bus took none
}

TEST_CASE("Script events: the layer mask keeps the events of other layers out", "[script_physics][events]")
{
    for (const u32 mask : {0xFFFFFFFFu, 1u << 2})
    {
        Rig rig(2, true);
        rig.options.layer_mask = mask;
        rig.Body(Vec3(0, -0.5f, 0), physics::BodyType::Static, Box(Vec3(20, 0.5f, 20), 1));  // layer 1: world
        const ecs::Entity box = rig.Body(Vec3(0, 0.5f, 0), physics::BodyType::Dynamic, Box(Vec3(0.5f, 0.5f, 0.5f)),
                                         1.0f);
        rig.Script(box, StringView("listener"));
        for (u32 frame = 0; frame < 30; ++frame)
        {
            rig.Frame();
        }
        INFO("mask " << mask);
        // The floor is on layer 1: it reaches the script only when the mask lets layer 1 through.
        CHECK(rig.ProbeOf(box).begins == (mask == 0xFFFFFFFFu ? 1 : 0));
        CHECK((rig.filtered > 0) == (mask != 0xFFFFFFFFu));
    }
}

TEST_CASE("Script events: the state and the events are the same at 1 and 16 threads",
          "[script_physics][events][parallel]")
{
    u64 base_hash = 0;
    i64 base_total = -1;
    for (const usize threads : {usize{1}, usize{16}})
    {
        INFO("threads " << threads);
        jobs::Scheduler scheduler(ConfigFor(threads));
        Rig rig(16, true);
        rig.Body(Vec3(0, -0.5f, 0), physics::BodyType::Static, Box(Vec3(60, 0.5f, 60)));
        Vector<ecs::Entity> boxes;
        for (u32 i = 0; i < 48; ++i)
        {
            const ecs::Entity e = rig.Body(Vec3(static_cast<f32>(i % 8) * 1.1f, 0.6f + static_cast<f32>(i / 8) * 1.05f,
                                                static_cast<f32>(i % 3) * 0.1f),
                                           physics::BodyType::Dynamic, Box(Vec3(0.5f, 0.5f, 0.5f)), 1.0f);
            rig.Script(e, StringView("listener"));
            boxes.Push(e);
        }
        for (u32 frame = 0; frame < 120; ++frame)
        {
            rig.Frame(&scheduler);
        }
        i64 total = 0;
        for (usize i = 0; i < boxes.Size(); ++i)
        {
            const Probe& p = rig.ProbeOf(boxes[i]);
            total = total * 31 + p.begins * 7 + p.ends * 3 + static_cast<i64>(p.y * 1000.0f);
        }
        const u64 hash = rig.physics.StateHash();
        if (base_total < 0)
        {
            base_total = total;
            base_hash = hash;
            CHECK(rig.emitted > 48);
        }
        else
        {
            CHECK(total == base_total);
            CHECK(hash == base_hash);
        }
    }
}

TEST_CASE("Script character: a script makes a character that walks, is grounded and moves its entity",
          "[script_physics][character]")
{
    Rig rig(2, true);
    rig.Body(Vec3(0, -0.5f, 0), physics::BodyType::Static, Box(Vec3(50, 0.5f, 50)));
    const ecs::Entity e = rig.world.Create(At(0, 1.0f, 0), Probe());
    Value params = Value::MakeMap();
    params.Set("speed", Value::FromFloat(2.0f));
    rig.Script(e, StringView("walker"), Move(params));
    for (u32 frame = 0; frame < 120; ++frame)
    {
        rig.Frame();
    }
    CHECK(rig.characters->Count() == 1);
    const ecs::LocalTransform* t = rig.world.Get<ecs::LocalTransform>(e);
    // Two seconds at two metres per second, less the frame the character was made in.
    CHECK(t->position.x > 3.5f);
    CHECK(t->position.x < 4.2f);
    CHECK(Abs(t->position.y - 0.9f) < 0.1f);  // the capsule's centre on the floor: half the 1.8 height
    CHECK(rig.ProbeOf(e).stays > 60);         // it reported being grounded
}

TEST_CASE("Script events: a coroutine parked in wait_event is a listener", "[script_physics][events]")
{
    Rig rig(2, false);
    const ecs::Entity zone = rig.Body(Vec3(0, 0, 0), physics::BodyType::Static, Box(Vec3(3, 3, 3), 0, true));
    rig.Script(zone, StringView("waiter"));
    rig.Body(Vec3(0, 0, 0), physics::BodyType::Dynamic, Ball(0.25f), 1.0f);
    for (u32 frame = 0; frame < 10; ++frame)
    {
        rig.Frame();
    }
    CHECK(rig.ProbeOf(zone).enters == 1);
    CHECK(Near(rig.ProbeOf(zone).x, 1.0f));
}

TEST_CASE("Script events: a character that walks into a trigger volume is named by its entity",
          "[script_physics][events][character]")
{
    Rig rig(2, true);
    rig.Body(Vec3(0, -0.5f, 0), physics::BodyType::Static, Box(Vec3(50, 0.5f, 50)));
    const ecs::Entity zone = rig.Body(Vec3(3, 1, 0), physics::BodyType::Static, Box(Vec3(0.5f, 1, 2), 0, true));
    rig.Script(zone, StringView("zone"));
    const ecs::Entity e = rig.world.Create(At(0, 1.0f, 0), Probe());
    Value params = Value::MakeMap();
    params.Set("speed", Value::FromFloat(2.0f));
    rig.Script(e, StringView("walker"), params);
    for (u32 frame = 0; frame < 200; ++frame)
    {
        rig.Frame();
    }
    CHECK(rig.ProbeOf(zone).enters == 1);
    CHECK(rig.ProbeOf(zone).exits == 1);  // it walked on through the volume
}

TEST_CASE("Script character: a script walks up a gentle ramp", "[script_physics][character]")
{
    Rig rig(1, true);
    const f32 angle = 25.0f * 0.0174532925f;
    physics::BodyDesc desc;
    desc.settings.type = physics::BodyType::Static;
    desc.position = Vec3(0.5f * Sin(angle), -0.5f * Cos(angle), 0);
    desc.orientation = Quaternion::FromAxisAngle(Vec3(0, 0, 1), angle);
    const physics::BodyId ramp = rig.physics.CreateBody(desc);
    REQUIRE(rig.physics.AddCollider(ramp, Box(Vec3(15, 0.5f, 5))) == physics::BodyStatus::Ok);
    const ecs::Entity e = rig.world.Create(At(0, 1.2f, 0), Probe());
    Value params = Value::MakeMap();
    params.Set("speed", Value::FromFloat(2.0f));
    params.Set("delay", Value::FromInt(40));  // 40 ticks standing on the ramp's foot, then walking up
    rig.Script(e, StringView("walker"), params);
    for (u32 frame = 0; frame < 40; ++frame)
    {
        rig.Frame();
    }
    const f32 start = static_cast<f32>(rig.world.Get<ecs::LocalTransform>(e)->position.y);
    for (u32 frame = 0; frame < 90; ++frame)
    {
        rig.Frame();
    }
    CHECK(rig.world.Get<ecs::LocalTransform>(e)->position.y > start + 0.5f);
}

TEST_CASE("Script character: a jump leaves the ground and the character lands again", "[script_physics][character]")
{
    Rig rig(1, true);
    rig.Body(Vec3(0, -0.5f, 0), physics::BodyType::Static, Box(Vec3(50, 0.5f, 50)));
    const ecs::Entity e = rig.world.Create(At(0, 1.0f, 0), Probe());
    Value params = Value::MakeMap();
    params.Set("go", Value::FromBool(true));
    rig.Script(e, StringView("jumper"), params);
    f32 highest = 0.0f;
    for (u32 frame = 0; frame < 120; ++frame)
    {
        rig.Frame();
        highest = FMax(highest, static_cast<f32>(rig.world.Get<ecs::LocalTransform>(e)->position.y));
    }
    CHECK(highest > 1.8f);  // 5 m/s upward under gravity rises about a metre and a quarter
    CHECK(Abs(rig.world.Get<ecs::LocalTransform>(e)->position.y - 0.9f) < 0.1f);
    CHECK(rig.characters->Find(e)->State().on_ground);
}

TEST_CASE("Script character: set_height crouches with the feet kept", "[script_physics][character]")
{
    Rig rig(1, true);
    rig.Body(Vec3(0, -0.5f, 0), physics::BodyType::Static, Box(Vec3(50, 0.5f, 50)));
    const ecs::Entity e = rig.world.Create(At(0, 1.0f, 0), Probe());
    rig.Script(e, StringView("crouch"));
    for (u32 frame = 0; frame < 60; ++frame)
    {
        rig.Frame();
    }
    CHECK(Near(rig.characters->Find(e)->Config().height, 1.0f));
    CHECK(Abs(rig.world.Get<ecs::LocalTransform>(e)->position.y - 0.5f) < 0.1f);  // the centre fell to half a metre
    CHECK(rig.characters->Find(e)->State().on_ground);
}

TEST_CASE("Script character: calls that cannot work are errors where they are made", "[script_physics][character]")
{
    Rig rig(1, true);
    const ecs::Entity e = rig.world.Create(At(0, 1.0f, 0), Probe());
    rig.Script(e, StringView("bad"));
    rig.Frame();
    CHECK(rig.ProbeOf(e).begins == 0);  // move without a character, a tiny height, a negative radius: all errors

    Rig without(1, true, false);
    const ecs::Entity f = without.world.Create(At(0, 1.0f, 0), Probe());
    without.Script(f, StringView("walker"));
    without.Frame();
    CHECK(without.runtime->RecentErrors().Size() > 0);
}

TEST_CASE("Script character: standing up under a ceiling waits until there is room", "[script_physics][character]")
{
    physics::PhysicsWorld physics;
    physics::CharacterSet set(&physics);
    ecs::World world;
    const ecs::Entity e = world.Create(At(0, 1, 0));
    physics::BodyDesc floor;
    floor.settings.type = physics::BodyType::Static;
    floor.position = Vec3(0, -0.5f, 0);
    const physics::BodyId floor_id = physics.CreateBody(floor);
    REQUIRE(physics.AddCollider(floor_id, Box(Vec3(50, 0.5f, 50))) == physics::BodyStatus::Ok);
    physics::BodyDesc ceiling;
    ceiling.settings.type = physics::BodyType::Kinematic;
    ceiling.position = Vec3(0, 1.5f, 0);  // its underside is at 1.4: a 1.8 m character does not fit
    const physics::BodyId ceiling_id = physics.CreateBody(ceiling);
    REQUIRE(physics.AddCollider(ceiling_id, Box(Vec3(50, 0.1f, 50))) == physics::BodyStatus::Ok);
    physics::CharacterConfig config;
    config.height = 1.0f;
    REQUIRE(set.Add(e, config, Vec3(0, 0.5f, 0)));
    for (u32 i = 0; i < 20; ++i)
    {
        physics.Step();
    }
    REQUIRE(set.SetHeight(e, 1.8f));
    for (u32 i = 0; i < 20; ++i)
    {
        physics.Step();
    }
    CHECK(Near(set.Find(e)->Config().height, 1.0f));  // blocked: still crouched, and still asking
    physics.DestroyBody(ceiling_id);
    physics.Step();
    CHECK(Near(set.Find(e)->Config().height, 1.8f));
}

TEST_CASE("Script character: the set refuses what cannot work", "[script_physics][character]")
{
    physics::PhysicsWorld physics;
    physics::CharacterSet set(&physics);
    ecs::World world;
    const ecs::Entity e = world.Create(At(0, 1, 0));
    physics::CharacterConfig bad;
    bad.height = 0.1f;
    CHECK_FALSE(set.Add(e, bad, Vec3(0, 1, 0)));
    CHECK_FALSE(set.Add(ecs::Entity(), physics::CharacterConfig(), Vec3(0, 1, 0)));
    CHECK(set.Add(e, physics::CharacterConfig(), Vec3(0, 1, 0)));
    CHECK_FALSE(set.Add(e, physics::CharacterConfig(), Vec3(0, 1, 0)));  // one character per entity
    CHECK_FALSE(set.SetMove(e, Vec3(Limits<f32>::QuietNaN(), 0, 0)));
    CHECK_FALSE(set.SetHeight(e, 0.2f));
    CHECK(set.SetHeight(e, 1.2f));
    world.Destroy(e);
    CHECK(set.Prune(world) == 1);
    CHECK(set.Count() == 0);
    CHECK(physics.Stats().bodies == 0);
}

TEST_CASE("Script character: a set's snapshot puts the characters back with the world's", "[script_physics][character]")
{
    // The physics snapshot holds the bodies; the set's holds the rest (the vertical speed, the ground, the held move).
    // Restored together, a run continues bit for bit (C API script physics, 2026-11-20).
    physics::PhysicsWorld physics;
    physics::BodyDesc floor;
    floor.settings.type = physics::BodyType::Static;
    floor.position = Vec3(0, -0.5f, 0);
    const physics::BodyId floor_id = physics.CreateBody(floor);
    physics::Collider box;
    box.shape = physics::Shape::MakeBox(Vec3(20, 0.5f, 20));
    REQUIRE(physics.AddCollider(floor_id, box) == physics::BodyStatus::Ok);
    physics::CharacterSet set(&physics);
    ecs::World world;
    const ecs::Entity e = world.Create(At(0, 3, 0));
    const ecs::Entity gone = world.Create(At(4, 3, 0));
    REQUIRE(set.Add(e, physics::CharacterConfig(), Vec3(0, 3, 0)));
    REQUIRE(set.Add(gone, physics::CharacterConfig(), Vec3(4, 3, 0)));
    REQUIRE(set.SetMove(e, Vec3(1, 0, 0)));
    for (u32 i = 0; i < 20; ++i)
    {
        physics.Step();  // falling: the vertical speed is the set's, not the world's
    }
    REQUIRE(set.Find(e)->State().vertical_speed < -1.0f);
    bytes::ByteWriter world_bytes;
    physics.Snapshot(&world_bytes);
    bytes::ByteWriter set_bytes;
    set.Snapshot(&set_bytes);
    Vector<byte> kept_world;
    Vector<byte> kept_set;
    for (usize i = 0; i < world_bytes.Size(); ++i)
    {
        kept_world.Push(world_bytes.Bytes()[i]);
    }
    for (usize i = 0; i < set_bytes.Size(); ++i)
    {
        kept_set.Push(set_bytes.Bytes()[i]);
    }
    for (u32 i = 0; i < 60; ++i)
    {
        physics.Step();
    }
    const Vec3 first = physics.Find(set.Find(e)->BodyHandle())->position.Narrow();
    CHECK(set.Find(e)->State().on_ground);
    // Back to the snapshot, with the held move changed meanwhile and one character removed.
    REQUIRE(set.SetMove(e, Vec3(-3, 0, 0)));
    REQUIRE(set.Remove(gone));
    REQUIRE(physics.Restore(ByteSpan(kept_world.Data(), kept_world.Size())) == physics::SnapshotStatus::Ok);
    CHECK(physics.Stats().bodies == 3);  // the floor and both characters' bodies of that moment
    REQUIRE(set.Restore(ByteSpan(kept_set.Data(), kept_set.Size())));
    CHECK(physics.Stats().bodies == 2);  // the removed character's kept body is destroyed
    CHECK(set.Recreate(world) == 0);     // the kept body is the character's again
    for (u32 i = 0; i < 60; ++i)
    {
        physics.Step();
    }
    const Vec3 second = physics.Find(set.Find(e)->BodyHandle())->position.Narrow();
    CHECK(first.x == second.x);
    CHECK(first.y == second.y);
    CHECK(first.z == second.z);
    // Malformed bytes are refused, and nothing changes.
    CHECK_FALSE(physics::CharacterSet::ValidSnapshot(ByteSpan(kept_set.Data(), kept_set.Size() - 1)));
    kept_set.Push(byte{0});
    CHECK_FALSE(physics::CharacterSet::ValidSnapshot(ByteSpan(kept_set.Data(), kept_set.Size())));
    kept_set.Pop();
    for (usize i = kept_set.Size() - 4; i < kept_set.Size(); ++i)
    {
        kept_set[i] = byte{0xFF};  // the last height becomes a NaN
    }
    CHECK_FALSE(set.Restore(ByteSpan(kept_set.Data(), kept_set.Size())));
}

}  // namespace events_test
