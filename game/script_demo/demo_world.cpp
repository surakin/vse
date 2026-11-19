// The script demo's world (Phase 27 Day 475). See demo_world.h.
#include "demo_world.h"
#include "containers/hash.h"

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "ecs/transform.h"
#include "os/console.h"
#include "os/os.h"
#include "physics/joint_sync.h"
#include "physics/sync.h"
#include "physics/system.h"
#include "script/backend_registry.h"
#include "script/component_bindings.h"
#include "script/entity_bindings.h"
#include "script/luajit/luajit_backend.h"
#include "script/merge_services.h"
#include "script_physics/physics_events.h"
#include "storage/builtin_components.h"

namespace script_demo
{

namespace
{

constexpr f32 kStep = 1.0f / 60.0f;

physics::RigidBody BodyOf(physics::BodyType type, f32 mass)
{
    physics::RigidBody body;
    body.type = type;
    body.mass = mass;
    return body;
}

physics::Collider BallOf(f32 radius, u8 layer, bool trigger)
{
    physics::Collider collider;
    collider.shape = physics::Shape::MakeSphere(radius);
    collider.layer = layer;
    collider.is_trigger = trigger;
    return collider;
}

ecs::LocalTransform At(f32 x, f32 y, f32 z)
{
    ecs::LocalTransform t;
    t.position = Vec3(x, y, z);
    return t;
}

u64 Mix(u64 h, u64 word)
{
    return Fnv1a64Builder(h).AddWord(word).Value();
}

u64 BitsOf(f32 value)
{
    u32 bits = 0;
    MemCopy(&bits, &value, sizeof(bits));
    return bits;
}

SharedBytesPtr Bytes(StringView text)
{
    return CopyShared(ByteSpan(reinterpret_cast<const byte*>(text.Data()), text.Length()));
}

}  // namespace

DemoWorld::DemoWorld(const DemoOptions& options)
    : options_(options), catalog_(&registry_), backend_(script::luajit::CreateLuaJitBackend())
{
    ENGINE_ASSERT(storage::RegisterBuiltinComponents(&registry_).Ok(), "the builtin components did not register");
    ENGINE_ASSERT(script_physics::RegisterPhysicsComponents(&registry_), "the physics components did not register");
    script_physics::RegisterPhysicsValidators(&catalog_);
    ENGINE_ASSERT(script::RegisterStandardBindings(&bindings_), "the standard bindings did not register");
    ENGINE_ASSERT(script_physics::RegisterPhysicsBindings(&bindings_), "the physics bindings did not register");
    String<HeapAllocator> why(HeapAllocator::Default());
    ENGINE_ASSERT(layers_.Parse(StringView("default,world,player,pickup"), &why), "the layer names did not parse");
    services_.world = &physics_;
    services_.layers = &layers_;

    script::ScriptRuntimeConfig config;
    config.backend = backend_.Get();
    config.bindings = &bindings_;
    config.partitions = options_.partitions;
    config.components = &catalog_;
    config.random_seed = options_.seed;
    config.budget.instructions = 5000000;
    runtime_ = MakeUnique<script::ScriptRuntime>(config);
    runtime_->Services().extensions[script::kServicePhysics] = &services_;
    ENGINE_ASSERT(script_physics::RegisterPhysicsCommandKinds(&runtime_->Kinds()),
                  "the command kinds did not register");
    {
        // As a module with a key, so that hot reload can name it.
        const StringView text(BuiltInScript());
        runtime_->Scripts().AddModule(StringView("demo"), StringView("demo"), Bytes(text), 0, text.Length(),
                                      StringView(), true);
    }
    reloader_ = MakeUnique<script::ScriptReloader>(&runtime_->Scripts(), backend_.Get(), true);
    runtime_->Events().Subscribe(StringView("pickup_collected"), &DemoWorld::OnPickup, this);

    physics::StepConfig step;
    step.gravity = Vec3(0.0f, -9.81f, 0.0f);
    step.allow_sleep = false;
    ENGINE_ASSERT(physics_.SetStepConfig(step) == physics::BodyStatus::Ok, "the step configuration was refused");

    // The floor: a wide static box, its top at y = 0, on the `world` layer.
    {
        physics::Collider floor;
        floor.shape = physics::Shape::MakeBox(Vec3(60.0f, 1.0f, 60.0f));
        floor.layer = 1;
        world_.Create(BodyOf(physics::BodyType::Static, 0.0f), floor, At(0.0f, -1.0f, 0.0f));
    }
    // The crowd: a grid of balls above the floor, behaviors in turn.
    static const char* const kBehaviors[] = {"wander", "flock", "flee"};
    u32 side = 1;
    while (side * side < options_.entities)
    {
        ++side;
    }
    const f32 spacing = 1.2f;
    for (u32 i = 0; i < options_.entities; ++i)
    {
        const f32 x = (static_cast<f32>(i % side) - static_cast<f32>(side) * 0.5f) * spacing;
        const f32 z = (static_cast<f32>(i / side) - static_cast<f32>(side) * 0.5f) * spacing;
        const ecs::Entity e = world_.Create(BodyOf(physics::BodyType::Dynamic, 1.0f), BallOf(0.4f, 0, false),
                                            At(x, 0.5f, z));
        world_.Add(e, script::ScriptComponent(StringView(kBehaviors[i % 3])));
    }
    entity_count_ = options_.entities;
    // The player: a kinematic ball the script walks along a square.
    {
        const ecs::Entity e = world_.Create(BodyOf(physics::BodyType::Kinematic, 0.0f), BallOf(0.5f, 2, false),
                                            At(-8.0f, 0.5f, 8.0f));
        world_.Add(e, script::ScriptComponent(StringView("player")));
    }
    // Pickups: trigger balls on a ring.
    for (u32 i = 0; i < 16; ++i)
    {
        const f32 x = (static_cast<f32>(i % 4) - 1.5f) * 6.0f;
        const f32 z = (static_cast<f32>(i / 4) - 1.5f) * 6.0f;
        const ecs::Entity e = world_.Create(BodyOf(physics::BodyType::Static, 0.0f), BallOf(1.5f, 3, true),
                                            At(x, 0.5f, z));
        script::Value params = script::Value::MakeMap();
        params.Set("id", script::Value::FromInt(static_cast<i64>(i)));
        world_.Add(e, script::ScriptComponent(StringView("pickup"), Move(params)));
    }
    // The planner and the marker it moves.
    marker_ = world_.Create(ecs::LocalTransform());
    {
        const ecs::Entity e = world_.Create(ecs::LocalTransform());
        u32 width = 1;
        while (width * width < options_.field_cells)
        {
            ++width;
        }
        script::Value params = script::Value::MakeMap();
        params.Set("cells", script::Value::FromInt(static_cast<i64>(options_.field_cells)));
        params.Set("width", script::Value::FromInt(static_cast<i64>(width)));
        params.Set("marker", script::Value::FromEntity(marker_));
        world_.Add(e, script::ScriptComponent(StringView("planner"), Move(params)));
    }
    physics::SyncBodies(world_, physics_);
}

DemoWorld::~DemoWorld() = default;

void DemoWorld::OnPickup(void* user, const script::EventRecord&)
{
    ++static_cast<DemoWorld*>(user)->pickups_;
}

void DemoWorld::Frame(jobs::Scheduler* scheduler)
{
    physics::StepConfig step = physics_.GetStepConfig();
    const u32 threads = scheduler != nullptr ? static_cast<u32>(scheduler->ParticipantCount()) : 1u;
    if (step.threads != threads)
    {
        step.threads = threads;
        physics_.SetStepConfig(step);
    }
    physics_.SetScheduler(scheduler);  // every frame: the caller may pass another scheduler, or none
    const u64 t0 = os::MonotonicTimeNanos();
    runtime_->Tick(&world_, static_cast<f64>(kStep), scheduler);
    const u64 t1 = os::MonotonicTimeNanos();
    physics::SyncBodies(world_, physics_);
    physics::SyncJoints(world_, physics_);
    physics::DriveKinematicBodies(world_, physics_, kStep);
    physics_.Step();
    physics::WriteBackTransforms(world_, physics_, 1.0f);
    script_physics::EmitPhysicsEvents(physics_, runtime_.Get(), script_physics::PhysicsEventOptions());
    const u64 t2 = os::MonotonicTimeNanos();
    last_script_seconds_ = static_cast<f64>(t1 - t0) * 1.0e-9;
    last_physics_seconds_ = static_cast<f64>(t2 - t1) * 1.0e-9;
    ++frames_;
}

bool DemoWorld::Reload(StringView text)
{
    Vector<script::ModuleReplacement> changes;
    script::ModuleReplacement r;
    r.key = String<HeapAllocator>(HeapAllocator::Default(), "demo");
    r.bytes = Bytes(text);
    r.size = text.Length();
    changes.Push(Move(r));
    script::ReloadReport report;
    const script::Outcome outcome = reloader_->Reload(
        Span<const script::ModuleReplacement>(changes.Data(), changes.Size()), ++reload_tick_ + frames_, &report);
    if (!outcome.IsOk())
    {
        os::PrintError("script_demo: reload failed: %s\n", outcome.message.CStr());
    }
    else if (!report.applied)
    {
        os::PrintError("script_demo: reload not applied: %u compile failures, %u deferred, run failure '%s'\n",
                       static_cast<unsigned>(report.compile_failures.Size()),
                       static_cast<unsigned>(report.deferred.Size()), report.run_failure.message.CStr());
    }
    return outcome.IsOk() && report.applied && report.compile_failures.Size() == 0;
}

Vec3 DemoWorld::MarkerPosition() const
{
    const ecs::LocalTransform* t = static_cast<const ecs::World&>(world_).Get<ecs::LocalTransform>(marker_);
    return t != nullptr ? t->position : Vec3(0.0f, 0.0f, 0.0f);
}

u64 DemoWorld::StateHash() const
{
    bool valid = false;
    u64 h = physics_.StateHash(&valid);
    h = Mix(h, valid ? 1u : 0u);
    const Vec3 m = MarkerPosition();
    h = Mix(h, BitsOf(m.x));
    h = Mix(h, BitsOf(m.y));
    h = Mix(h, BitsOf(m.z));
    h = Mix(h, pickups_);
    h = Mix(h, runtime_->Stats().updates);
    h = Mix(h, runtime_->Stats().errors);
    return h;
}

}  // namespace script_demo
