// The physics bindings (Phase 27 Day 466): components with validation, forces and velocities applied at the merge,
// queries against the tick-start physics state, layers by name, joints, kinematic bodies, the frame order, and the
// results at any thread count.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "physics/height_field.h"
#include "physics/joint_sync.h"
#include "physics/query.h"
#include "physics/sync.h"
#include "physics/system.h"
#include "script/backend_registry.h"
#include "script/bindings.h"
#include "script/command_buffer.h"
#include "script/component_bindings.h"
#include "script/component_catalog.h"
#include "script/entity_bindings.h"
#include "script/luajit/luajit_backend.h"
#include "script/merge.h"
#include "script/merge_services.h"
#include "script/partition_context.h"
#include "script/script_runtime.h"
#include "script_physics/physics_bindings.h"
#include "settings/registry.h"
#include "storage/builtin_components.h"

using namespace vse;
using namespace vse::script;
using namespace vse::script_physics;

namespace
{

// A component the scripts write what they observed into.
struct Probe
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    i32 hits = 0;
};

constexpr u32 kProbeId = 2200;

StringView Text(const char* text)
{
    return StringView(text, CStrLength(text));
}

bool Near(f32 a, f32 b, f32 tolerance = 1.0e-3f)
{
    return Abs(a - b) <= tolerance;
}

physics::Collider BoxCollider(const Vec3& half, u8 layer = 0)
{
    physics::Collider c;
    c.shape = physics::Shape::MakeBox(half);
    c.layer = layer;
    return c;
}

physics::Collider BallCollider(f32 radius, u8 layer = 0)
{
    physics::Collider c;
    c.shape = physics::Shape::MakeSphere(radius);
    c.layer = layer;
    return c;
}

ecs::LocalTransform At(f32 x, f32 y, f32 z)
{
    ecs::LocalTransform t;
    t.position = Vec3(x, y, z);
    return t;
}

physics::RigidBody BodyOf(physics::BodyType type, f32 mass = 0.0f)
{
    physics::RigidBody b;
    b.type = type;
    b.mass = mass;
    return b;
}

// One partition's state with the physics bindings installed, and a physics world to run them against.
struct PhysRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    UniquePtr<State> state = backend->CreateState(0);
    UniquePtr<Installation> installed;
    ecs::World world;
    physics::PhysicsWorld physics;
    PhysicsLayers layers;
    PhysicsScriptServices services;
    CommandKindTable kinds;
    CommandBuffer buffer;
    Overlay overlay;
    PartitionContext context;
    MergeServices merge_services;
    ecs::Entity self;

    explicit PhysRig(bool gravity = false, bool sleeping = false) : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(RegisterPhysicsComponents(&registry));
        RegisterPhysicsValidators(&catalog);
        REQUIRE(RegisterStandardBindings(&bindings));
        REQUIRE(RegisterPhysicsBindings(&bindings));
        Outcome error;
        installed = bindings.Install(state.Get(), InstallProfile::Partition(ContextEntityCheck()), &error);
        REQUIRE(installed.Get() != nullptr);
        REQUIRE(RegisterEntityCommandKinds(&kinds));
        REQUIRE(RegisterComponentCommandKinds(&kinds));
        REQUIRE(RegisterPhysicsCommandKinds(&kinds));
        String<HeapAllocator> why(HeapAllocator::Default());
        REQUIRE(layers.Parse(StringView("default, world, enemy"), &why));
        services.world = &physics;
        services.layers = &layers;
        merge_services.components = &catalog;
        merge_services.extensions[kServicePhysics] = &services;
        physics::StepConfig config;
        config.gravity = gravity ? Vec3(0.0f, -9.81f, 0.0f) : Vec3(0.0f, 0.0f, 0.0f);
        config.allow_sleep = sleeping;
        REQUIRE(physics.SetStepConfig(config) == physics::BodyStatus::Ok);
        buffer.Begin(0);
        self = ecs::CreateTransformEntity(world, ecs::LocalTransform());
        context.world = &world;
        context.commands = &buffer;
        context.overlay = &overlay;
        context.components = &catalog;
        context.current_entity_key = self.ToU64();
        context.extensions[kServicePhysics] = &services;
        catalog.Freeze();
    }

    ecs::Entity Add(const Vec3& at, physics::BodyType type, const physics::Collider& collider, f32 mass = 0.0f)
    {
        return world.Create(BodyOf(type, mass), collider, At(at.x, at.y, at.z));
    }

    void Sync()
    {
        physics::SyncBodies(world, physics);
        physics::SyncJoints(world, physics);
    }

    Outcome Run(const char* body, CallOutput* out, const Value* args = nullptr, usize count = 0)
    {
        String<HeapAllocator> source(HeapAllocator::Default());
        source.Append("function run(a, b, c) ");
        source.Append(StringView(body));
        source.Append(" end");
        REQUIRE(state->Load("physics_test", source.View()).IsOk());
        ScopedPartitionContext scope(&context);
        return state->Call("run", Span<const Value>(args, count), Budget(), out);
    }

    // Runs `body` with entities a, b and c and applies what it recorded.
    Outcome RunAndMerge(const char* body, ecs::Entity a = ecs::Entity(), ecs::Entity b = ecs::Entity(),
                        ecs::Entity c = ecs::Entity())
    {
        const Value args[3] = {Value::FromEntity(a), Value::FromEntity(b), Value::FromEntity(c)};
        CallOutput out;
        Outcome outcome = Run(body, &out, args, 3);
        if (outcome.IsOk())
        {
            CommandBuffer* buffers[] = {&buffer};
            Merge(&world, Span<CommandBuffer* const>(buffers, 1), kinds, ConflictPolicy::LastWins, &merge_services);
        }
        buffer.Begin(0);
        return outcome;
    }

    // The call's results, for the reads.
    Outcome Read(const char* body, CallOutput* out, ecs::Entity a = ecs::Entity(), ecs::Entity b = ecs::Entity())
    {
        const Value args[2] = {Value::FromEntity(a), Value::FromEntity(b)};
        return Run(body, out, args, 2);
    }

    const physics::Body* BodyFor(ecs::Entity e)
    {
        const physics::BodyLink* link = world.Get<physics::BodyLink>(e);
        return link != nullptr ? physics.Find(link->id) : nullptr;
    }
};

}  // namespace

TEST_CASE("Script physics: the components are registered and scripts can add them", "[script_physics][components]")
{
    PhysRig rig;
    const ecs::Entity e = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
    const Outcome outcome = rig.RunAndMerge(
        "component.add(a, 'rigid_body', {type = 2, mass = 4, gravity_scale = 0.5})\n"
        "component.add(a, 'collider', {shape = 1, half_extents = vec3.new(1, 2, 3), friction = 0.25, layer = physics.layer('enemy')})\n"
        "component.add(a, 'velocity', {linear = vec3.new(1, 0, 0)})",
        e);
    REQUIRE(outcome.IsOk());
    const physics::RigidBody* body = rig.world.Get<physics::RigidBody>(e);
    const physics::Collider* collider = rig.world.Get<physics::Collider>(e);
    REQUIRE(body != nullptr);
    REQUIRE(collider != nullptr);
    CHECK(body->type == physics::BodyType::Dynamic);
    CHECK(body->mass == 4.0f);
    CHECK(body->gravity_scale == 0.5f);
    CHECK(collider->shape.kind == physics::ShapeKind::Box);
    CHECK(collider->shape.half_extents.z == 3.0f);
    CHECK(collider->material.friction == 0.25f);
    CHECK(collider->layer == 2);
    rig.Sync();
    const physics::Body* made = rig.BodyFor(e);
    REQUIRE(made != nullptr);
    CHECK(made->linear_velocity.x == 1.0f);
    // And back: a script reads them as tables.
    CallOutput out;
    REQUIRE(
        rig.Read("local c = component.get(a, 'collider') return c.shape, c.half_extents.y, c.layer, c.trigger", &out, e)
            .IsOk());
    REQUIRE(out.results.Size() == 4);
    CHECK(out.results[0].AsInt() == 1);
    CHECK(out.results[1].ToDouble() == 2.0);
    CHECK(out.results[2].AsInt() == 2);
    CHECK(out.results[3].AsBool() == false);
}

TEST_CASE("Script physics: invalid physics components are refused at the call with the physics reason",
          "[script_physics][components]")
{
    PhysRig rig;
    const ecs::Entity e = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
    struct Case
    {
        const char* body;
        const char* reason;
    };
    const Case cases[] = {
        {"component.add(a, 'rigid_body', {mass = -1})", "out of its range"},
        {"component.add(a, 'rigid_body', {gravity_scale = 1 / 0})", "finite"},
        {"component.add(a, 'collider', {friction = -0.5})", "out of its range"},
        {"component.add(a, 'collider', {radius = 0})", "above zero"},
        {"component.add(a, 'collider', {layer = 40})", "out of its range"},
        {"component.add(a, 'collider', {shape = 9})", "inside the field's range"},
        {"component.add(a, 'velocity', {linear = vec3.new(0, 1 / 0, 0)})", "finite"},
    };
    for (const Case& c : cases)
    {
        INFO(c.body);
        const Outcome outcome = rig.RunAndMerge(c.body, e);
        REQUIRE_FALSE(outcome.IsOk());
        INFO(outcome.message.CStr());
        CHECK(outcome.message.View().Contains(Text(c.reason)));
    }
    // A rotation that is not a unit quaternion (a script cannot build one: the host hands it in).
    const Value args[2] = {Value::FromQuat(Quaternion(0.0f, 0.0f, 0.0f, 3.0f)), Value::FromEntity(e)};
    CallOutput out;
    const Outcome skewed = rig.Run("component.add(b, 'collider', {rotation = a})", &out, args, 2);
    REQUIRE_FALSE(skewed.IsOk());
    CHECK(skewed.message.View().Contains("unit quaternion"));
    CHECK_FALSE(rig.world.Has<physics::RigidBody>(e));
    CHECK_FALSE(rig.world.Has<physics::Collider>(e));
}

TEST_CASE("Script physics: forces, impulses and torques act on the next step, velocities at once",
          "[script_physics][forces]")
{
    PhysRig rig;
    const ecs::Entity e = rig.Add(Vec3(0, 0, 0), physics::BodyType::Dynamic, BallCollider(0.5f), 2.0f);
    rig.Sync();
    // A force of 60 N on 2 kg for one 1/60 s step: 0.5 m/s.
    REQUIRE(rig.RunAndMerge("physics.add_force(a, vec3.new(60, 0, 0))", e).IsOk());
    CHECK(rig.BodyFor(e)->linear_velocity.x == 0.0f);
    rig.physics.Step();
    CHECK(Near(rig.BodyFor(e)->linear_velocity.x, 0.5f));
    // An impulse of 4 N s on 2 kg is 2 m/s, at once.
    REQUIRE(rig.RunAndMerge("physics.add_impulse(a, vec3.new(0, 4, 0))", e).IsOk());
    CHECK(Near(rig.BodyFor(e)->linear_velocity.y, 2.0f));
    // An impulse off the centre also turns the body.
    REQUIRE(rig.RunAndMerge("physics.add_impulse_at(a, vec3.new(0, 0, 1), vec3.new(1, 0, 0))", e).IsOk());
    CHECK(rig.BodyFor(e)->angular_velocity.y != 0.0f);
    // A torque acts on the step.
    const f32 before = rig.BodyFor(e)->angular_velocity.x;
    REQUIRE(rig.RunAndMerge("physics.add_torque(a, vec3.new(5, 0, 0))", e).IsOk());
    rig.physics.Step();
    CHECK(rig.BodyFor(e)->angular_velocity.x > before);
    // Velocities are set, the other half kept.
    REQUIRE(rig.RunAndMerge("physics.set_velocity(a, vec3.new(3, 0, 0))", e).IsOk());
    CHECK(rig.BodyFor(e)->linear_velocity.x == 3.0f);
    CHECK(rig.BodyFor(e)->angular_velocity.y != 0.0f);
    REQUIRE(rig.RunAndMerge("physics.set_angular_velocity(a, vec3.new(0, 0, 7))", e).IsOk());
    CHECK(rig.BodyFor(e)->angular_velocity.z == 7.0f);
    CHECK(rig.BodyFor(e)->linear_velocity.x == 3.0f);
    // A script reads the velocity as of the last step.
    CallOutput out;
    REQUIRE(rig.Read("return physics.get_velocity(a).x, physics.get_angular_velocity(a).z", &out, e).IsOk());
    CHECK(out.results[0].ToDouble() == 3.0);
    CHECK(out.results[1].ToDouble() == 7.0);
}

TEST_CASE("Script physics: a push that cannot work is an error at the call", "[script_physics][forces]")
{
    PhysRig rig;
    const ecs::Entity dynamic = rig.Add(Vec3(0, 0, 0), physics::BodyType::Dynamic, BallCollider(0.5f));
    const ecs::Entity floor = rig.Add(Vec3(0, -5, 0), physics::BodyType::Static, BoxCollider(Vec3(5, 1, 5)));
    const ecs::Entity bare = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
    rig.Sync();
    struct Case
    {
        const char* body;
        const char* reason;
    };
    const Case cases[] = {
        {"physics.add_force(a, vec3.new(0, 1 / 0, 0))", "finite"},
        {"physics.add_force(b, vec3.new(0, 1, 0))", "dynamic body"},
        {"physics.add_impulse(c, vec3.new(0, 1, 0))", "no physics body"},
        {"physics.add_torque(b, vec3.new(0, 1, 0))", "dynamic body"},
        {"physics.set_velocity(b, vec3.new(0, 1, 0))", "static body"},
        {"physics.set_gravity_scale(c, 0)", "no `rigid_body`"},
        {"physics.add_force(a, 5)", "Vec3"},
        {"physics.add_force(a, vec3.new(0, 1e13, 0))", "too large"},
        {"physics.add_impulse(a, vec3.new(-1e20, 0, 0))", "too large"},
        {"physics.add_torque(a, vec3.new(0, 0, 1e30))", "too large"},
        {"physics.set_velocity(a, vec3.new(0, 1e6, 0))", "too large"},
        {"physics.set_angular_velocity(a, vec3.new(0, 0, -1e6))", "too large"},
    };
    for (const Case& c : cases)
    {
        INFO(c.body);
        const Outcome outcome = rig.RunAndMerge(c.body, dynamic, floor, bare);
        REQUIRE_FALSE(outcome.IsOk());
        INFO(outcome.message.CStr());
        CHECK(outcome.message.View().Contains(Text(c.reason)));
    }
    CHECK(rig.BodyFor(dynamic)->linear_velocity.y == 0.0f);
}

TEST_CASE("Script physics: the gravity scale changes the component the sync follows", "[script_physics][forces]")
{
    PhysRig rig(true);
    const ecs::Entity floating = rig.Add(Vec3(0, 10, 0), physics::BodyType::Dynamic, BallCollider(0.5f));
    const ecs::Entity falling = rig.Add(Vec3(5, 10, 0), physics::BodyType::Dynamic, BallCollider(0.5f));
    rig.Sync();
    REQUIRE(rig.RunAndMerge("physics.set_gravity_scale(a, 0)", floating).IsOk());
    CHECK(rig.world.Get<physics::RigidBody>(floating)->gravity_scale == 0.0f);
    rig.Sync();
    for (u32 i = 0; i < 30; ++i)
    {
        rig.physics.Step();
    }
    CHECK(Near(static_cast<f32>(rig.BodyFor(floating)->position.y), 10.0f));
    CHECK(rig.BodyFor(falling)->position.y < 9.0f);
}

TEST_CASE("Script physics: sleep and wake", "[script_physics][forces]")
{
    PhysRig rig(false, true);
    const ecs::Entity e = rig.Add(Vec3(0, 0, 0), physics::BodyType::Dynamic, BallCollider(0.5f));
    rig.Sync();
    CHECK_FALSE(rig.physics.IsSleeping(rig.world.Get<physics::BodyLink>(e)->id));
    REQUIRE(rig.RunAndMerge("physics.set_velocity(a, vec3.new(1, 0, 0)) physics.sleep(a)", e).IsOk());
    CHECK(rig.physics.IsSleeping(rig.world.Get<physics::BodyLink>(e)->id));
    CallOutput out;
    REQUIRE(rig.Read("return physics.is_sleeping(a)", &out, e).IsOk());
    CHECK(out.results[0].AsBool());
    REQUIRE(rig.RunAndMerge("physics.wake(a)", e).IsOk());
    CHECK_FALSE(rig.physics.IsSleeping(rig.world.Get<physics::BodyLink>(e)->id));
    // Sleeping a static body is refused by the world; the command is skipped, not an error.
    const ecs::Entity floor = rig.Add(Vec3(0, -5, 0), physics::BodyType::Static, BoxCollider(Vec3(5, 1, 5)));
    rig.Sync();
    CHECK(rig.RunAndMerge("physics.sleep(a)", floor).IsOk());
}

TEST_CASE("Script physics: queries see the tick-start physics world and name layers", "[script_physics][queries]")
{
    PhysRig rig;
    const ecs::Entity wall = rig.Add(Vec3(0, 0, 10), physics::BodyType::Static, BoxCollider(Vec3(2, 2, 1), 2));
    const ecs::Entity crate = rig.Add(Vec3(6, 0, 0), physics::BodyType::Dynamic, BallCollider(1.0f, 1), 1.0f);
    const ecs::Entity caster = rig.Add(Vec3(0, 0, 0), physics::BodyType::Dynamic, BallCollider(0.5f), 1.0f);
    rig.Sync();
    rig.physics.Step();

    CallOutput out;
    REQUIRE(rig.Read("local h = physics.raycast(vec3.new(0, 0, 5), vec3.new(0, 0, 1), 100, nil)\n"
                     "return h.entity, h.distance, h.normal.z, h.point.z",
                     &out, wall)
                .IsOk());
    REQUIRE(out.results.Size() == 4);
    CHECK(out.results[0].AsEntity().ToU64() == wall.ToU64());
    CHECK(Near(static_cast<f32>(out.results[1].ToDouble()), 4.0f));
    CHECK(Near(static_cast<f32>(out.results[2].ToDouble()), -1.0f));
    CHECK(Near(static_cast<f32>(out.results[3].ToDouble()), 9.0f));

    // Layers by name: the wall is on `enemy`.
    REQUIRE(rig.Read("return physics.raycast(vec3.new(0, 0, 5), vec3.new(0, 0, 1), 100, 'world') == nil, "
                     "physics.raycast(vec3.new(0, 0, 5), vec3.new(0, 0, 1), 100, 'enemy') ~= nil, "
                     "physics.raycast(vec3.new(0, 0, 5), vec3.new(0, 0, 1), 100, {'world', 'enemy'}) ~= nil, "
                     "physics.raycast(vec3.new(0, 0, 5), vec3.new(0, 0, 1), 3, nil) == nil",
                     &out)
                .IsOk());
    for (usize i = 0; i < 4; ++i)
    {
        CHECK(out.results[i].AsBool());
    }
    // A ray from inside the caster hits it unless it is ignored.
    REQUIRE(rig.Read("local own = physics.raycast(vec3.new(0, 0, 0), vec3.new(0, 0, 1), 100, nil)\n"
                     "local other = physics.raycast_ex(vec3.new(0, 0, 0), vec3.new(0, 0, 1), 100, nil, a)\n"
                     "return own.entity, other.entity",
                     &out, caster, wall)
                .IsOk());
    CHECK(out.results[0].AsEntity().ToU64() == caster.ToU64());
    CHECK(out.results[1].AsEntity().ToU64() == wall.ToU64());

    // Shape casts, overlaps and the closest point.
    REQUIRE(
        rig.Read("local h = physics.shape_cast({kind = 'sphere', radius = 0.5}, vec3.new(0, 0, 0), quat.identity(), "
                 "vec3.new(1, 0, 0), 20, 'world')\n"
                 "local m = physics.shape_cast({kind = 'sphere', radius = 0.5}, vec3.new(0, 0, 0), quat.identity(), "
                 "vec3.new(1, 0, 0), 20, 'enemy')\n"
                 "return h.entity, h.distance, m == nil",
                 &out)
            .IsOk());
    CHECK(out.results[0].AsEntity().ToU64() == crate.ToU64());
    CHECK(Near(static_cast<f32>(out.results[1].ToDouble()), 4.5f));
    CHECK(out.results[2].AsBool());
    REQUIRE(
        rig.Read("local near = physics.overlap_sphere(vec3.new(6, 0, 0), 1.5, nil)\n"
                 "local none = physics.overlap_sphere(vec3.new(20, 20, 20), 1, nil)\n"
                 "local box = physics.overlap_box(vec3.new(0, 0, 10), vec3.new(3, 3, 3), quat.identity(), 'enemy')\n"
                 "return #near, #none, #box, near[1], box[1]",
                 &out)
            .IsOk());
    CHECK(out.results[0].AsInt() == 1);
    CHECK(out.results[1].AsInt() == 0);
    CHECK(out.results[2].AsInt() == 1);
    CHECK(out.results[3].AsEntity().ToU64() == crate.ToU64());
    CHECK(out.results[4].AsEntity().ToU64() == wall.ToU64());
    REQUIRE(rig.Read("local h = physics.closest_point(vec3.new(6, 5, 0), 10, 'world')\n"
                     "local none = physics.closest_point(vec3.new(6, 5, 0), 1, 'world')\n"
                     "return h.entity, h.distance, none == nil",
                     &out)
                .IsOk());
    CHECK(out.results[0].AsEntity().ToU64() == crate.ToU64());
    CHECK(Near(static_cast<f32>(out.results[1].ToDouble()), 4.0f));
    CHECK(out.results[2].AsBool());
}

TEST_CASE("Script physics: a query does not see this tick's commands", "[script_physics][queries]")
{
    PhysRig rig;
    const ecs::Entity e = rig.Add(Vec3(0, 0, 5), physics::BodyType::Dynamic, BallCollider(0.5f), 1.0f);
    rig.Sync();
    rig.physics.Step();
    CallOutput out;
    // The impulse is recorded, not applied: the velocity the script reads is still the last step's.
    REQUIRE(rig.Read("physics.add_impulse(a, vec3.new(0, 0, 9)) return physics.get_velocity(a).z, "
                     "physics.raycast(vec3.new(0, 0, 0), vec3.new(0, 0, 1), 100, nil).entity",
                     &out, e)
                .IsOk());
    CHECK(out.results[0].ToDouble() == 0.0);
    CHECK(out.results[1].AsEntity().ToU64() == e.ToU64());
}

TEST_CASE("Script physics: bad query arguments are errors that name the problem", "[script_physics][queries]")
{
    PhysRig rig;
    rig.Add(Vec3(0, 0, 5), physics::BodyType::Static, BoxCollider(Vec3(1, 1, 1)));
    rig.Sync();
    rig.physics.Step();
    struct Case
    {
        const char* body;
        const char* reason;
    };
    const Case cases[] = {
        {"physics.raycast(vec3.new(0, 0, 0), vec3.new(0, 0, 0), 10, nil)", "direction that is not zero"},
        {"physics.raycast(vec3.new(0, 0, 0), vec3.new(0, 0, 1), 0, nil)", "above zero"},
        {"physics.raycast(vec3.new(0, 0, 0), vec3.new(0, 0, 1), 10, 'nowhere')", "no physics layer named nowhere"},
        {"physics.raycast(vec3.new(0, 0, 0), vec3.new(0, 0, 1), 10, 7)", "layer name"},
        {"physics.raycast(vec3.new(0, 0, 0), vec3.new(0, 0, 1), 10, {'default', 3})", "named by text"},
        {"physics.shape_cast({kind = 'cone'}, vec3.new(0, 0, 0), quat.identity(), vec3.new(1, 0, 0), 1, nil)",
         "not sphere, box or capsule"},
        {"physics.shape_cast({radius = 1}, vec3.new(0, 0, 0), quat.identity(), vec3.new(1, 0, 0), 1, nil)", "`kind`"},
        {"physics.shape_cast({kind = 'sphere', radius = -1}, vec3.new(0, 0, 0), quat.identity(), vec3.new(1, 0, 0), 1, "
         "nil)",
         "above zero"},
        {"physics.overlap_sphere(vec3.new(0, 0, 0), -1, nil)", "radius above zero"},
        {"physics.overlap_box(vec3.new(0, 0, 0), vec3.new(1, 0, 1), quat.identity(), nil)", "above zero"},
        {"physics.closest_point(vec3.new(0, 0, 0), 0, nil)", "above zero"},
        {"physics.shape_cast({kind = 'sphere', radius = 1}, vec3.new(0, 0, 0), (function() local q = quat.identity() q.w = 0 return q end)(), "
         "vec3.new(1, 0, 0), 1, nil)",
         "non-zero finite quaternion"},
        {"physics.overlap_box(vec3.new(0, 0, 0), vec3.new(1, 1, 1), (function() local q = quat.identity() q.w = 0 return q end)(), nil)",
         "non-zero finite quaternion"},
        {"physics.layer('nowhere')", "no physics layer"},
        {"physics.shape_cast({kind = 'sphere', radius = 'big'}, vec3.new(0, 0, 0), quat.identity(), vec3.new(1, 0, 0), 1, "
         "nil)",
         "the shape's `radius` must be a finite number"},
        {"physics.shape_cast({kind = 'box', half_extents = 3}, vec3.new(0, 0, 0), quat.identity(), vec3.new(1, 0, 0), 1, "
         "nil)",
         "the shape's `half_extents` must be a finite vec3"},
    };
    for (const Case& c : cases)
    {
        INFO(c.body);
        CallOutput out;
        const Outcome outcome = rig.Read(c.body, &out);
        REQUIRE_FALSE(outcome.IsOk());
        INFO(outcome.message.CStr());
        CHECK(outcome.message.View().Contains(Text(c.reason)));
    }
    CallOutput out;
    REQUIRE(
        rig.Read("return physics.layer('enemy'), physics.layer_mask({'world', 'enemy'}), physics.layer_mask(nil)", &out)
            .IsOk());
    CHECK(out.results[0].AsInt() == 2);
    CHECK(out.results[1].AsInt() == 6);
    CHECK(out.results[2].AsInt() == 0xFFFFFFFFll);
}

TEST_CASE("Script physics: a world without physics says so", "[script_physics][queries]")
{
    PhysRig rig;
    rig.context.extensions[kServicePhysics] = nullptr;
    CallOutput out;
    const Outcome outcome = rig.Read("return physics.raycast(vec3.new(0, 0, 0), vec3.new(0, 0, 1), 10, nil)", &out);
    REQUIRE_FALSE(outcome.IsOk());
    CHECK(outcome.message.View().Contains("no physics"));
    // Neither does a world whose services have no layer table.
    rig.context.extensions[kServicePhysics] = &rig.services;
    rig.services.layers = nullptr;
    const Outcome without_layers = rig.Read("return physics.layer('default')", &out);
    REQUIRE_FALSE(without_layers.IsOk());
    CHECK(without_layers.message.View().Contains("no physics"));
}

TEST_CASE("Script physics: layer names parse, with errors for a bad table", "[script_physics][layers]")
{
    PhysicsLayers layers;
    String<HeapAllocator> why(HeapAllocator::Default());
    REQUIRE(layers.Parse(StringView("default,world , player_1"), &why));
    CHECK(layers.Count() == 3);
    u32 index = 0;
    CHECK(layers.Find(StringView("player_1"), &index));
    CHECK(index == 2);
    CHECK_FALSE(layers.Find(StringView("Player_1"), &index));
    CHECK(layers.Parse(StringView(""), &why));
    CHECK(layers.Count() == 1);
    CHECK(layers.NameOf(0) == StringView("default"));
    const char* const bad[] = {"a,,b", "a,a", "9lives", "has space,b", "a-b"};
    for (const char* text : bad)
    {
        INFO(text);
        CHECK_FALSE(layers.Parse(Text(text), &why));
        CHECK(layers.Count() == 1);  // unchanged
        CHECK(why.Length() > 0);
    }
    StringBuilder many(HeapAllocator::Default());
    for (u32 i = 0; i < 33; ++i)
    {
        many.Append(i == 0 ? "l" : ",l").AppendUInt(i);
    }
    CHECK_FALSE(layers.Parse(many.View(), &why));
}

TEST_CASE("Script physics: the layer table is a setting", "[script_physics][layers]")
{
    settings::Registry registry;
    RegisterPhysicsScriptSettings(&registry);
    PhysicsLayers layers;
    REQUIRE(PhysicsLayersFromSettings(registry, &layers));
    CHECK(layers.Count() == 1);
    PhysicsLayers none;
    settings::Registry empty;
    CHECK(PhysicsLayersFromSettings(empty, &none));
    CHECK(none.Count() == 1);
}

TEST_CASE("Script physics: joints are made and removed through the joint component", "[script_physics][joints]")
{
    PhysRig rig;
    const ecs::Entity a = rig.Add(Vec3(0, 0, 0), physics::BodyType::Dynamic, BallCollider(0.5f), 1.0f);
    const ecs::Entity b = rig.Add(Vec3(2, 0, 0), physics::BodyType::Dynamic, BallCollider(0.5f), 1.0f);
    const ecs::Entity owner = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
    rig.Sync();
    REQUIRE(rig.physics.JointCount() == 0);
    REQUIRE(rig.RunAndMerge("physics.add_joint(c, {type = 'distance', a = a, b = b, point_a = vec3.new(0, 0, 0), "
                            "point_b = vec3.new(2, 0, 0), hertz = 4, damping_ratio = 0.5})",
                            a, b, owner)
                .IsOk());
    REQUIRE(rig.world.Has<physics::Joint>(owner));
    CHECK(rig.world.Get<physics::Joint>(owner)->type == physics::JointType::Distance);
    CHECK(rig.world.Get<physics::Joint>(owner)->hertz == 4.0f);
    rig.Sync();
    CHECK(rig.physics.JointCount() == 1);
    // Adding again replaces the description.
    REQUIRE(
        rig.RunAndMerge("physics.add_joint(c, {type = 'ball', a = a, b = b, point_a = vec3.new(1, 0, 0)})", a, b, owner)
            .IsOk());
    CHECK(rig.world.Get<physics::Joint>(owner)->type == physics::JointType::Ball);
    REQUIRE(rig.RunAndMerge("physics.remove_joint(c)", a, b, owner).IsOk());
    CHECK_FALSE(rig.world.Has<physics::Joint>(owner));
    rig.Sync();
    CHECK(rig.physics.JointCount() == 0);
    // A joint to the world.
    REQUIRE(
        rig.RunAndMerge("physics.add_joint(c, {type = 'hinge', a = a, axis = vec3.new(0, 0, 1), enable_limit = true, "
                        "lower_angle = -1, upper_angle = 1})",
                        a, b, owner)
            .IsOk());
    rig.Sync();
    CHECK(rig.physics.JointCount() == 1);
}

TEST_CASE("Script physics: a bad joint description is an error", "[script_physics][joints]")
{
    PhysRig rig;
    const ecs::Entity a = rig.Add(Vec3(0, 0, 0), physics::BodyType::Dynamic, BallCollider(0.5f), 1.0f);
    const ecs::Entity owner = ecs::CreateTransformEntity(rig.world, ecs::LocalTransform());
    rig.Sync();
    struct Case
    {
        const char* body;
        const char* reason;
    };
    const Case cases[] = {
        {"physics.add_joint(c, 5)", "described by a table"},
        {"physics.add_joint(c, {a = a})", "needs a `type`"},
        {"physics.add_joint(c, {type = 'rope', a = a})", "not ball, distance, fixed or hinge"},
        {"physics.add_joint(c, {type = 'ball'})", "at least one body"},
        {"physics.add_joint(c, {type = 'ball', a = 5})", "entity or nil"},
        {"physics.add_joint(c, {type = 'ball', a = a, point_a = 3})", "finite vec3"},
        {"physics.add_joint(c, {type = 'ball', a = a, hertz = 1 / 0})", "finite number"},
        {"physics.add_joint(c, {type = 'ball', a = a, enable_limit = 4})", "true or false"},
        {"physics.add_joint(c, {type = 'distance', a = a, min_length = 3, max_length = 1})", "max_length"},
        {"physics.add_joint(c, {type = 'hinge', a = a, enable_limit = true, lower_angle = 1, upper_angle = -1})",
         "upper_angle"},
        {"physics.remove_joint(c)", "no joint"},
    };
    for (const Case& c : cases)
    {
        INFO(c.body);
        const Outcome outcome = rig.RunAndMerge(c.body, a, ecs::Entity(), owner);
        REQUIRE_FALSE(outcome.IsOk());
        INFO(outcome.message.CStr());
        CHECK(outcome.message.View().Contains(Text(c.reason)));
    }
    CHECK_FALSE(rig.world.Has<physics::Joint>(owner));
}

TEST_CASE("Script physics: a script moves a kinematic body by its transform and the drive carries it",
          "[script_physics][kinematic]")
{
    PhysRig rig;
    const ecs::Entity platform = rig.Add(Vec3(0, 0, 0), physics::BodyType::Kinematic, BoxCollider(Vec3(1, 0.1f, 1)));
    rig.Sync();
    const physics::BodyId platform_id = rig.world.Get<physics::BodyLink>(platform)->id;
    REQUIRE(rig.RunAndMerge("transform.set_position(a, vec3.new(0.5, 0, 0))", platform).IsOk());
    CHECK(rig.physics.Find(platform_id)->position.x == 0.0f);  // not yet: the drive does it
    CHECK(physics::DriveKinematicBodies(rig.world, rig.physics, 1.0f / 60.0f) == 1);
    rig.physics.Step();
    CHECK(Near(static_cast<f32>(rig.physics.Find(platform_id)->position.x), 0.5f));
    CHECK(rig.physics.Find(platform_id)->settings.type == physics::BodyType::Kinematic);
}

TEST_CASE("Script physics: the command kinds are registered once", "[script_physics][commands]")
{
    CommandKindTable kinds;
    REQUIRE(RegisterEntityCommandKinds(&kinds));
    REQUIRE(RegisterPhysicsCommandKinds(&kinds));
    CHECK_FALSE(RegisterPhysicsCommandKinds(&kinds));  // an id is taken
    for (u16 kind = kCommandPhysicsForce; kind <= kCommandPhysicsRemoveJoint; ++kind)
    {
        REQUIRE(kinds.Find(kind) != nullptr);
    }
}

// ------------------------------------------------------------------ the whole runtime

namespace
{

const char* const kRuntimeScripts = R"LUA(
-- Every pusher adds a different force to the one target, every tick: the sum has to come out the same however the
-- partitions ran.
behavior('pusher', {
  on_update = function(self, dt)
    local t = self.params.target
    physics.add_force(t, vec3.new(random.float() - 0.5, random.float() - 0.5, random.float() - 0.5))
    physics.add_impulse(t, vec3.new((random.float() - 0.5) * 0.01, 0, 0))
  end,
})
-- Writes what it sees of the target into its own probe.
behavior('observer', {
  on_update = function(self, dt)
    local v = physics.get_velocity(self.params.target)
    component.set(self.entity, 'probe', { x = v.x, y = v.y })
  end,
})
-- A kick once, on the first update.
behavior('kicker', {
  on_update = function(self, dt)
    physics.add_impulse(self.params.target, vec3.new(1, 0, 0))
  end,
})
-- Casts at the wall and counts the hits; looks around too.
behavior('scout', {
  on_update = function(self, dt)
    local hit = physics.raycast(self.params.origin, vec3.new(0, 0, 1), 50, 'enemy')
    local near = physics.overlap_sphere(self.params.origin, 1.5, nil)
    local probe = component.get(self.entity, 'probe')
    component.set(self.entity, 'probe', { x = hit and hit.distance or -1, hits = probe.hits + #near + (hit and 1 or 0) })
  end,
})
)LUA";

jobs::Scheduler::Config ConfigFor(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

struct RuntimeRig
{
    storage::ComponentRegistry registry;
    ComponentCatalog catalog;
    BindingRegistry bindings;
    UniquePtr<Backend> backend = luajit::CreateLuaJitBackend();
    ecs::World world;
    physics::PhysicsWorld physics;
    PhysicsLayers layers;
    PhysicsScriptServices services;
    UniquePtr<ScriptRuntime> runtime;

    explicit RuntimeRig(u32 partitions) : catalog(&registry)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(RegisterPhysicsComponents(&registry));
        REQUIRE(registry
                    .Register<Probe>({.id = kProbeId,
                                      .name = "probe",
                                      .fields = {storage::Field<&Probe::x>("x"), storage::Field<&Probe::y>("y"),
                                                 storage::Field<&Probe::hits>("hits")}})
                    .Ok());
        RegisterPhysicsValidators(&catalog);
        REQUIRE(RegisterStandardBindings(&bindings));
        REQUIRE(RegisterPhysicsBindings(&bindings));
        String<HeapAllocator> why(HeapAllocator::Default());
        REQUIRE(layers.Parse(StringView("default,world,enemy"), &why));
        services.world = &physics;
        services.layers = &layers;
        ScriptRuntimeConfig config;
        config.backend = backend.Get();
        config.bindings = &bindings;
        config.partitions = partitions;
        config.components = &catalog;
        config.random_seed = 11;
        runtime = MakeUnique<ScriptRuntime>(config);
        runtime->Services().extensions[kServicePhysics] = &services;
        REQUIRE(RegisterPhysicsCommandKinds(&runtime->Kinds()));
        runtime->AddScript("physics_scripts", CopyShared(ByteSpan(reinterpret_cast<const byte*>(kRuntimeScripts),
                                                                  CStrLength(kRuntimeScripts))));
        physics::StepConfig step;
        step.gravity = Vec3(0.0f, 0.0f, 0.0f);
        step.allow_sleep = false;
        REQUIRE(physics.SetStepConfig(step) == physics::BodyStatus::Ok);
    }

    ecs::Entity Body(const Vec3& at, physics::BodyType type, const physics::Collider& collider, f32 mass)
    {
        return world.Create(BodyOf(type, mass), collider, At(at.x, at.y, at.z), Probe());
    }

    void Script(ecs::Entity e, StringView behavior, ecs::Entity target)
    {
        Value params = Value::MakeMap();
        REQUIRE(params.Set("target", Value::FromEntity(target)));
        REQUIRE(world.Add(e, ScriptComponent(behavior, Move(params))) == ecs::Status::Ok);
    }

    // The frame, in the documented order: scripts and the merge, the physics sync, the kinematic drive, the step and
    // the write-back.
    void Frame(jobs::Scheduler* scheduler = nullptr)
    {
        runtime->Tick(&world, 1.0 / 60.0, scheduler);
        physics::SyncBodies(world, physics);
        physics::SyncJoints(world, physics);
        physics::DriveKinematicBodies(world, physics, 1.0f / 60.0f);
        physics.Step();
        physics::WriteBackTransforms(world, physics, 1.0f);
    }
};

}  // namespace

TEST_CASE("Script physics: a script sees the last step's results, what it asks for happens in the next",
          "[script_physics][frame]")
{
    RuntimeRig rig(4);
    const ecs::Entity target = rig.Body(Vec3(0, 0, 0), physics::BodyType::Dynamic, BallCollider(0.5f), 1.0f);
    const ecs::Entity kicker = rig.world.Create(ecs::LocalTransform());
    const ecs::Entity observer = rig.world.Create(ecs::LocalTransform(), Probe());
    rig.Script(kicker, StringView("kicker"), target);
    rig.Script(observer, StringView("observer"), target);
    physics::SyncBodies(rig.world, rig.physics);  // the host's first sync, before the first tick
    rig.world.Get<Probe>(observer)->x = -5.0f;
    for (u32 tick = 1; tick <= 4; ++tick)
    {
        rig.Frame();
        // In tick N the observer read the velocity the step N-1 left: N-1 metres per second (an impulse of 1 per tick
        // on one kilogram); the kicker's impulse of tick N reached the body in the merge of tick N.
        INFO("tick " << tick);
        CHECK(Near(rig.world.Get<Probe>(observer)->x, static_cast<f32>(tick - 1)));
        const physics::Body* body = rig.physics.Find(rig.world.Get<physics::BodyLink>(target)->id);
        CHECK(Near(body->linear_velocity.x, static_cast<f32>(tick)));
    }
    // The write-back moved the entity along with the body.
    CHECK(rig.world.Get<ecs::LocalTransform>(target)->position.x > 0.0f);
}

namespace
{

// A scene of pushers and scouts around a few bodies; returns the physics state hash after `ticks` frames.
u64 RunScene(usize threads, u32 partitions, u32 pushers, u32 ticks, u32* probe_hits = nullptr)
{
    jobs::Scheduler scheduler(ConfigFor(threads));
    RuntimeRig rig(partitions);
    const ecs::Entity target = rig.Body(Vec3(0, 0, 0), physics::BodyType::Dynamic, BallCollider(0.5f), 3.0f);
    const ecs::Entity wall = rig.Body(Vec3(0, 0, 20), physics::BodyType::Static, BoxCollider(Vec3(30, 30, 1), 2), 0.0f);
    (void)wall;
    for (u32 i = 0; i < pushers; ++i)
    {
        const ecs::Entity e = rig.world.Create(ecs::LocalTransform());
        rig.Script(e, StringView("pusher"), target);
    }
    Vector<ecs::Entity> scouts;
    for (u32 i = 0; i < pushers / 4; ++i)
    {
        const ecs::Entity e = rig.world.Create(ecs::LocalTransform(), Probe());
        Value params = Value::MakeMap();
        REQUIRE(params.Set("origin", Value::FromVec3(Vec3(static_cast<f32>(i % 7) - 3.0f, 0.0f, 5.0f))));
        REQUIRE(rig.world.Add(e, ScriptComponent(StringView("scout"), Move(params))) == ecs::Status::Ok);
        scouts.Push(e);
    }
    physics::SyncBodies(rig.world, rig.physics);
    for (u32 t = 0; t < ticks; ++t)
    {
        rig.Frame(&scheduler);
    }
    if (probe_hits != nullptr)
    {
        u32 total = 0;
        for (usize i = 0; i < scouts.Size(); ++i)
        {
            total += static_cast<u32>(rig.world.Get<Probe>(scouts[i])->hits);
        }
        *probe_hits = total;
    }
    REQUIRE(rig.runtime->Stats().errors == 0);
    bool valid = false;
    const u64 hash = rig.physics.StateHash(&valid);
    REQUIRE(valid);
    return hash;
}

}  // namespace

TEST_CASE("Script physics: forces from 64 partitions sum to the same bits at 1 and 16 threads",
          "[script_physics][parallel]")
{
    u32 base_hits = 0;
    const u64 base = RunScene(1, 64, 256, 5, &base_hits);
    CHECK(base != 0);
    CHECK(base_hits > 0);
    const usize counts[] = {2, 3, 7, 16};
    for (usize threads : counts)
    {
        INFO("threads " << threads);
        u32 hits = 0;
        CHECK(RunScene(threads, 64, 256, 5, &hits) == base);
        CHECK(hits == base_hits);
    }
}

TEST_CASE("Script physics: the sum follows the merge order, not an arrangement of the partitions",
          "[script_physics][parallel]")
{
    // The pushers' forces are a function of the entity and the tick only, so the merged sum, in the fixed order of
    // (partition, entity, call), is the same for one partition count run twice, and the run is not an accident of
    // thread timing.
    CHECK(RunScene(8, 64, 128, 4) == RunScene(8, 64, 128, 4));
    // A different partition count merges in a different order: the world still runs (the sum may differ in the last
    // bits, which is why the hash is only compared for equal counts).
    CHECK(RunScene(4, 16, 128, 4) != 0);
}

TEST_CASE("Script physics: queries from many threads at once (the physics world is only read)",
          "[script_physics][parallel]")
{
    jobs::Scheduler scheduler(ConfigFor(16));
    RuntimeRig rig(64);
    rig.Body(Vec3(0, 0, 20), physics::BodyType::Static, BoxCollider(Vec3(60, 60, 1), 2), 0.0f);
    for (u32 i = 0; i < 300; ++i)
    {
        const ecs::Entity e = rig.world.Create(ecs::LocalTransform(), Probe());
        Value params = Value::MakeMap();
        REQUIRE(params.Set("origin", Value::FromVec3(Vec3(static_cast<f32>(i % 11) - 5.0f, 0.0f, 5.0f))));
        REQUIRE(rig.world.Add(e, ScriptComponent(StringView("scout"), Move(params))) == ecs::Status::Ok);
    }
    physics::SyncBodies(rig.world, rig.physics);
    rig.physics.Step();
    for (u32 t = 0; t < 3; ++t)
    {
        rig.Frame(&scheduler);
    }
    CHECK(rig.runtime->Stats().errors == 0);
    u32 checked = 0;
    rig.world.Query<ecs::Read<Probe>, ecs::Read<ScriptComponent>>().ForEach(
        [&](ecs::Entity, const Probe& probe, const ScriptComponent&)
        {
            CHECK(Near(probe.x, 14.0f));  // the wall's near face is at z = 19
            ++checked;
        });
    CHECK(checked == 300);
}

TEST_CASE("Script physics: terrain.height, normal and raycast read the height-field colliders of the tick start",
          "[script_physics][queries][terrain]")
{
    PhysRig rig;
    // A 41 by 21 sample map rising 0.3 m for each metre along x, one metre a cell, centred on its entity at (20, 0,
    // 10).
    Vector<u16> samples;
    for (u32 z = 0; z < 21; ++z)
    {
        for (u32 x = 0; x < 41; ++x)
        {
            samples.Push(static_cast<u16>(x * 65535u / 40u));
        }
    }
    const physics::HeightFieldConstPtr field = physics::HeightField::CreateFromSamples(
        Span<const u16>(samples.Data(), samples.Size()), 41, 21, 0.0f, 12.0f, 1.0f, nullptr);
    REQUIRE(field);
    physics::Collider collider;
    collider.shape = physics::Shape::MakeHeightField(rig.physics.HeightFields().Add(field), field->Extent());
    const ecs::Entity ground = rig.Add(Vec3(20.0f, 0.0f, 10.0f), physics::BodyType::Static, collider);
    rig.Add(Vec3(25.0f, 15.0f, 10.0f), physics::BodyType::Static, BoxCollider(Vec3(1.0f, 1.0f, 1.0f)));
    rig.Sync();
    rig.physics.Step();

    CallOutput out;
    REQUIRE(rig.Read("local n = terrain.normal(25, 10)\n"
                     "local r = terrain.raycast(vec3.new(25, 30, 10), vec3.new(0, -1, 0), 100)\n"
                     "return terrain.height(25, 10), n.y, r.distance, r.entity, r.normal.y,\n"
                     "       terrain.height(500, 10) == nil, terrain.normal(500, 10) == nil,\n"
                     "       terrain.raycast(vec3.new(25, 30, 10), vec3.new(0, -1, 0), 10) == nil",
                     &out)
                .IsOk());
    REQUIRE(out.results.Size() == 8);
    CHECK(Near(static_cast<f32>(out.results[0].ToDouble()), 7.5f));
    CHECK(static_cast<f32>(out.results[1].ToDouble()) > 0.9f);
    CHECK(Near(static_cast<f32>(out.results[2].ToDouble()), 22.5f));  // the terrain, not the box above it
    CHECK(out.results[3].AsEntity().ToU64() == ground.ToU64());
    CHECK(static_cast<f32>(out.results[4].ToDouble()) > 0.9f);
    CHECK(out.results[5].AsBool());
    CHECK(out.results[6].AsBool());
    CHECK(out.results[7].AsBool());

    // Bad arguments are errors that name the problem; a world without physics says so.
    CHECK_FALSE(rig.Read("return terrain.height(0/0, 1)", &out).IsOk());
    CHECK_FALSE(rig.Read("return terrain.raycast(vec3.new(0, 1, 0), vec3.new(0, 0, 0), 5)", &out).IsOk());
    CHECK_FALSE(rig.Read("return terrain.raycast(vec3.new(0, 1, 0), vec3.new(0, -1, 0), 0)", &out).IsOk());
    rig.context.extensions[kServicePhysics] = nullptr;
    CHECK_FALSE(rig.Read("return terrain.height(25, 10)", &out).IsOk());
}
