// The runtime of the C API (Phase 23 Day 349): worlds and entities behind a property API, the frame, counters and jobs.
// Every function checks its handle and its arguments, copies in and out (no pointer into a world escapes) and returns
// an `engine_status`; nothing of the engine's C++ types, allocations or exceptions crosses the boundary.
#include "engine_c/runtime.h"

#include "engine_c/audio_api.h"
#include "engine_c/surface.h"

#include "animation/components.h"
#include "audio/components.h"
#include "animation/pose.h"
#include "asset/level.h"
#include "algorithm.h"
#include "containers/vector.h"
#include "bytes/byte_writer.h"
#include "cstr/mem_ops.h"
#include "ecs/transform.h"
#include "engine_c/engine_internal.h"
#include "engine_c/handles.h"
#include "engine_c/resources.h"
#include "engine_c/engine_objects.h"
#include "jobs/scheduler.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "jobs/topology.h"
#include "log/diagnostics_hooks.h"
#include "math/quaternion.h"
#include "mem/heap_allocator.h"
#include "os/os.h"
#include "physics/components.h"
#include "physics/joint_sync.h"
#include "physics/query.h"
#include "physics/snapshot_status.h"
#include "physics/sync.h"
#include "physics/system.h"
#include "storage/builtin_components.h"

namespace engine_placeholder::engine_c
{

namespace
{

// The components the C API owns (the transform pair is the ECS's own; MeshReference is in runtime.h).
struct EntityName
{
    u8 length = 0;
    char text[ENGINE_NAME_MAX_BYTES] = {};
};

constexpr usize kMaxSlots = 1u << 16;  // worlds, and counters, each

Atomic<u32> g_generation{0};

bool Finite(float v)
{
    return v - v == 0.0f;
}

const char* NameOfKind(engine_component_kind kind)
{
    switch (kind)
    {
    case ENGINE_COMPONENT_TRANSFORM: return "transform";
    case ENGINE_COMPONENT_WORLD_TRANSFORM: return "world transform";
    case ENGINE_COMPONENT_NAME: return "name";
    case ENGINE_COMPONENT_MESH_REFERENCE: return "mesh reference";
    case ENGINE_COMPONENT_CAMERA: return "camera";
    case ENGINE_COMPONENT_LIGHT: return "light";
    case ENGINE_COMPONENT_VISIBILITY: return "visibility";
    case ENGINE_COMPONENT_SKELETON_INSTANCE: return "skeleton instance";
    case ENGINE_COMPONENT_ANIMATION_PLAYER: return "animation player";
    case ENGINE_COMPONENT_LABEL: return "label";
    case ENGINE_COMPONENT_RIGID_BODY: return "rigid body";
    case ENGINE_COMPONENT_COLLIDER: return "collider";
    case ENGINE_COMPONENT_JOINT: return "joint";
    case ENGINE_COMPONENT_CHARACTER: return "character";
    case ENGINE_COMPONENT_AUDIO_SOURCE: return "audio source";
    case ENGINE_COMPONENT_AUDIO_LISTENER: return "audio listener";
    case ENGINE_COMPONENT_SCRIPT: return "script";
    }
    return "component";
}

bool KnownKind(u32 kind)
{
    return kind >= ENGINE_COMPONENT_TRANSFORM && kind <= ENGINE_COMPONENT_SCRIPT;
}

ecs::ComponentId IdOfKind(engine_component_kind kind)
{
    switch (kind)
    {
    case ENGINE_COMPONENT_TRANSFORM: return ecs::ComponentOf<ecs::LocalTransform>();
    case ENGINE_COMPONENT_WORLD_TRANSFORM: return ecs::ComponentOf<ecs::WorldTransform>();
    case ENGINE_COMPONENT_NAME: return ecs::ComponentOf<EntityName>();
    case ENGINE_COMPONENT_MESH_REFERENCE: return ecs::ComponentOf<MeshReference>();
    case ENGINE_COMPONENT_CAMERA: return ecs::ComponentOf<CameraComponent>();
    case ENGINE_COMPONENT_LIGHT: return ecs::ComponentOf<LightComponent>();
    case ENGINE_COMPONENT_VISIBILITY: return ecs::ComponentOf<VisibilityComponent>();
    case ENGINE_COMPONENT_SKELETON_INSTANCE: return ecs::ComponentOf<animation::SkeletonInstance>();
    case ENGINE_COMPONENT_ANIMATION_PLAYER: return ecs::ComponentOf<animation::AnimationPlayer>();
    case ENGINE_COMPONENT_LABEL: return ecs::ComponentOf<LabelComponent>();
    case ENGINE_COMPONENT_RIGID_BODY: return ecs::ComponentOf<physics::RigidBody>();
    case ENGINE_COMPONENT_COLLIDER: return ecs::ComponentOf<physics::Collider>();
    case ENGINE_COMPONENT_JOINT: return ecs::ComponentOf<physics::Joint>();
    case ENGINE_COMPONENT_CHARACTER: return ecs::ComponentOf<CharacterComponent>();
    case ENGINE_COMPONENT_AUDIO_SOURCE: return ecs::ComponentOf<audio::AudioSource>();
    case ENGINE_COMPONENT_AUDIO_LISTENER: return ecs::ComponentOf<audio::AudioListener>();
    case ENGINE_COMPONENT_SCRIPT: return ScriptComponentId();
    }
    return ecs::kInvalidComponent;
}

// A world the caller has locked (the runtime's world mutex) and the slot it was found in.
engine_status ResolveWorld(Runtime& runtime, engine_handle handle, WorldObject** out)
{
    Runtime::WorldSlot* slot = nullptr;
    const engine_status status = ResolveSlot(runtime.worlds, handle, kKindWorld, "world", &slot);
    *out = status == ENGINE_OK ? slot->object.Get() : nullptr;
    return status;
}

engine_status ResolveEntity(WorldObject& object, u64 key, ecs::Entity* out)
{
    if (key == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the entity key is zero");
    }
    *out = ecs::Entity::FromU64(key);
    if (!object.world.Exists(*out))
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the entity was destroyed");
    }
    return ENGINE_OK;
}

jobs::Job RunHostJob(engine_job_fn fn, void* user, CounterObject* counter, Atomic<u32>* in_flight)
{
    fn(user);
    in_flight->FetchSub(1);
    counter->Done();
    co_return;
}

}  // namespace

WorldObject* FindWorld(Runtime& runtime, engine_handle handle, engine_status* status)
{
    WorldObject* object = nullptr;
    const engine_status resolved = ResolveWorld(runtime, handle, &object);
    if (status != nullptr)
    {
        *status = resolved;
    }
    return resolved == ENGINE_OK ? object : nullptr;
}

u32 NextGeneration()
{
    return g_generation.FetchAdd(1) + 1;
}

u32 LastGeneration()
{
    return g_generation.Load();
}

namespace
{

// After the step: each character's entity takes the position of its capsule's centre; a character whose entity is gone
// gives its body back (Day 409).
void FollowCharacters(WorldObject& object)
{
    physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
    usize kept = 0;
    for (usize i = 0; i < object.characters.Size(); ++i)
    {
        CharacterEntry& entry = object.characters[i];
        const ecs::Entity entity = ecs::Entity::FromU64(entry.key);
        if (!object.world.Exists(entity) || !object.world.Has<CharacterComponent>(entity))
        {
            entry.controller.Destroy(&physics);
            continue;
        }
        const physics::Body* body = physics.Find(entry.controller.BodyHandle());
        const ecs::LocalTransform* transform = object.world.Peek<ecs::LocalTransform>(entity);
        if (body != nullptr && transform != nullptr)
        {
            ecs::LocalTransform moved = *transform;
            moved.position = body->position;
            object.world.Set<ecs::LocalTransform>(entity, moved);
        }
        if (kept != i)
        {
            object.characters[kept] = entry;
        }
        ++kept;
    }
    while (object.characters.Size() > kept)
    {
        object.characters.Pop();
    }
}

}  // namespace

namespace
{

// Stops the voices of a world's audio sources (on the host they play on) and lets go of the host.
void ReleaseWorldAudio(WorldObject& object)
{
    if (object.spatial_audio != nullptr && object.spatial_host.Get() != nullptr)
    {
        LockGuard<Mutex> lock(object.spatial_host->mutex);
        ecs::World nothing;
        object.spatial_audio->Update(nothing, 0.0f);  // no sources: every voice is stopped
    }
    object.spatial_audio = nullptr;
    object.spatial_host = AudioHostPtr();
}

// Places a world's sounds on the engine's audio host, once per frame (a world without an audio component pays nothing).
void UpdateWorldAudio(Runtime& runtime, WorldObject& object, f32 dt)
{
    if (!object.audio_used)
    {
        return;
    }
    AudioHostPtr host;
    {
        LockGuard<Mutex> lock(runtime.audio_mutex);
        host = runtime.audio_owner;
    }
    if (host.Get() != object.spatial_host.Get())
    {  // audio was closed, or closed and opened again: the voices of the old mixer are gone with it
        ReleaseWorldAudio(object);
        if (host.Get() == nullptr)
        {
            return;
        }
        object.spatial_audio = MakeUnique<audio::SpatialAudio>();
        object.spatial_audio->Init(&host->mixer, audio::SpatialAudioConfig());
        object.spatial_host = host;
    }
    if (host.Get() == nullptr)
    {
        return;
    }
    LockGuard<Mutex> lock(host->mutex);
    object.spatial_audio->Update(object.world, dt);
}

}  // namespace

WorldObject::~WorldObject()
{
    ReleaseWorldAudio(*this);
}

WorldObject::WorldObject()
{
    world.SetResource(ecs::TransformReport{});
    world.SetResource(physics::PhysicsWorld());
    schedule.Add(physics::MakePhysicsStepSystem());  // writes the transforms the transform system then reads
    schedule.Add(ecs::MakeTransformSystem("transform"));
    schedule.Build();
}

void CounterObject::Done()
{
    LockGuard<Mutex> lock(mutex);
    if (pending > 0)
    {
        --pending;
    }
    if (pending == 0)
    {
        changed.NotifyAll();  // under the lock: after it is released nothing of this object is touched
    }
}

// ---- the job host

JobHost::JobHost() : thread_(&JobHost::Main, this)
{
    UniqueLock<Mutex> lock(mutex_);
    ready_changed_.Wait(lock, [this] { return ready_; });
}

JobHost::~JobHost()
{
    stop_.Store(1, MemoryOrder::Release);
    thread_.Join();
}

void JobHost::Submit(jobs::Job&& job)
{
    scheduler_->Submit(static_cast<jobs::Job&&>(job));
}

void JobHost::Main(JobHost* self)
{
    jobs::Scheduler::Config config;
    config.participants = jobs::DefaultParticipantCount() < 2 ? 2 : jobs::DefaultParticipantCount();
    config.pin = false;
    {
        jobs::Scheduler scheduler(config);
        {
            LockGuard<Mutex> lock(self->mutex_);
            self->scheduler_ = &scheduler;
            self->ready_ = true;
            self->ready_changed_.NotifyAll();
        }
        // Participant 0 helps with jobs and sleeps in between: it is not the host's thread and has nothing else to do.
        scheduler.HelpUntil(
            [](void* user)
            {
                JobHost* host = static_cast<JobHost*>(user);
                if (host->stop_.Load(MemoryOrder::Acquire) != 0)
                {
                    return true;
                }
                ThisThread::SleepForMicroseconds(200);
                return false;
            },
            self);
    }
    NotifyThreadEnd();  // this thread ends: its frame-pool cache goes back before a library unload
}

Runtime::Runtime() = default;
Runtime::~Runtime() = default;

JobHost& Runtime::EnsureJobs()
{
    LockGuard<Mutex> lock(table_mutex);
    if (jobs.Get() == nullptr)
    {
        jobs = MakeUnique<JobHost>();
    }
    return *jobs;
}

ResourceHost* Runtime::Resources()
{
    LockGuard<Mutex> lock(table_mutex);
    if (resources.Get() == nullptr)
    {
        resources = MakeUnique<ResourceHost>(*this);
    }
    return resources.Get();
}

SurfaceHost* Runtime::Surfaces()
{
    LockGuard<Mutex> lock(table_mutex);
    if (surfaces.Get() == nullptr)
    {
        surfaces = MakeUnique<SurfaceHost>();
    }
    return surfaces.Get();
}

SurfaceHost* Runtime::SurfacesIfCreated()
{
    LockGuard<Mutex> lock(table_mutex);
    return surfaces.Get();
}

ResourceHost* Runtime::ResourcesIfCreated()
{
    LockGuard<Mutex> lock(table_mutex);
    return resources.Get();
}

// ---- animation helpers (Phase 24 Day 363)

namespace
{

// Row-major 3x4 affine product a * b (b first).
animation::Mat3x4 MulAffine(const animation::Mat3x4& a, const animation::Mat3x4& b)
{
    animation::Mat3x4 r;
    for (u32 row = 0; row < 3; ++row)
    {
        for (u32 col = 0; col < 4; ++col)
        {
            f32 sum = col == 3 ? a[row * 4 + 3] : 0.0f;
            for (u32 k = 0; k < 3; ++k)
            {
                sum += a[row * 4 + k] * b[k * 4 + col];
            }
            r[row * 4 + col] = sum;
        }
    }
    return r;
}

Mat4 Mat4FromAffine(const animation::Mat3x4& m)
{
    Mat4 r = Mat4::Identity();
    for (u32 col = 0; col < 4; ++col)
    {
        for (u32 row = 0; row < 3; ++row)
        {
            r.col[col][row] = m[row * 4 + col];
        }
    }
    return r;
}

// The joint's matrix in the skeleton's model space as the last frame left it (the bind pose before any clip played).
bool JointModelMatrix(const animation::AnimationResolver& resolver, const animation::SkeletonInstance& instance,
                      u32 joint, Mat4* out)
{
    const asset::SkeletonView* skeleton = resolver.FindSkeleton(asset::ResourceId::FromValue(instance.skeleton));
    if (skeleton == nullptr || joint >= skeleton->JointCount() || joint >= animation::kMaxPoseJoints)
    {
        return false;
    }
    animation::LocalPose local;
    animation::SetBindPose(*skeleton, &local);
    animation::ModelPose bind;
    animation::ModelFromLocal(*skeleton, local, &bind);
    animation::Mat3x4 model = bind.matrix[joint];
    if (joint < instance.palette.Size())
    {
        model = MulAffine(instance.palette[joint], bind.matrix[joint]);
    }
    *out = Mat4FromAffine(model);
    return true;
}

// Advances the animation of one world before its transforms are propagated.
void AnimateWorld(WorldObject& object, ResourceHost* resources, f32 dt)
{
    if (resources == nullptr)
    {
        return;
    }
    animation::AnimationSystem animator(&resources->Animation());
    animator.Update(&object.world, nullptr, dt);
    // A skeleton with no clip yet is drawn in its bind pose.
    Vector<ecs::Entity> bare;
    auto query = object.world.Query<ecs::Read<animation::SkeletonInstance>>();
    query.ForEach(
        [&](ecs::Entity entity, const animation::SkeletonInstance& instance)
        {
            if (instance.palette.Size() == 0)
            {
                bare.Push(entity);
            }
        });
    for (usize i = 0; i < bare.Size(); ++i)
    {
        animation::SkeletonInstance* instance = object.world.Poke<animation::SkeletonInstance>(bare[i]);
        const asset::SkeletonView* skeleton = resources->Animation().FindSkeleton(
            asset::ResourceId::FromValue(instance->skeleton));
        if (skeleton == nullptr || skeleton->JointCount() == 0 || skeleton->JointCount() > animation::kMaxPoseJoints)
        {
            continue;
        }
        animation::LocalPose local;
        animation::SetBindPose(*skeleton, &local);
        animation::ModelPose model;
        animation::ModelFromLocal(*skeleton, local, &model);
        instance->palette.Resize(skeleton->JointCount());
        animation::SkinningMatrices(*skeleton, model,
                                    Span<animation::Mat3x4>(instance->palette.Data(), skeleton->JointCount()));
    }
}

// Places the entities that follow a joint, after the propagation.
void AttachWorld(WorldObject& object, ResourceHost* resources)
{
    if (resources == nullptr)
    {
        return;
    }
    struct Pending
    {
        ecs::Entity entity;
        JointAttachment attachment;
    };
    Vector<Pending> pending;
    auto query = object.world.Query<ecs::Read<JointAttachment>>();
    query.ForEach([&](ecs::Entity entity, const JointAttachment& attachment)
                  { pending.Push(Pending{entity, attachment}); });
    for (usize i = 0; i < pending.Size(); ++i)
    {
        const ecs::Entity skeleton_entity = ecs::Entity::FromU64(pending[i].attachment.skeleton_key);
        if (!object.world.Exists(skeleton_entity))
        {
            continue;
        }
        const animation::SkeletonInstance* instance = object.world.Peek<animation::SkeletonInstance>(skeleton_entity);
        const ecs::WorldTransform* base = object.world.Peek<ecs::WorldTransform>(skeleton_entity);
        const ecs::LocalTransform* local = object.world.Peek<ecs::LocalTransform>(pending[i].entity);
        Mat4 joint;
        if (instance == nullptr || base == nullptr || local == nullptr ||
            !JointModelMatrix(resources->Animation(), *instance, pending[i].attachment.joint, &joint))
        {
            continue;
        }
        ecs::WorldTransform placed;
        placed.matrix = base->matrix * joint * local->ToMatrix();
        object.world.Set<ecs::WorldTransform>(pending[i].entity, placed);
    }
}

}  // namespace

// ---- the frame

engine_status FrameFn(engine_handle engine, const engine_frame_info* info, engine_frame_stats* stats)
{
    if (info == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`info` is NULL");
    }
    if (StructSizeOf(info) < sizeof(engine_frame_info))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_frame_info.struct_size is smaller than the struct");
    }
    if (stats != nullptr && StructSizeOf(stats) < kFrameStatsFirstSize)
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_frame_stats.struct_size is smaller than the struct");
    }
    if (info->flags != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_frame_info.flags has an unknown bit");
    }
    if (!(info->delta_seconds >= 0.0) || info->delta_seconds > 3600.0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "delta_seconds is negative, not a number or above an hour");
    }
    Runtime* runtime = nullptr;
    const engine_status resolved = ResolveRuntime(engine, &runtime);
    if (resolved != ENGINE_OK)
    {
        return resolved;
    }
    const u64 start = os::MonotonicTimeNanos();
    const u64 budget_ns = info->budget_us > Limits<u64>::Max() / 1000ull ? Limits<u64>::Max()
                                                                         : info->budget_us * 1000ull;
    engine_frame_stats result = {};
    result.struct_size = sizeof(engine_frame_stats);
    if (ResourceHost* resources = runtime->ResourcesIfCreated())
    {
        resources->PumpLoads();  // builds what asynchronous loads have read, within a small budget
    }
    LockGuard<Mutex> lock(runtime->world_mutex);
    result.frame_index = ++runtime->frames;
    const usize count = runtime->worlds.Size();
    const usize first = count == 0 ? 0 : runtime->cursor % count;
    runtime->cursor = 0;
    for (usize i = 0; i < count; ++i)
    {
        const usize index = (first + i) % count;
        WorldObject* object = runtime->worlds[index].object.Get();
        if (object == nullptr)
        {
            continue;
        }
        if (budget_ns != 0 && result.worlds_run > 0 && os::MonotonicTimeNanos() - start >= budget_ns)
        {  // the rest wait for the next frame, which starts with the first of them
            if (result.worlds_skipped == 0)
            {
                runtime->cursor = index;
            }
            ++result.worlds_skipped;
            continue;
        }
        ResourceHost* resources = runtime->ResourcesIfCreated();
        AnimateWorld(*object, resources, static_cast<f32>(info->delta_seconds));
        ScriptTickWorld(*object, info->delta_seconds);  // before the bodies follow the entities (Phase 27 Day 471)
        // The physics bodies follow the entities, then the schedule steps them (before the transform system, which
        // reads what the step wrote back); a world that never had a body component pays nothing (Day 408).
        physics::PhysicsWorld& physics = object->world.Resource<physics::PhysicsWorld>();
        const u64 steps_before = physics.GetStepStats().steps;
        if (object->physics_used)
        {
            const u64 physics_start = os::MonotonicTimeNanos();
            physics::SyncBodies(object->world, physics);
            physics::SyncJoints(object->world, physics);
            physics::DriveKinematicBodies(object->world, physics, static_cast<f32>(info->delta_seconds));
            result.physics_us += (os::MonotonicTimeNanos() - physics_start) / 1000;
        }
        object->schedule.Run(object->world, static_cast<f32>(info->delta_seconds));
        if (object->physics_used)
        {
            physics::SyncJoints(object->world, physics);  // a joint the step broke loses its component now
        }
        FollowCharacters(*object);
        UpdateWorldAudio(*runtime, *object, static_cast<f32>(info->delta_seconds));
        result.physics_steps += static_cast<u32>(physics.GetStepStats().steps - steps_before);
        result.physics_bodies += physics.Stats().bodies;
        AttachWorld(*object, resources);
        ++result.worlds_run;
        result.entities += object->world.Stats().entities;
    }
    if (SurfaceHost* surfaces = runtime->SurfacesIfCreated())
    {
        surfaces->RenderAll(*runtime, static_cast<f32>(info->delta_seconds));  // after the worlds ran (Day 359)
    }
    {
        TextureCounters& counters = runtime->textures.Counters();
        result.textures_uploaded = counters.textures_uploaded.Exchange(0, MemoryOrder::Relaxed);
        result.bytes_uploaded = counters.bytes_uploaded.Exchange(0, MemoryOrder::Relaxed);
        result.updates_replaced = counters.updates_replaced.Exchange(0, MemoryOrder::Relaxed);
        result.slots_reverted = counters.slots_reverted.Exchange(0, MemoryOrder::Relaxed);
    }
    result.elapsed_us = (os::MonotonicTimeNanos() - start) / 1000;
    if (stats != nullptr)
    {
        const u32 written = StructSizeOf(stats) < sizeof(result) ? StructSizeOf(stats)
                                                                 : static_cast<u32>(sizeof(result));
        result.struct_size = written;  // a host built against an older header gets the fields it knows
        MemCopy(stats, &result, written);
    }
    return ENGINE_OK;
}

// ---- worlds and entities

engine_status WorldCreateFn(engine_handle engine, engine_handle* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    out->value = 0;
    Runtime* runtime = nullptr;
    const engine_status resolved = ResolveRuntime(engine, &runtime);
    if (resolved != ENGINE_OK)
    {
        return resolved;
    }
    LockGuard<Mutex> lock(runtime->world_mutex);
    usize slot = runtime->worlds.Size();
    for (usize i = 0; i < runtime->worlds.Size(); ++i)
    {
        if (runtime->worlds[i].object.Get() == nullptr)
        {
            slot = i;
            break;
        }
    }
    if (slot == runtime->worlds.Size() && slot >= kMaxSlots)
    {
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "too many worlds");
    }
    if (!EngineObjectAcquire())
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the engine was destroyed");
    }
    if (slot == runtime->worlds.Size())
    {
        runtime->worlds.Push(Runtime::WorldSlot());
    }
    runtime->worlds[slot].object = MakeUnique<WorldObject>();
    runtime->worlds[slot].object->world.Resource<physics::PhysicsWorld>().SetStepConfig(runtime->physics_config);
    runtime->worlds[slot].generation = NextGeneration();
    *out = MakeObjectHandle(kKindWorld, runtime->worlds[slot].generation, slot);
    return ENGINE_OK;
}

engine_status WorldDestroyFn(engine_handle world)
{
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    UniquePtr<WorldObject> dying;
    {
        LockGuard<Mutex> lock(runtime->world_mutex);
        Runtime::WorldSlot* slot = nullptr;
        const engine_status status = ResolveSlot(runtime->worlds, world, kKindWorld, "world", &slot);
        if (status != ENGINE_OK)
        {
            return status;
        }
        dying = static_cast<UniquePtr<WorldObject>&&>(slot->object);
    }
    dying = nullptr;
    EngineObjectRelease();
    return ENGINE_OK;
}

namespace
{

// Resolves a world and, when `key` is given, one of its entities, under the world mutex (held by `lock`).
template <class Fn> engine_status WithWorld(engine_handle world, bool with_entity, u64 key, Fn&& fn)
{
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    LockGuard<Mutex> lock(runtime->world_mutex);
    WorldObject* object = nullptr;
    engine_status status = ResolveWorld(*runtime, world, &object);
    if (status != ENGINE_OK)
    {
        return status;
    }
    ecs::Entity entity;
    if (with_entity)
    {
        status = ResolveEntity(*object, key, &entity);
        if (status != ENGINE_OK)
        {
            return status;
        }
    }
    return fn(*object, entity);
}

}  // namespace

engine_status EntityCreateFn(engine_handle world, u64* out_key)
{
    if (out_key == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out_key` is NULL");
    }
    *out_key = 0;
    return WithWorld(world, false, 0,
                     [&](WorldObject& object, ecs::Entity)
                     {
                         const ecs::Entity entity = object.world.Create();
                         if (!entity.IsValid())
                         {
                             return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the world refused a new entity");
                         }
                         *out_key = entity.ToU64();
                         return ENGINE_OK;
                     });
}

engine_status EntityDestroyFn(engine_handle world, u64 key)
{
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         return object.world.Destroy(entity) == ecs::Status::Ok
                                    ? ENGINE_OK
                                    : Fail(ENGINE_ERROR_STALE_HANDLE, "the entity was destroyed");
                     });
}

// ---- components

namespace
{

engine_status CheckInStruct(const void* value, usize first_version, const char* what)
{
    if (value == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`value` is NULL");
    }
    if (StructSizeOf(value) < first_version)
    {
        SetLastError("struct_size: %s.struct_size is smaller than the struct", what);
        return ENGINE_ERROR_STRUCT_SIZE;
    }
    return ENGINE_OK;
}

template <class T> engine_status SetOrAdd(ecs::World& world, ecs::Entity entity, T value)
{
    const ecs::Status status = world.Has<T>(entity) ? world.Set<T>(entity, Move(value))
                                                    : world.Add<T>(entity, Move(value));
    return status == ecs::Status::Ok ? ENGINE_OK : Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the world refused the component");
}

}  // namespace

engine_status ComponentSetFn(engine_handle world, u64 key, u32 kind_value, const void* value)
{
    if (!KnownKind(kind_value))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "unknown component kind");
    }
    const engine_component_kind kind = static_cast<engine_component_kind>(kind_value);
    if (kind == ENGINE_COMPONENT_WORLD_TRANSFORM)
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "the world transform is read only: the frame writes it");
    }
    if (kind == ENGINE_COMPONENT_NAME)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "set a name with entity_name_set");
    }
    if (kind == ENGINE_COMPONENT_ANIMATION_PLAYER)
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "the animation player is read only: use animation_play");
    }
    engine_status checked = ENGINE_OK;
    if (kind == ENGINE_COMPONENT_TRANSFORM)
    {
        checked = CheckInStruct(value, sizeof(engine_transform), "engine_transform");
    }
    else if (kind == ENGINE_COMPONENT_SKELETON_INSTANCE)
    {
        checked = CheckInStruct(value, sizeof(engine_skeleton_instance), "engine_skeleton_instance");
    }
    else if (kind == ENGINE_COMPONENT_CAMERA)
    {
        checked = CheckInStruct(value, sizeof(engine_camera), "engine_camera");
    }
    else if (kind == ENGINE_COMPONENT_LIGHT)
    {
        checked = CheckInStruct(value, sizeof(engine_light), "engine_light");
    }
    else if (kind == ENGINE_COMPONENT_VISIBILITY)
    {
        checked = CheckInStruct(value, sizeof(engine_visibility), "engine_visibility");
    }
    else if (kind == ENGINE_COMPONENT_LABEL)
    {
        checked = CheckInStruct(value, sizeof(engine_label), "engine_label");
    }
    else if (kind == ENGINE_COMPONENT_RIGID_BODY)
    {
        checked = CheckInStruct(value, sizeof(engine_rigid_body), "engine_rigid_body");
    }
    else if (kind == ENGINE_COMPONENT_COLLIDER)
    {
        checked = CheckInStruct(value, sizeof(engine_collider), "engine_collider");
    }
    else if (kind == ENGINE_COMPONENT_JOINT)
    {
        checked = CheckInStruct(value, sizeof(engine_joint), "engine_joint");
    }
    else if (kind == ENGINE_COMPONENT_CHARACTER)
    {
        checked = CheckInStruct(value, sizeof(engine_character), "engine_character");
    }
    else if (kind == ENGINE_COMPONENT_AUDIO_SOURCE)
    {
        // The first version ended before `stream`; a host built against it passes a struct without one.
        checked = CheckInStruct(value, offsetof(engine_audio_source, stream), "engine_audio_source");
    }
    else if (kind == ENGINE_COMPONENT_AUDIO_LISTENER)
    {
        checked = CheckInStruct(value, sizeof(engine_audio_listener), "engine_audio_listener");
    }
    else if (kind == ENGINE_COMPONENT_SCRIPT)
    {
        // The params pointer ends the struct: a host of a version with none passes a shorter one.
        checked = CheckInStruct(value, offsetof(engine_script_component, params), "engine_script_component");
    }
    else
    {
        checked = CheckInStruct(value, kMeshReferenceFirstSize, "engine_mesh_reference");
    }
    if (checked != ENGINE_OK)
    {
        return checked;
    }
    return WithWorld(
        world, true, key,
        [&](WorldObject& object, ecs::Entity entity)
        {
            if (kind == ENGINE_COMPONENT_SCRIPT)
            {
                return ScriptComponentSet(*CurrentRuntime(), object, entity, value);
            }
            if (kind == ENGINE_COMPONENT_AUDIO_SOURCE)
            {
                const engine_audio_source* in = static_cast<const engine_audio_source*>(value);
                audio::AudioSource source;
                const engine_status found = AudioClipOfHandle(*CurrentRuntime(), in->clip, &source.clip);
                if (found != ENGINE_OK)
                {
                    return found;
                }
                const u64 stream = StructSizeOf(in) >= sizeof(engine_audio_source) ? in->stream : 0;
                if (stream != 0)
                {
                    if (in->clip != 0)
                    {
                        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "an audio source has a clip or a stream, not both");
                    }
                    const engine_status of_stream = AudioStreamOfHandle(*CurrentRuntime(), stream, &source.stream);
                    if (of_stream != ENGINE_OK)
                    {
                        return of_stream;
                    }
                    bool taken = false;
                    object.world.Query<ecs::Read<audio::AudioSource>>().ForEach(
                        [&](ecs::Entity other, const audio::AudioSource& have)
                        { taken = taken || (other != entity && have.stream.Get() == source.stream.Get()); });
                    if (taken)
                    {
                        return Fail(ENGINE_ERROR_INVALID_ARGUMENT,
                                    "the stream is already the sound of another audio source (it has one reader)");
                    }
                }
                if (in->model > ENGINE_AUDIO_MODEL_EXPONENTIAL || !IsFinite(in->gain) || in->gain < 0.0f ||
                    in->gain > 16.0f || !IsFinite(in->pitch) || in->pitch < 0.25f || in->pitch > 4.0f ||
                    !IsFinite(in->rolloff) || in->rolloff < 0.0f || !IsFinite(in->cone_inner) ||
                    !IsFinite(in->cone_outer) || !IsFinite(in->cone_outer_gain) || in->cone_outer_gain < 0.0f ||
                    in->cone_outer_gain > 1.0f || in->cone_inner < 0.0f || in->cone_outer < in->cone_inner ||
                    (in->flags & ~31u) != 0)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a number of the audio source is out of range");
                }
                source.bus = audio::BusId{in->bus};
                source.gain = in->gain;
                source.pitch = in->pitch;
                source.loop = (in->flags & ENGINE_AUDIO_SOURCE_LOOP) != 0;
                source.play_on_start = (in->flags & ENGINE_AUDIO_SOURCE_PLAY_ON_START) != 0;
                source.spatial = (in->flags & ENGINE_AUDIO_SOURCE_SPATIAL) != 0;
                source.doppler = (in->flags & ENGINE_AUDIO_SOURCE_DOPPLER) != 0;
                source.occlusion = (in->flags & ENGINE_AUDIO_SOURCE_OCCLUSION) != 0;
                source.priority = in->priority;
                source.attenuation.model = static_cast<audio::DistanceModel>(in->model);
                source.attenuation.min_distance = in->min_distance;
                source.attenuation.max_distance = in->max_distance;
                source.attenuation.rolloff = in->rolloff;
                source.cone.inner_angle = in->cone_inner;
                source.cone.outer_angle = in->cone_outer;
                source.cone.outer_gain = in->cone_outer_gain;
                if (!audio::IsValid(source.attenuation))
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the distances of the audio source are not valid");
                }
                object.audio_used = true;
                return SetOrAdd(object.world, entity, source);
            }
            if (kind == ENGINE_COMPONENT_AUDIO_LISTENER)
            {
                const engine_audio_listener* in = static_cast<const engine_audio_listener*>(value);
                if (!IsFinite(in->gain) || in->gain < 0.0f || in->gain > 4.0f)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the listener gain is not in 0 to 4");
                }
                audio::AudioListener listener;
                listener.gain = in->gain;
                listener.active = in->active != 0;
                object.audio_used = true;
                return SetOrAdd(object.world, entity, listener);
            }
            if (kind == ENGINE_COMPONENT_RIGID_BODY)
            {
                const engine_rigid_body* in = static_cast<const engine_rigid_body*>(value);
                if (in->type > ENGINE_BODY_DYNAMIC)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the body type is not known");
                }
                physics::RigidBody body;
                body.type = static_cast<physics::BodyType>(in->type);
                body.flags = in->flags;
                body.mass = in->mass;
                body.linear_damping = in->linear_damping;
                body.angular_damping = in->angular_damping;
                body.gravity_scale = in->gravity_scale;
                if (body.Validate() != physics::Validation::Ok)
                {
                    SetLastError("invalid_argument: the rigid body is refused (%s)",
                                 physics::ValidationName(body.Validate()));
                    return ENGINE_ERROR_INVALID_ARGUMENT;
                }
                object.physics_used = true;
                return SetOrAdd(object.world, entity, body);
            }
            if (kind == ENGINE_COMPONENT_COLLIDER)
            {
                const engine_collider* in = static_cast<const engine_collider*>(value);
                physics::Collider collider;
                if (in->shape == ENGINE_SHAPE_SPHERE)
                {
                    collider.shape = physics::Shape::MakeSphere(in->radius);
                }
                else if (in->shape == ENGINE_SHAPE_BOX)
                {
                    collider.shape = physics::Shape::MakeBox(
                        Vec3(in->half_extents[0], in->half_extents[1], in->half_extents[2]));
                }
                else if (in->shape == ENGINE_SHAPE_CAPSULE)
                {
                    collider.shape = physics::Shape::MakeCapsule(in->half_height, in->radius);
                }
                else
                {
                    return Fail(ENGINE_ERROR_UNSUPPORTED, "the collider shape is not one of the primitives");
                }
                collider.offset = Vec3(in->offset[0], in->offset[1], in->offset[2]);
                collider.rotation = Quaternion(in->rotation[0], in->rotation[1], in->rotation[2], in->rotation[3]);
                collider.material.friction = in->friction;
                collider.material.restitution = in->restitution;
                collider.material.density = in->density;
                collider.layer = in->layer < 32 ? static_cast<u8>(in->layer) : static_cast<u8>(255);
                collider.is_trigger = in->is_trigger != 0;
                collider.group = in->group;
                collider.mask = in->mask;
                if (in->is_trigger > 1 || in->layer >= 32 || collider.Validate() != physics::Validation::Ok)
                {
                    SetLastError("invalid_argument: the collider is refused (%s)",
                                 in->layer >= 32 || in->is_trigger > 1 ? "out_of_range"
                                                                       : physics::ValidationName(collider.Validate()));
                    return ENGINE_ERROR_INVALID_ARGUMENT;
                }
                object.physics_used = true;
                return SetOrAdd(object.world, entity, collider);
            }
            if (kind == ENGINE_COMPONENT_JOINT)
            {
                const engine_joint* in = static_cast<const engine_joint*>(value);
                if (in->kind > ENGINE_JOINT_HINGE || (in->flags & ~7u) != 0)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the joint kind or flags are not known");
                }
                if (in->entity_a == 0 && in->entity_b == 0)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a joint needs at least one entity");
                }
                if ((in->entity_a != 0 && !object.world.Exists(ecs::Entity::FromU64(in->entity_a))) ||
                    (in->entity_b != 0 && !object.world.Exists(ecs::Entity::FromU64(in->entity_b))))
                {
                    return Fail(ENGINE_ERROR_STALE_HANDLE, "an entity of the joint was destroyed");
                }
                if (in->entity_a == in->entity_b)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a joint ties two different entities");
                }
                const float numbers[] = {in->point_a[0],    in->point_a[1],  in->point_a[2],      in->point_b[0],
                                         in->point_b[1],    in->point_b[2],  in->axis[0],         in->axis[1],
                                         in->axis[2],       in->min_length,  in->max_length,      in->hertz,
                                         in->damping_ratio, in->break_force, in->break_torque,    in->lower_angle,
                                         in->upper_angle,   in->motor_speed, in->max_motor_torque};
                for (const float number : numbers)
                {
                    if (!Finite(number))
                    {
                        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the joint has a number that is not finite");
                    }
                }
                if (in->kind == ENGINE_JOINT_HINGE &&
                    in->axis[0] * in->axis[0] + in->axis[1] * in->axis[1] + in->axis[2] * in->axis[2] < 1.0e-6f)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a hinge needs a non-zero axis");
                }
                physics::Joint joint;
                joint.entity_a = in->entity_a == 0 ? ecs::Entity() : ecs::Entity::FromU64(in->entity_a);
                joint.entity_b = in->entity_b == 0 ? ecs::Entity() : ecs::Entity::FromU64(in->entity_b);
                joint.type = static_cast<physics::JointType>(in->kind);
                joint.point_a = Vec3(in->point_a[0], in->point_a[1], in->point_a[2]);
                joint.point_b = Vec3(in->point_b[0], in->point_b[1], in->point_b[2]);
                joint.axis = Vec3(in->axis[0], in->axis[1], in->axis[2]);
                joint.min_length = in->min_length;
                joint.max_length = in->max_length;
                joint.hertz = in->hertz;
                joint.damping_ratio = in->damping_ratio;
                joint.break_force = in->break_force;
                joint.break_torque = in->break_torque;
                joint.collide_connected = (in->flags & ENGINE_JOINT_COLLIDE_CONNECTED) != 0;
                joint.enable_limit = (in->flags & ENGINE_JOINT_LIMIT) != 0;
                joint.lower_angle = in->lower_angle;
                joint.upper_angle = in->upper_angle;
                joint.enable_motor = (in->flags & ENGINE_JOINT_MOTOR) != 0;
                joint.motor_speed = in->motor_speed;
                joint.max_motor_torque = in->max_motor_torque;
                object.physics_used = true;
                // New numbers (or a new pair) for a joint the physics already made: it is made again, as the sync keeps
                // a joint whose two bodies are the same; a joint that broke is not revived by the sync, so its stale
                // link goes too.
                if (const physics::JointLink* link = object.world.Peek<physics::JointLink>(entity))
                {
                    object.world.Resource<physics::PhysicsWorld>().DestroyJoint(link->id);
                    object.world.Remove<physics::JointLink>(entity);
                }
                return SetOrAdd(object.world, entity, joint);
            }
            if (kind == ENGINE_COMPONENT_CHARACTER)
            {
                const engine_character* in = static_cast<const engine_character*>(value);
                const ecs::LocalTransform* transform = object.world.Peek<ecs::LocalTransform>(entity);
                if (transform == nullptr)
                {
                    return Fail(ENGINE_ERROR_MISSING_DEPENDENCY, "a character needs a transform");
                }
                physics::CharacterConfig config;
                config.radius = in->radius;
                config.height = in->height;
                config.step_height = in->step_height;
                config.min_ground_y = in->min_ground_y;
                config.skin_width = in->skin_width;
                config.gravity_scale = in->gravity_scale;
                physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
                usize index = object.characters.Size();
                for (usize i = 0; i < object.characters.Size(); ++i)
                {
                    if (object.characters[i].key == key)
                    {
                        index = i;
                    }
                }
                physics::CharacterController made;  // a refusal leaves the character as it was
                if (made.Create(&physics, config, transform->position) != physics::BodyStatus::Ok)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT,
                                "the character's size or numbers are refused (height at least 2 * radius, all finite)");
                }
                if (index == object.characters.Size())
                {
                    object.characters.Push(CharacterEntry());
                }
                else
                {
                    object.characters[index].controller.Destroy(&physics);
                }
                CharacterEntry& entry = object.characters[index];
                entry.key = key;
                entry.controller = made;
                entry.config = config;
                entry.events = physics::CharacterEvents();
                CharacterComponent component;
                component.radius = config.radius;
                component.height = config.height;
                component.step_height = config.step_height;
                component.min_ground_y = config.min_ground_y;
                component.skin_width = config.skin_width;
                component.gravity_scale = config.gravity_scale;
                return SetOrAdd(object.world, entity, component);
            }
            if (kind == ENGINE_COMPONENT_SKELETON_INSTANCE)
            {
                const engine_skeleton_instance* in = static_cast<const engine_skeleton_instance*>(value);
                if (in->skeleton == 0)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the skeleton id is zero");
                }
                Runtime* runtime = CurrentRuntime();
                const engine_status pinned = runtime->Resources()->PinSkeleton(in->skeleton);
                if (pinned != ENGINE_OK)
                {
                    return pinned;
                }
                const animation::SkeletonInstance* have = object.world.Peek<animation::SkeletonInstance>(entity);
                if (have != nullptr && have->skeleton == in->skeleton)
                {
                    return ENGINE_OK;
                }
                animation::SkeletonInstance instance;
                instance.skeleton = in->skeleton;
                return SetOrAdd(object.world, entity, Move(instance));
            }
            if (kind == ENGINE_COMPONENT_LABEL)
            {
                const engine_label* in = static_cast<const engine_label*>(value);
                if (in->text.length > ENGINE_LABEL_MAX_BYTES || (in->text.data == nullptr && in->text.length != 0))
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the label text is NULL or longer than 256 bytes");
                }
                for (const float c : in->color)
                {
                    if (!(Finite(c) && c >= 0.0f && c <= 1.0f))
                    {
                        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the label colour is not in 0..1");
                    }
                }
                if (!(Finite(in->size) && in->size > 0.0f && in->size <= 100000.0f))
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the label size is not finite and > 0");
                }
                for (const float o : in->offset)
                {
                    if (!Finite(o))
                    {
                        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the label offset is not finite");
                    }
                }
                if (in->anchor > ENGINE_LABEL_ANCHOR_RIGHT || (in->flags & ~ENGINE_LABEL_SIZE_METRES) != 0)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the label anchor or flags are not known");
                }
                LabelComponent label;
                label.length = static_cast<u32>(in->text.length);
                if (in->text.length != 0)
                {
                    MemCopy(label.text, in->text.data, in->text.length);
                }
                MemCopy(label.color, in->color, sizeof(label.color));
                label.size = in->size;
                MemCopy(label.offset, in->offset, sizeof(label.offset));
                label.anchor = in->anchor;
                label.flags = in->flags;
                return SetOrAdd(object.world, entity, label);
            }
            if (kind == ENGINE_COMPONENT_VISIBILITY)
            {
                const engine_visibility* in = static_cast<const engine_visibility*>(value);
                if (in->hidden > 1)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "hidden is 0 or 1");
                }
                VisibilityComponent visibility;
                visibility.hidden = in->hidden;
                return SetOrAdd(object.world, entity, visibility);
            }
            if (kind == ENGINE_COMPONENT_LIGHT)
            {
                const engine_light* in = static_cast<const engine_light*>(value);
                if (in->kind != ENGINE_LIGHT_DIRECTIONAL && in->kind != ENGINE_LIGHT_POINT)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the light kind is not known");
                }
                for (const float c : in->color)
                {
                    if (!(Finite(c) && c >= 0.0f))
                    {
                        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the light colour is not finite and >= 0");
                    }
                }
                if (!(Finite(in->intensity) && in->intensity >= 0.0f))
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the intensity is not finite and >= 0");
                }
                if (in->kind == ENGINE_LIGHT_POINT && !(Finite(in->radius) && in->radius > 0.0f))
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a point light needs radius > 0");
                }
                LightComponent light;
                light.kind = in->kind;
                light.color[0] = in->color[0];
                light.color[1] = in->color[1];
                light.color[2] = in->color[2];
                light.intensity = in->intensity;
                light.radius = in->kind == ENGINE_LIGHT_POINT ? in->radius : 1.0f;
                return SetOrAdd(object.world, entity, light);
            }
            if (kind == ENGINE_COMPONENT_CAMERA)
            {
                const engine_camera* in = static_cast<const engine_camera*>(value);
                if ((in->flags & ~ENGINE_CAMERA_FIXED_ASPECT) != 0)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the camera has an unknown flag bit");
                }
                if (!(Finite(in->fov_y) && in->fov_y > 0.0f && in->fov_y < 3.1415926f))
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "fov_y is not in (0, pi)");
                }
                if (!(Finite(in->near_plane) && in->near_plane > 0.0f && Finite(in->far_plane) &&
                      in->far_plane > in->near_plane))
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the planes need 0 < near_plane < far_plane (finite)");
                }
                if ((in->flags & ENGINE_CAMERA_FIXED_ASPECT) != 0 && !(Finite(in->aspect) && in->aspect > 0.0f))
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a fixed aspect needs aspect > 0");
                }
                CameraComponent camera;
                camera.flags = in->flags;
                camera.fov_y = in->fov_y;
                camera.near_plane = in->near_plane;
                camera.far_plane = in->far_plane;
                camera.aspect = in->aspect;
                return SetOrAdd(object.world, entity, camera);
            }
            if (kind == ENGINE_COMPONENT_MESH_REFERENCE)
            {
                const engine_mesh_reference* in = static_cast<const engine_mesh_reference*>(value);
                MeshReference reference;
                reference.mesh = in->mesh;
                reference.material = in->material;
                if (StructSizeOf(value) >= sizeof(engine_mesh_reference))  // an older host has no material_resource
                {
                    reference.material_resource = in->material_resource;
                }
                return SetOrAdd(object.world, entity, reference);
            }
            const engine_transform* in = static_cast<const engine_transform*>(value);
            for (const float f : in->position)
            {
                if (!Finite(f))
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the position is not finite");
                }
            }
            for (const float f : in->scale)
            {
                if (!Finite(f))
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the scale is not finite");
                }
            }
            for (const float f : in->rotation)
            {
                if (!Finite(f))
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the rotation is not finite");
                }
            }
            Quaternion rotation;
            rotation.x = in->rotation[0];
            rotation.y = in->rotation[1];
            rotation.z = in->rotation[2];
            rotation.w = in->rotation[3];
            const float length_squared = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z +
                                         rotation.w * rotation.w;
            if (!(length_squared > 1e-12f) || !Finite(length_squared))
            {
                return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the rotation has no length");
            }
            const float inverse = 1.0f / Sqrt(length_squared);
            ecs::LocalTransform local;
            local.position = Vec3(in->position[0], in->position[1], in->position[2]);
            local.rotation = Quaternion{rotation.x * inverse, rotation.y * inverse, rotation.z * inverse,
                                        rotation.w * inverse};
            local.scale = Vec3(in->scale[0], in->scale[1], in->scale[2]);
            const bool had_local = object.world.Has<ecs::LocalTransform>(entity);
            const engine_status status = SetOrAdd(object.world, entity, local);
            if (status != ENGINE_OK)
            {
                return status;
            }
            // The frame's transform system writes the world matrix of every entity that has both.
            if (!object.world.Has<ecs::WorldTransform>(entity))
            {
                const engine_status added = SetOrAdd(object.world, entity, ecs::WorldTransform());
                if (added != ENGINE_OK && !had_local)
                {  // a failing call changes nothing: take back the component this call added
                    object.world.Remove<ecs::LocalTransform>(entity);
                }
                return added;
            }
            return ENGINE_OK;
        });
}

#define TRY_STATUS(expression)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status try_status = (expression);                                                                 \
        if (try_status != ENGINE_OK)                                                                                   \
        {                                                                                                              \
            return try_status;                                                                                         \
        }                                                                                                              \
    } while (0)

engine_status EntitySetParentFn(engine_handle world, u64 child_key, u64 parent_key)
{
    return WithWorld(
        world, true, child_key,
        [&](WorldObject& object, ecs::Entity child)
        {
            if (parent_key == 0)
            {
                if (object.world.Has<ecs::Parent>(child))
                {
                    ecs::ClearParent(object.world, child);
                }
                return ENGINE_OK;
            }
            ecs::Entity parent;
            const engine_status found = ResolveEntity(object, parent_key, &parent);
            if (found != ENGINE_OK)
            {
                return found;
            }
            switch (ecs::SetParent(object.world, child, parent))
            {
            case ecs::HierarchyStatus::Ok: return ENGINE_OK;
            case ecs::HierarchyStatus::DeadEntity: return Fail(ENGINE_ERROR_STALE_HANDLE, "an entity was destroyed");
            case ecs::HierarchyStatus::SelfParent:
                return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "an entity cannot be its own parent");
            case ecs::HierarchyStatus::Cycle:
                return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the parent is a descendant of the child: a cycle");
            case ecs::HierarchyStatus::TooDeep:
                return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the hierarchy would be deeper than the engine allows");
            case ecs::HierarchyStatus::Refused: break;
            }
            return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the world refused the parent link");
        });
}

engine_status EntityGetParentFn(engine_handle world, u64 key, u64* parent_key)
{
    if (parent_key == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`parent_key` is NULL");
    }
    *parent_key = 0;
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         const ecs::World& view = object.world;
                         const ecs::Parent* parent = view.Peek<ecs::Parent>(entity);
                         if (parent != nullptr && view.Exists(parent->entity))
                         {
                             *parent_key = parent->entity.ToU64();
                         }
                         return ENGINE_OK;
                     });
}

namespace
{

// Calls `fn(key)` for each entity named `name`, until it returns false.
template <class Fn> void ForEachNamed(ecs::World& world, StringView name, Fn&& fn)
{
    bool go = true;
    auto query = world.Query<ecs::Read<EntityName>>();
    query.ForEach(
        [&](ecs::Entity entity, const EntityName& candidate)
        {
            if (go && StringView(candidate.text, candidate.length) == name)
            {
                go = fn(entity.ToU64());
            }
        });
}

engine_status CheckName(engine_string name)
{
    if (name.data == nullptr && name.length != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a string has a NULL pointer and a length");
    }
    return ENGINE_OK;
}

// The kind a slot name takes, or 0 for a name the G-buffer material does not have.
u32 KindOfSlot(StringView name)
{
    if (name == StringView("base_color") || name == StringView("emissive_color"))
    {
        return ENGINE_SLOT_COLOR;
    }
    if (name == StringView("roughness") || name == StringView("metallic") || name == StringView("alpha_cutoff") ||
        name == StringView("occlusion") || name == StringView("occlusion_strength"))
    {
        return ENGINE_SLOT_FLOAT;
    }
    if (name == StringView("albedo_tex") || name == StringView("normal_tex") ||
        name == StringView("metallic_roughness_tex") || name == StringView("occlusion_tex") ||
        name == StringView("emissive_tex"))
    {
        return ENGINE_SLOT_TEXTURE;
    }
    if (name == StringView("albedo_uv"))
    {
        return ENGINE_SLOT_VEC4;
    }
    if (name == StringView("albedo_uv_outside"))
    {
        return ENGINE_SLOT_COLOR;
    }
    return 0;
}

}  // namespace

engine_status EntityFindFn(engine_handle world, engine_string name, u64* key)
{
    if (key == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`key` is NULL");
    }
    *key = 0;
    TRY_STATUS(CheckName(name));
    return WithWorld(world, false, 0,
                     [&](WorldObject& object, ecs::Entity)
                     {
                         u64 found = 0;
                         ForEachNamed(object.world, StringView(name.data, name.length),
                                      [&](u64 candidate)
                                      {
                                          found = candidate;
                                          return false;
                                      });
                         if (found == 0)
                         {
                             return Fail(ENGINE_ERROR_NOT_FOUND, "no entity has that name");
                         }
                         *key = found;
                         return ENGINE_OK;
                     });
}

engine_status EntityFindAllFn(engine_handle world, engine_string name, u64* keys, size_t capacity, size_t* total)
{
    if (total == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`total` is NULL");
    }
    *total = 0;
    if (keys == nullptr && capacity != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`keys` is NULL with a capacity above zero");
    }
    TRY_STATUS(CheckName(name));
    return WithWorld(world, false, 0,
                     [&](WorldObject& object, ecs::Entity)
                     {
                         usize count = 0;
                         ForEachNamed(object.world, StringView(name.data, name.length),
                                      [&](u64 candidate)
                                      {
                                          if (count < capacity)
                                          {
                                              keys[count] = candidate;
                                          }
                                          ++count;
                                          return true;
                                      });
                         *total = count;
                         return ENGINE_OK;
                     });
}

u64 Runtime::CopyLabelFont(Vector<u8>* out)
{
    LockGuard<Mutex> lock(label_font_mutex_);
    out->Resize(label_font_.Size());
    if (label_font_.Size() != 0)
    {
        MemCopy(out->Data(), label_font_.Data(), label_font_.Size());
    }
    return label_font_version_;
}

void Runtime::SetLabelFont(const void* data, usize size)
{
    LockGuard<Mutex> lock(label_font_mutex_);
    label_font_.Resize(size);
    MemCopy(label_font_.Data(), data, size);
    ++label_font_version_;
}

engine_status LabelFontSetFn(engine_handle engine, const void* data, size_t size)
{
    Runtime* runtime = nullptr;
    TRY_STATUS(ResolveRuntime(engine, &runtime));
    if (data == nullptr || size == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the font data is NULL or empty");
    }
    if (size > (usize{32} << 20))
    {
        return Fail(ENGINE_ERROR_TOO_LARGE, "the font file is above 32 MiB");
    }
    runtime->SetLabelFont(data, size);
    return ENGINE_OK;
}

// Sets an override of the material slot `slot` on `key`'s mesh slot `mesh_slot` (SlotOverrides::kAllMeshSlots: all).
// With a name, `mesh_slot_name` names the mesh slot instead (looked up in the entity's mounted mesh).
engine_status SetSlotOverride(engine_handle world, u64 key, engine_string slot, const engine_slot_override* value,
                              u32 mesh_slot, const StringView* mesh_slot_name)
{
    TRY_STATUS(CheckName(slot));
    TRY_STATUS(CheckInStruct(value, sizeof(engine_slot_override), "engine_slot_override"));
    const StringView name(slot.data, slot.length);
    const u32 expected = KindOfSlot(name);
    if (expected == 0)
    {
        SetLastError("invalid_argument: the material has no slot '%.*s' (base_color, roughness, metallic, "
                     "emissive_color, alpha_cutoff, occlusion, occlusion_strength, albedo_tex, normal_tex, "
                     "metallic_roughness_tex, occlusion_tex, emissive_tex, albedo_uv, albedo_uv_outside)",
                     static_cast<int>(slot.length < 40 ? slot.length : 40), slot.data != nullptr ? slot.data : "");
        return ENGINE_ERROR_INVALID_ARGUMENT;
    }
    const bool host_texture = expected == ENGINE_SLOT_TEXTURE && value->kind == ENGINE_SLOT_HOST_TEXTURE;
    if (value->kind != expected && !host_texture)
    {
        SetLastError("invalid_argument: the slot '%.*s' takes another kind of value", static_cast<int>(slot.length),
                     slot.data);
        return ENGINE_ERROR_INVALID_ARGUMENT;
    }
    for (const float f : value->value)
    {
        if (!Finite(f))
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the override value is not finite");
        }
    }
    if (host_texture)
    {
        Runtime* runtime = CurrentRuntime();
        if (runtime == nullptr || runtime->textures.Find(value->host_texture).Get() == nullptr)
        {
            return Fail(ENGINE_ERROR_STALE_HANDLE, "the host texture does not exist (destroyed, or not a texture)");
        }
    }
    else if (expected == ENGINE_SLOT_TEXTURE && value->texture_resource == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a texture override needs a texture resource id");
    }
    return WithWorld(
        world, true, key,
        [&](WorldObject& object, ecs::Entity entity)
        {
            if (mesh_slot_name != nullptr)
            {
                const MeshReference* reference = object.world.Peek<MeshReference>(entity);
                if (reference == nullptr || reference->mesh == 0)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the entity has no mesh reference");
                }
                const engine_status found = CurrentRuntime()->Resources()->FindMeshSlot(reference->mesh,
                                                                                        *mesh_slot_name, &mesh_slot);
                if (found != ENGINE_OK)
                {
                    return found;
                }
            }
            if (mesh_slot != SlotOverrides::kAllMeshSlots && mesh_slot >= SlotMaterialsComponent::kMax)
            {
                return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a mesh slot is numbered below 16");
            }
            SlotOverrides overrides;
            if (const SlotOverrides* have = object.world.Peek<SlotOverrides>(entity))
            {
                overrides = *have;
            }
            u32 at = overrides.count;
            for (u32 i = 0; i < overrides.count; ++i)
            {
                if (StringView(overrides.items[i].name) == name && overrides.items[i].mesh_slot == mesh_slot)
                {
                    at = i;
                }
            }
            if (at == SlotOverrides::kMax)
            {
                return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "an entity holds at most 8 slot overrides");
            }
            SlotOverrides::Item& item = overrides.items[at];
            item = SlotOverrides::Item();
            item.mesh_slot = mesh_slot;
            MemCopy(item.name, name.Data(), name.Length());
            item.kind = value->kind;
            MemCopy(item.value, value->value, sizeof(item.value));
            item.texture = value->texture_resource;
            item.host_texture = host_texture ? value->host_texture.value : 0;
            if (at == overrides.count)
            {
                ++overrides.count;
            }
            return SetOrAdd(object.world, entity, overrides);
        });
}

engine_status EntitySetSlotOverrideFn(engine_handle world, u64 key, engine_string slot,
                                      const engine_slot_override* value)
{
    return SetSlotOverride(world, key, slot, value, SlotOverrides::kAllMeshSlots, nullptr);
}

engine_status EntitySetMeshSlotOverrideFn(engine_handle world, u64 key, engine_string mesh_slot, engine_string slot,
                                          const engine_slot_override* value)
{
    TRY_STATUS(CheckName(mesh_slot));
    if (mesh_slot.length == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the mesh slot name is empty");
    }
    const StringView mesh_name(mesh_slot.data, mesh_slot.length);
    return SetSlotOverride(world, key, slot, value, SlotOverrides::kAllMeshSlots, &mesh_name);
}

engine_status EntityClearSlotOverrideFn(engine_handle world, u64 key, engine_string slot)
{
    TRY_STATUS(CheckName(slot));
    const StringView name(slot.data, slot.length);
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         const SlotOverrides* have = object.world.Peek<SlotOverrides>(entity);
                         if (name.Length() == 0)
                         {
                             if (have != nullptr)
                             {
                                 object.world.Remove<SlotOverrides>(entity);
                             }
                             return ENGINE_OK;
                         }
                         if (have == nullptr)
                         {
                             return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no override of that slot");
                         }
                         SlotOverrides overrides = *have;
                         bool removed = false;
                         for (u32 i = overrides.count; i > 0; --i)  // every override of that slot, of any mesh slot
                         {
                             if (StringView(overrides.items[i - 1].name) == name)
                             {
                                 overrides.items[i - 1] = overrides.items[overrides.count - 1];
                                 overrides.items[overrides.count - 1] = SlotOverrides::Item();
                                 --overrides.count;
                                 removed = true;
                             }
                         }
                         if (!removed)
                         {
                             return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no override of that slot");
                         }
                         if (overrides.count == 0)
                         {
                             object.world.Remove<SlotOverrides>(entity);
                             return ENGINE_OK;
                         }
                         return SetOrAdd(object.world, entity, overrides);
                     });
}

// Sets or clears slot `slot` of an entity's slot materials.
engine_status SetMeshSlotMaterial(WorldObject& object, ecs::Entity entity, u32 slot, u64 material)
{
    if (slot >= SlotMaterialsComponent::kMax)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a mesh slot is numbered below 16");
    }
    SlotMaterialsComponent slots;
    if (const SlotMaterialsComponent* have = object.world.Peek<SlotMaterialsComponent>(entity))
    {
        slots = *have;
    }
    slots.material[slot] = material;
    slots.count = 0;
    for (u32 i = 0; i < SlotMaterialsComponent::kMax; ++i)
    {
        if (slots.material[i] != 0)
        {
            slots.count = i + 1;
        }
    }
    if (slots.count == 0)
    {
        if (object.world.Peek<SlotMaterialsComponent>(entity) != nullptr)
        {
            object.world.Remove<SlotMaterialsComponent>(entity);
        }
        return ENGINE_OK;
    }
    return SetOrAdd(object.world, entity, slots);
}

engine_status EntitySetMeshSlotMaterialFn(engine_handle world, u64 key, u32 slot, u64 material_resource)
{
    return WithWorld(world, true, key, [&](WorldObject& object, ecs::Entity entity)
                     { return SetMeshSlotMaterial(object, entity, slot, material_resource); });
}

engine_status EntitySetMeshSlotMaterialByNameFn(engine_handle world, u64 key, engine_string slot, u64 material_resource)
{
    TRY_STATUS(CheckName(slot));
    if (slot.length == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the slot name is empty");
    }
    const StringView name(slot.data, slot.length);
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         const MeshReference* reference = object.world.Peek<MeshReference>(entity);
                         if (reference == nullptr || reference->mesh == 0)
                         {
                             return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the entity has no mesh reference");
                         }
                         Runtime* runtime = CurrentRuntime();
                         u32 index = 0;
                         const engine_status found = runtime->Resources()->FindMeshSlot(reference->mesh, name, &index);
                         if (found != ENGINE_OK)
                         {
                             return found;
                         }
                         return SetMeshSlotMaterial(object, entity, index, material_resource);
                     });
}

engine_status EntityClearMeshSlotMaterialsFn(engine_handle world, u64 key)
{
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         if (object.world.Peek<SlotMaterialsComponent>(entity) != nullptr)
                         {
                             object.world.Remove<SlotMaterialsComponent>(entity);
                         }
                         return ENGINE_OK;
                     });
}

engine_status AnimationPlayFn(engine_handle world, u64 key, u64 clip, const engine_play_params* params)
{
    engine_play_params play = {};
    play.struct_size = sizeof(engine_play_params);
    if (params != nullptr)
    {
        TRY_STATUS(CheckInStruct(params, sizeof(engine_play_params), "engine_play_params"));
        play = *params;
    }
    if (clip == 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the clip id is zero");
    }
    if ((play.flags & ~ENGINE_PLAY_LOOP) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_play_params.flags has an unknown bit");
    }
    if (!(Finite(play.speed) && play.speed >= 0.0f) || !(Finite(play.start_time) && play.start_time >= 0.0f) ||
        !(Finite(play.fade_seconds) && play.fade_seconds >= 0.0f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "speed, start_time and fade_seconds are finite and >= 0");
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         const animation::SkeletonInstance* instance = object.world.Peek<animation::SkeletonInstance>(
                             entity);
                         if (instance == nullptr)
                         {
                             return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the entity has no skeleton instance");
                         }
                         Runtime* runtime = CurrentRuntime();
                         TRY_STATUS(runtime->Resources()->PinClip(clip, instance->skeleton));
                         animation::AnimationPlayer player;
                         if (animation::AnimationPlayer* old = object.world.Poke<animation::AnimationPlayer>(entity))
                         {
                             if (play.fade_seconds > 0.0f && old->clip != 0 && old->clip != clip)
                             {
                                 player.fade_clip = old->clip;
                                 player.fade_time = old->time;
                                 player.fade_total = play.fade_seconds;
                                 player.fade_elapsed = 0.0f;
                                 player.fade_sampler = Move(old->sampler);
                             }
                         }
                         player.clip = clip;
                         player.time = play.start_time;
                         player.speed = play.speed == 0.0f ? 1.0f : play.speed;
                         player.playing = true;
                         player.looping = (play.flags & ENGINE_PLAY_LOOP) != 0;
                         return SetOrAdd(object.world, entity, Move(player));
                     });
}

engine_status AnimationSetSpeedFn(engine_handle world, u64 key, float speed)
{
    if (!(Finite(speed) && speed >= 0.0f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the speed is finite and >= 0");
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         animation::AnimationPlayer* player = object.world.Poke<animation::AnimationPlayer>(entity);
                         if (player == nullptr)
                         {
                             return Fail(ENGINE_ERROR_NOT_FOUND, "no animation was started on the entity");
                         }
                         player->speed = speed;
                         return ENGINE_OK;
                     });
}

engine_status AnimationStopFn(engine_handle world, u64 key)
{
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         animation::AnimationPlayer* player = object.world.Poke<animation::AnimationPlayer>(entity);
                         if (player == nullptr)
                         {
                             return Fail(ENGINE_ERROR_NOT_FOUND, "no animation was started on the entity");
                         }
                         player->playing = false;
                         return ENGINE_OK;
                     });
}

engine_status AnimationTimeFn(engine_handle world, u64 key, double* seconds)
{
    if (seconds == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`seconds` is NULL");
    }
    *seconds = 0.0;
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         const ecs::World& view = object.world;
                         const animation::AnimationPlayer* player = view.Peek<animation::AnimationPlayer>(entity);
                         if (player == nullptr)
                         {
                             return Fail(ENGINE_ERROR_NOT_FOUND, "no animation was started on the entity");
                         }
                         *seconds = static_cast<double>(player->time);
                         return ENGINE_OK;
                     });
}

engine_status SkeletonFindJointFn(engine_handle world, u64 key, engine_string name, u32* index)
{
    if (index == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`index` is NULL");
    }
    *index = 0;
    TRY_STATUS(CheckName(name));
    return WithWorld(
        world, true, key,
        [&](WorldObject& object, ecs::Entity entity)
        {
            const ecs::World& view = object.world;
            const animation::SkeletonInstance* instance = view.Peek<animation::SkeletonInstance>(entity);
            if (instance == nullptr)
            {
                return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no skeleton instance");
            }
            Runtime* runtime = CurrentRuntime();
            ResourceHost* resources = runtime->ResourcesIfCreated();
            const asset::SkeletonView* skeleton = resources == nullptr
                                                      ? nullptr
                                                      : resources->Animation().FindSkeleton(
                                                            asset::ResourceId::FromValue(instance->skeleton));
            if (skeleton == nullptr || !skeleton->FindJoint(StringView(name.data, name.length), index))
            {
                return Fail(ENGINE_ERROR_NOT_FOUND, "the skeleton has no joint of that name");
            }
            return ENGINE_OK;
        });
}

engine_status SkeletonJointTransformFn(engine_handle world, u64 key, u32 joint, float* matrix)
{
    if (matrix == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`matrix` is NULL");
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         const ecs::World& view = object.world;
                         const animation::SkeletonInstance* instance = view.Peek<animation::SkeletonInstance>(entity);
                         if (instance == nullptr)
                         {
                             return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no skeleton instance");
                         }
                         Runtime* runtime = CurrentRuntime();
                         ResourceHost* resources = runtime->ResourcesIfCreated();
                         Mat4 model;
                         if (resources == nullptr ||
                             !JointModelMatrix(resources->Animation(), *instance, joint, &model))
                         {
                             return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the joint does not exist");
                         }
                         static_assert(sizeof(Mat4) == 16 * sizeof(float), "a Mat4 is sixteen floats");
                         MemCopy(matrix, &model, sizeof(model));
                         return ENGINE_OK;
                     });
}

engine_status EntityAttachToJointFn(engine_handle world, u64 key, u64 skeleton_key, u32 joint)
{
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         if (skeleton_key == 0)
                         {
                             if (object.world.Has<JointAttachment>(entity))
                             {
                                 object.world.Remove<JointAttachment>(entity);
                                 // the next propagation writes the world matrix again
                                 if (const ecs::LocalTransform* local = object.world.Peek<ecs::LocalTransform>(entity))
                                 {
                                     ecs::LocalTransform again = *local;
                                     object.world.Set<ecs::LocalTransform>(entity, again);
                                 }
                             }
                             return ENGINE_OK;
                         }
                         if (skeleton_key == key)
                         {
                             return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "an entity cannot follow a joint of itself");
                         }
                         ecs::Entity skeleton_entity;
                         TRY_STATUS(ResolveEntity(object, skeleton_key, &skeleton_entity));
                         const animation::SkeletonInstance* instance = object.world.Peek<animation::SkeletonInstance>(
                             skeleton_entity);
                         if (instance == nullptr)
                         {
                             return Fail(ENGINE_ERROR_NOT_FOUND, "the skeleton entity has no skeleton instance");
                         }
                         Runtime* runtime = CurrentRuntime();
                         ResourceHost* resources = runtime->ResourcesIfCreated();
                         Mat4 unused;
                         if (resources == nullptr ||
                             !JointModelMatrix(resources->Animation(), *instance, joint, &unused))
                         {
                             return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the joint does not exist");
                         }
                         JointAttachment attachment;
                         attachment.skeleton_key = skeleton_key;
                         attachment.joint = joint;
                         return SetOrAdd(object.world, entity, attachment);
                     });
}

engine_status LevelSpawnFn(engine_handle world, u64 level, const engine_spawn_params* params, u64* root, u64* keys,
                           size_t capacity, size_t* total)
{
    if (root == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`root` is NULL");
    }
    *root = 0;
    if (total != nullptr)
    {
        *total = 0;
    }
    if (keys == nullptr && capacity != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`keys` is NULL with a capacity above zero");
    }
    engine_spawn_params place = {};
    place.struct_size = sizeof(engine_spawn_params);
    place.rotation[3] = 1.0f;
    place.scale = 1.0f;
    if (params != nullptr)
    {
        TRY_STATUS(CheckInStruct(params, sizeof(engine_spawn_params), "engine_spawn_params"));
        place = *params;
        if (place.flags != 0)
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "engine_spawn_params.flags has an unknown bit");
        }
        if (place.rotation[0] == 0.0f && place.rotation[1] == 0.0f && place.rotation[2] == 0.0f &&
            place.rotation[3] == 0.0f)
        {
            place.rotation[3] = 1.0f;
        }
        if (place.scale == 0.0f)
        {
            place.scale = 1.0f;
        }
        for (const float v : place.position)
        {
            if (!Finite(v))
            {
                return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the position is not finite");
            }
        }
        const float length2 = place.rotation[0] * place.rotation[0] + place.rotation[1] * place.rotation[1] +
                              place.rotation[2] * place.rotation[2] + place.rotation[3] * place.rotation[3];
        if (!Finite(length2) || !(length2 > 1e-12f) || !(Finite(place.scale) && place.scale > 0.0f))
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT,
                        "the rotation is zero or not finite, or the scale is not above 0");
        }
        if ((place.name_prefix.data == nullptr && place.name_prefix.length != 0) || place.name_prefix.length > 15)
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "name_prefix is at most 15 bytes");
        }
    }
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    ResourceHost* resources = runtime->ResourcesIfCreated();
    if (resources == nullptr)
    {
        return Fail(ENGINE_ERROR_NOT_FOUND, "the level is not mounted: no resource was added");
    }
    // The level, read and checked before the world is touched.
    Vector<byte> blob;
    TRY_STATUS(resources->ReadLevel(level, &blob));
    storage::ComponentRegistry registry;
    if (storage::RegisterBuiltinComponents(&registry).Failed() || asset::RegisterLevelComponents(&registry).Failed() ||
        animation::RegisterAnimationComponents(&registry).Failed())
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "the level components could not be registered");
    }
    ecs::World scratch;
    storage::KeyIndex index;
    const asset::Status loaded = asset::LoadLevelBlob(ByteSpan(blob.Data(), blob.Size()), resources->Types(), registry,
                                                      scratch, index);
    if (loaded.Failed())
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, loaded.Message());
    }
    Vector<ecs::Entity> source;
    {
        auto query = scratch.Query<ecs::Read<ecs::LocalTransform>>();
        query.ForEach([&](ecs::Entity entity, const ecs::LocalTransform&) { source.Push(entity); });
    }
    // The skeletons the level names are loaded before anything is made.
    for (usize i = 0; i < source.Size(); ++i)
    {
        if (const animation::SkeletonInstance* instance = scratch.Peek<animation::SkeletonInstance>(source[i]))
        {
            TRY_STATUS(resources->PinSkeleton(instance->skeleton));
        }
    }
    const StringView prefix(place.name_prefix.data, place.name_prefix.length);
    return WithWorld(
        world, false, 0,
        [&](WorldObject& object, ecs::Entity)
        {
            Vector<ecs::Entity> made;  // the root first, then the level's entities in `source` order
            auto undo = [&]()
            {
                for (usize i = made.Size(); i > 0; --i)
                {
                    object.world.Destroy(made[i - 1]);
                }
            };
            auto name_of = [&](StringView own, EntityName* out)
            {
                usize length = 0;
                if (prefix.Length() != 0)
                {
                    MemCopy(out->text, prefix.Data(), prefix.Length());
                    out->text[prefix.Length()] = '/';
                    length = prefix.Length() + 1;
                }
                for (usize i = 0; i < own.Length() && length < ENGINE_NAME_MAX_BYTES; ++i)
                {
                    out->text[length++] = own[i];
                }
                out->length = static_cast<u8>(length);
            };
            const ecs::Entity top = object.world.Create();
            if (!top.IsValid())
            {
                return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the world refused a new entity");
            }
            made.Push(top);
            ecs::LocalTransform at;
            at.position = Vec3(place.position[0], place.position[1], place.position[2]);
            const float rotation_length2 = place.rotation[0] * place.rotation[0] +
                                           place.rotation[1] * place.rotation[1] +
                                           place.rotation[2] * place.rotation[2] +
                                           place.rotation[3] * place.rotation[3];
            const float rotation_scale = 1.0f / Sqrt(rotation_length2);  // checked above: above zero and finite
            at.rotation = Quaternion(place.rotation[0] * rotation_scale, place.rotation[1] * rotation_scale,
                                     place.rotation[2] * rotation_scale, place.rotation[3] * rotation_scale);
            at.scale = Vec3(place.scale, place.scale, place.scale);
            EntityName top_name;
            name_of(prefix.Length() != 0 ? StringView() : StringView("level"), &top_name);
            if (prefix.Length() != 0)
            {
                top_name.length = static_cast<u8>(prefix.Length());  // the root is called the prefix itself
            }
            if (object.world.Add<ecs::LocalTransform>(top, at) != ecs::Status::Ok ||
                object.world.Add<ecs::WorldTransform>(top, ecs::WorldTransform()) != ecs::Status::Ok ||
                object.world.Add<EntityName>(top, top_name) != ecs::Status::Ok)
            {
                undo();
                return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the world refused a component");
            }
            for (usize i = 0; i < source.Size(); ++i)
            {
                const ecs::Entity entity = object.world.Create();
                if (!entity.IsValid())
                {
                    undo();
                    return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the world refused a new entity");
                }
                made.Push(entity);
                ecs::Status status = object.world.Add<ecs::LocalTransform>(
                    entity, *scratch.Peek<ecs::LocalTransform>(source[i]));
                if (status == ecs::Status::Ok)
                {  // the frame's transform system writes the world matrix of entities that have both
                    status = object.world.Add<ecs::WorldTransform>(entity, ecs::WorldTransform());
                }
                if (const asset::EntityName* own = scratch.Peek<asset::EntityName>(source[i]))
                {
                    EntityName name;
                    name_of(own->name.View(), &name);
                    if (status == ecs::Status::Ok)
                    {
                        status = object.world.Add<EntityName>(entity, name);
                    }
                }
                if (const asset::MeshReference* mesh = scratch.Peek<asset::MeshReference>(source[i]))
                {
                    MeshReference reference;
                    reference.mesh = mesh->mesh.id;
                    reference.material_resource = mesh->material.id;
                    if (status == ecs::Status::Ok)
                    {
                        status = object.world.Add<MeshReference>(entity, reference);
                    }
                }
                if (const asset::MeshSlotMaterials* slots = scratch.Peek<asset::MeshSlotMaterials>(source[i]))
                {
                    SlotMaterialsComponent made_slots;
                    for (u32 k = 0; k < SlotMaterialsComponent::kMax && k < asset::kMaxLevelSlotMaterials; ++k)
                    {
                        made_slots.material[k] = slots->At(k).id;
                        if (made_slots.material[k] != 0)
                        {
                            made_slots.count = k + 1;
                        }
                    }
                    if (status == ecs::Status::Ok && made_slots.count != 0)
                    {
                        status = object.world.Add<SlotMaterialsComponent>(entity, made_slots);
                    }
                }
                if (const animation::SkeletonInstance* skeleton = scratch.Peek<animation::SkeletonInstance>(source[i]))
                {
                    animation::SkeletonInstance instance;
                    instance.skeleton = skeleton->skeleton;
                    if (status == ecs::Status::Ok)
                    {
                        status = object.world.Add<animation::SkeletonInstance>(entity, Move(instance));
                    }
                }
                if (status != ecs::Status::Ok)
                {
                    undo();
                    return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the world refused a component");
                }
            }
            // The hierarchy: a level entity with a parent in the level follows it, the others hang from the root.
            Vector<u32> order;  // positions in `source`, by entity key
            for (usize i = 0; i < source.Size(); ++i)
            {
                order.Push(static_cast<u32>(i));
            }
            Sort(Span<u32>(order.Data(), order.Size()),
                 [&](u32 a, u32 b) { return source[a].ToU64() < source[b].ToU64(); });
            for (usize i = 0; i < source.Size(); ++i)
            {
                ecs::Entity parent = top;
                if (const ecs::Parent* link = scratch.Peek<ecs::Parent>(source[i]))
                {
                    // the position of the parent in `source`, by halving the keys sorted once above
                    const u64 wanted = link->entity.ToU64();
                    usize low = 0;
                    usize high = order.Size();
                    while (low < high)
                    {
                        const usize mid = low + (high - low) / 2;
                        if (source[order[mid]].ToU64() < wanted)
                        {
                            low = mid + 1;
                        }
                        else
                        {
                            high = mid;
                        }
                    }
                    if (low < order.Size() && source[order[low]].ToU64() == wanted)
                    {
                        parent = made[order[low] + 1];
                    }
                }
                if (ecs::SetParent(object.world, made[i + 1], parent) != ecs::HierarchyStatus::Ok)
                {
                    undo();
                    return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the level's hierarchy is deeper than the engine allows");
                }
            }
            *root = top.ToU64();
            for (usize i = 0; i < made.Size() && i < capacity; ++i)
            {
                keys[i] = made[i].ToU64();
            }
            if (total != nullptr)
            {
                *total = made.Size();
            }
            return ENGINE_OK;
        });
}

engine_status WorldSetEnvironmentFn(engine_handle world, const engine_environment* environment)
{
    const engine_status checked = CheckInStruct(environment, sizeof(engine_environment), "engine_environment");
    if (checked != ENGINE_OK)
    {
        return checked;
    }
    for (const float c : environment->ambient_color)
    {
        if (!(Finite(c) && c >= 0.0f))
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the ambient colour is not finite and >= 0");
        }
    }
    for (const float c : environment->sky_color)
    {
        if (!(Finite(c) && c >= 0.0f))
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the sky colour is not finite and >= 0");
        }
    }
    if (!(Finite(environment->ambient_intensity) && environment->ambient_intensity >= 0.0f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the ambient intensity is not finite and >= 0");
    }
    if (!(Finite(environment->exposure) && environment->exposure > 0.0f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the exposure is not finite and > 0");
    }
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the world is not an engine's");
    }
    LockGuard<Mutex> lock(runtime->world_mutex);
    engine_status found = ENGINE_OK;
    WorldObject* object = FindWorld(*runtime, world, &found);
    if (object == nullptr)
    {
        return found;
    }
    Environment value;
    for (int i = 0; i < 3; ++i)
    {
        value.ambient_color[i] = environment->ambient_color[i];
        value.sky_color[i] = environment->sky_color[i];
    }
    value.ambient_intensity = environment->ambient_intensity;
    value.exposure = environment->exposure;
    object->environment = value;
    object->environment_set = true;
    return ENGINE_OK;
}

engine_status ComponentGetFn(engine_handle world, u64 key, u32 kind_value, void* out)
{
    if (!KnownKind(kind_value))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "unknown component kind");
    }
    const engine_component_kind kind = static_cast<engine_component_kind>(kind_value);
    if (kind == ENGINE_COMPONENT_NAME)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "read a name with entity_name_get");
    }
    const usize wanted = kind == ENGINE_COMPONENT_TRANSFORM           ? sizeof(engine_transform)
                         : kind == ENGINE_COMPONENT_WORLD_TRANSFORM   ? sizeof(engine_world_transform)
                         : kind == ENGINE_COMPONENT_CAMERA            ? sizeof(engine_camera)
                         : kind == ENGINE_COMPONENT_LIGHT             ? sizeof(engine_light)
                         : kind == ENGINE_COMPONENT_LABEL             ? sizeof(engine_label)
                         : kind == ENGINE_COMPONENT_VISIBILITY        ? sizeof(engine_visibility)
                         : kind == ENGINE_COMPONENT_RIGID_BODY        ? sizeof(engine_rigid_body)
                         : kind == ENGINE_COMPONENT_COLLIDER          ? sizeof(engine_collider)
                         : kind == ENGINE_COMPONENT_JOINT             ? sizeof(engine_joint)
                         : kind == ENGINE_COMPONENT_CHARACTER         ? sizeof(engine_character)
                         : kind == ENGINE_COMPONENT_AUDIO_SOURCE      ? offsetof(engine_audio_source, stream)
                         : kind == ENGINE_COMPONENT_AUDIO_LISTENER    ? sizeof(engine_audio_listener)
                         : kind == ENGINE_COMPONENT_SCRIPT            ? sizeof(engine_script_component)
                         : kind == ENGINE_COMPONENT_SKELETON_INSTANCE ? sizeof(engine_skeleton_instance)
                         : kind == ENGINE_COMPONENT_ANIMATION_PLAYER  ? sizeof(engine_animation_state)
                                                                      : kMeshReferenceFirstSize;
    const char* what = kind == ENGINE_COMPONENT_TRANSFORM           ? "engine_transform"
                       : kind == ENGINE_COMPONENT_WORLD_TRANSFORM   ? "engine_world_transform"
                       : kind == ENGINE_COMPONENT_CAMERA            ? "engine_camera"
                       : kind == ENGINE_COMPONENT_LIGHT             ? "engine_light"
                       : kind == ENGINE_COMPONENT_LABEL             ? "engine_label"
                       : kind == ENGINE_COMPONENT_VISIBILITY        ? "engine_visibility"
                       : kind == ENGINE_COMPONENT_RIGID_BODY        ? "engine_rigid_body"
                       : kind == ENGINE_COMPONENT_COLLIDER          ? "engine_collider"
                       : kind == ENGINE_COMPONENT_JOINT             ? "engine_joint"
                       : kind == ENGINE_COMPONENT_CHARACTER         ? "engine_character"
                       : kind == ENGINE_COMPONENT_AUDIO_SOURCE      ? "engine_audio_source"
                       : kind == ENGINE_COMPONENT_AUDIO_LISTENER    ? "engine_audio_listener"
                       : kind == ENGINE_COMPONENT_SCRIPT            ? "engine_script_component"
                       : kind == ENGINE_COMPONENT_SKELETON_INSTANCE ? "engine_skeleton_instance"
                       : kind == ENGINE_COMPONENT_ANIMATION_PLAYER  ? "engine_animation_state"
                                                                    : "engine_mesh_reference";
    const engine_status checked = CheckInStruct(out, wanted, what);
    if (checked != ENGINE_OK)
    {
        return checked;
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         const ecs::World& view = object.world;
                         if (kind == ENGINE_COMPONENT_TRANSFORM)
                         {
                             const ecs::LocalTransform* local = view.Peek<ecs::LocalTransform>(entity);
                             if (local == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no transform");
                             }
                             engine_transform result = {};
                             result.struct_size = sizeof(engine_transform);
                             result.position[0] = local->position.x;
                             result.position[1] = local->position.y;
                             result.position[2] = local->position.z;
                             result.rotation[0] = local->rotation.x;
                             result.rotation[1] = local->rotation.y;
                             result.rotation[2] = local->rotation.z;
                             result.rotation[3] = local->rotation.w;
                             result.scale[0] = local->scale.x;
                             result.scale[1] = local->scale.y;
                             result.scale[2] = local->scale.z;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_WORLD_TRANSFORM)
                         {
                             const ecs::WorldTransform* world_transform = view.Peek<ecs::WorldTransform>(entity);
                             if (world_transform == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no transform");
                             }
                             static_assert(sizeof(Mat4) == 16 * sizeof(float), "a Mat4 is sixteen floats");
                             engine_world_transform result = {};
                             result.struct_size = sizeof(engine_world_transform);
                             MemCopy(result.matrix, &world_transform->matrix, sizeof(result.matrix));
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_SKELETON_INSTANCE)
                         {
                             const animation::SkeletonInstance* instance = view.Peek<animation::SkeletonInstance>(
                                 entity);
                             if (instance == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no skeleton instance");
                             }
                             engine_skeleton_instance result = {};
                             result.struct_size = sizeof(engine_skeleton_instance);
                             result.skeleton = instance->skeleton;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_ANIMATION_PLAYER)
                         {
                             const animation::AnimationPlayer* player = view.Peek<animation::AnimationPlayer>(entity);
                             if (player == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no animation player");
                             }
                             engine_animation_state result = {};
                             result.struct_size = sizeof(engine_animation_state);
                             result.playing = player->playing ? 1u : 0u;
                             result.looping = player->looping ? 1u : 0u;
                             result.finished = player->finished ? 1u : 0u;
                             result.fading = player->fade_clip != 0 ? 1u : 0u;
                             result.clip = player->clip;
                             result.time = player->time;
                             result.speed = player->speed;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_AUDIO_SOURCE)
                         {
                             const audio::AudioSource* source = view.Peek<audio::AudioSource>(entity);
                             if (source == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no audio source");
                             }
                             engine_audio_source result = {};
                             result.struct_size = sizeof(engine_audio_source);
                             result.clip = AudioHandleOfClip(*CurrentRuntime(), source->clip);
                             result.bus = source->bus.index;
                             result.gain = source->gain;
                             result.pitch = source->pitch;
                             result.flags = (source->loop ? ENGINE_AUDIO_SOURCE_LOOP : 0u) |
                                            (source->play_on_start ? ENGINE_AUDIO_SOURCE_PLAY_ON_START : 0u) |
                                            (source->spatial ? ENGINE_AUDIO_SOURCE_SPATIAL : 0u) |
                                            (source->doppler ? ENGINE_AUDIO_SOURCE_DOPPLER : 0u) |
                                            (source->occlusion ? ENGINE_AUDIO_SOURCE_OCCLUSION : 0u);
                             result.priority = source->priority;
                             result.model = static_cast<u32>(source->attenuation.model);
                             result.min_distance = source->attenuation.min_distance;
                             result.max_distance = source->attenuation.max_distance;
                             result.rolloff = source->attenuation.rolloff;
                             result.cone_inner = source->cone.inner_angle;
                             result.cone_outer = source->cone.outer_angle;
                             result.cone_outer_gain = source->cone.outer_gain;
                             result.stream = AudioHandleOfStream(*CurrentRuntime(), source->stream);
                             // A host of the first version has a struct that ends before `stream`.
                             const usize room = StructSizeOf(out) < sizeof(result) ? StructSizeOf(out) : sizeof(result);
                             result.struct_size = static_cast<u32>(room);
                             MemCopy(out, &result, room);
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_SCRIPT)
                         {
                             return ScriptComponentGet(object, entity, out);
                         }
                         if (kind == ENGINE_COMPONENT_AUDIO_LISTENER)
                         {
                             const audio::AudioListener* listener = view.Peek<audio::AudioListener>(entity);
                             if (listener == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no audio listener");
                             }
                             engine_audio_listener result = {};
                             result.struct_size = sizeof(engine_audio_listener);
                             result.gain = listener->gain;
                             result.active = listener->active ? 1u : 0u;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_RIGID_BODY)
                         {
                             const physics::RigidBody* body = view.Peek<physics::RigidBody>(entity);
                             if (body == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no rigid body");
                             }
                             engine_rigid_body result = {};
                             result.struct_size = sizeof(engine_rigid_body);
                             result.type = static_cast<u32>(body->type);
                             result.mass = body->mass;
                             result.linear_damping = body->linear_damping;
                             result.angular_damping = body->angular_damping;
                             result.gravity_scale = body->gravity_scale;
                             result.flags = body->flags;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_JOINT)
                         {
                             const physics::Joint* joint = view.Peek<physics::Joint>(entity);
                             if (joint == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no joint");
                             }
                             engine_joint result = {};
                             result.struct_size = sizeof(engine_joint);
                             result.kind = static_cast<u32>(joint->type);
                             result.entity_a = joint->entity_a.IsValid() ? joint->entity_a.ToU64() : 0;
                             result.entity_b = joint->entity_b.IsValid() ? joint->entity_b.ToU64() : 0;
                             result.point_a[0] = joint->point_a.x;
                             result.point_a[1] = joint->point_a.y;
                             result.point_a[2] = joint->point_a.z;
                             result.point_b[0] = joint->point_b.x;
                             result.point_b[1] = joint->point_b.y;
                             result.point_b[2] = joint->point_b.z;
                             result.axis[0] = joint->axis.x;
                             result.axis[1] = joint->axis.y;
                             result.axis[2] = joint->axis.z;
                             result.min_length = joint->min_length;
                             result.max_length = joint->max_length;
                             result.hertz = joint->hertz;
                             result.damping_ratio = joint->damping_ratio;
                             result.break_force = joint->break_force;
                             result.break_torque = joint->break_torque;
                             result.flags = (joint->collide_connected ? ENGINE_JOINT_COLLIDE_CONNECTED : 0u) |
                                            (joint->enable_limit ? ENGINE_JOINT_LIMIT : 0u) |
                                            (joint->enable_motor ? ENGINE_JOINT_MOTOR : 0u);
                             result.lower_angle = joint->lower_angle;
                             result.upper_angle = joint->upper_angle;
                             result.motor_speed = joint->motor_speed;
                             result.max_motor_torque = joint->max_motor_torque;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_CHARACTER)
                         {
                             const CharacterComponent* character = view.Peek<CharacterComponent>(entity);
                             if (character == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no character");
                             }
                             engine_character result = {};
                             result.struct_size = sizeof(engine_character);
                             result.radius = character->radius;
                             result.height = character->height;
                             result.step_height = character->step_height;
                             result.min_ground_y = character->min_ground_y;
                             result.skin_width = character->skin_width;
                             result.gravity_scale = character->gravity_scale;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_COLLIDER)
                         {
                             const physics::Collider* collider = view.Peek<physics::Collider>(entity);
                             if (collider == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no collider");
                             }
                             engine_collider result = {};
                             result.struct_size = sizeof(engine_collider);
                             result.shape = collider->shape.kind == physics::ShapeKind::Sphere ? ENGINE_SHAPE_SPHERE
                                            : collider->shape.kind == physics::ShapeKind::Box  ? ENGINE_SHAPE_BOX
                                                                                               : ENGINE_SHAPE_CAPSULE;
                             result.radius = collider->shape.radius;
                             result.half_height = collider->shape.half_height;
                             result.half_extents[0] = collider->shape.half_extents.x;
                             result.half_extents[1] = collider->shape.half_extents.y;
                             result.half_extents[2] = collider->shape.half_extents.z;
                             result.offset[0] = collider->offset.x;
                             result.offset[1] = collider->offset.y;
                             result.offset[2] = collider->offset.z;
                             result.rotation[0] = collider->rotation.x;
                             result.rotation[1] = collider->rotation.y;
                             result.rotation[2] = collider->rotation.z;
                             result.rotation[3] = collider->rotation.w;
                             result.friction = collider->material.friction;
                             result.restitution = collider->material.restitution;
                             result.density = collider->material.density;
                             result.layer = collider->layer;
                             result.group = collider->group;
                             result.mask = collider->mask;
                             result.is_trigger = collider->is_trigger ? 1u : 0u;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_VISIBILITY)
                         {
                             const VisibilityComponent* visibility = view.Peek<VisibilityComponent>(entity);
                             if (visibility == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no visibility component");
                             }
                             engine_visibility result = {};
                             result.struct_size = sizeof(engine_visibility);
                             result.hidden = visibility->hidden;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_LABEL)
                         {
                             const LabelComponent* label = view.Peek<LabelComponent>(entity);
                             if (label == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no label");
                             }
                             engine_label result = {};
                             result.struct_size = sizeof(engine_label);
                             result.text.length = label->length;  // the bytes stay in the engine (data is NULL)
                             MemCopy(result.color, label->color, sizeof(result.color));
                             result.size = label->size;
                             MemCopy(result.offset, label->offset, sizeof(result.offset));
                             result.anchor = label->anchor;
                             result.flags = label->flags;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_LIGHT)
                         {
                             const LightComponent* light = view.Peek<LightComponent>(entity);
                             if (light == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no light");
                             }
                             engine_light result = {};
                             result.struct_size = sizeof(engine_light);
                             result.kind = light->kind;
                             result.color[0] = light->color[0];
                             result.color[1] = light->color[1];
                             result.color[2] = light->color[2];
                             result.intensity = light->intensity;
                             result.radius = light->radius;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         if (kind == ENGINE_COMPONENT_CAMERA)
                         {
                             const CameraComponent* camera = view.Peek<CameraComponent>(entity);
                             if (camera == nullptr)
                             {
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no camera");
                             }
                             engine_camera result = {};
                             result.struct_size = sizeof(engine_camera);
                             result.flags = camera->flags;
                             result.fov_y = camera->fov_y;
                             result.near_plane = camera->near_plane;
                             result.far_plane = camera->far_plane;
                             result.aspect = camera->aspect;
                             MemCopy(out, &result, sizeof(result));
                             return ENGINE_OK;
                         }
                         const MeshReference* reference = view.Peek<MeshReference>(entity);
                         if (reference == nullptr)
                         {
                             return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no mesh reference");
                         }
                         engine_mesh_reference result = {};
                         result.struct_size = sizeof(engine_mesh_reference);
                         result.mesh = reference->mesh;
                         result.material = reference->material;
                         result.material_resource = reference->material_resource;
                         const u32 written = StructSizeOf(out) < sizeof(result) ? StructSizeOf(out)
                                                                                : static_cast<u32>(sizeof(result));
                         result.struct_size = written;  // a host built against an older header gets the fields it knows
                         MemCopy(out, &result, written);
                         return ENGINE_OK;
                     });
}

engine_status ComponentRemoveFn(engine_handle world, u64 key, u32 kind_value)
{
    if (!KnownKind(kind_value))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "unknown component kind");
    }
    const engine_component_kind kind = static_cast<engine_component_kind>(kind_value);
    if (kind == ENGINE_COMPONENT_WORLD_TRANSFORM)
    {
        return Fail(ENGINE_ERROR_UNSUPPORTED, "the world transform goes with the transform");
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         ecs::World& w = object.world;
                         ecs::Status status = ecs::Status::Ok;
                         switch (kind)
                         {
                         case ENGINE_COMPONENT_TRANSFORM:
                             status = w.Remove<ecs::LocalTransform>(entity);
                             if (status == ecs::Status::Ok && w.Has<ecs::WorldTransform>(entity))
                             {
                                 w.Remove<ecs::WorldTransform>(entity);
                             }
                             break;
                         case ENGINE_COMPONENT_NAME: status = w.Remove<EntityName>(entity); break;
                         case ENGINE_COMPONENT_CAMERA: status = w.Remove<CameraComponent>(entity); break;
                         case ENGINE_COMPONENT_LIGHT: status = w.Remove<LightComponent>(entity); break;
                         case ENGINE_COMPONENT_LABEL: status = w.Remove<LabelComponent>(entity); break;
                         case ENGINE_COMPONENT_RIGID_BODY: status = w.Remove<physics::RigidBody>(entity); break;
                         case ENGINE_COMPONENT_COLLIDER: status = w.Remove<physics::Collider>(entity); break;
                         case ENGINE_COMPONENT_JOINT: status = w.Remove<physics::Joint>(entity); break;
                         case ENGINE_COMPONENT_CHARACTER:
                             status = w.Remove<CharacterComponent>(entity);  // the frame gives the capsule back
                             break;
                         case ENGINE_COMPONENT_AUDIO_SOURCE: status = w.Remove<audio::AudioSource>(entity); break;
                         case ENGINE_COMPONENT_AUDIO_LISTENER: status = w.Remove<audio::AudioListener>(entity); break;
                         case ENGINE_COMPONENT_SCRIPT: return ScriptComponentRemove(object, entity);
                         case ENGINE_COMPONENT_VISIBILITY: status = w.Remove<VisibilityComponent>(entity); break;
                         case ENGINE_COMPONENT_SKELETON_INSTANCE:
                             status = w.Remove<animation::SkeletonInstance>(entity);
                             break;
                         case ENGINE_COMPONENT_ANIMATION_PLAYER:
                             status = w.Remove<animation::AnimationPlayer>(entity);
                             break;
                         default: status = w.Remove<MeshReference>(entity); break;
                         }
                         if (status != ecs::Status::Ok)
                         {
                             SetLastError("not_found: the entity has no %s", NameOfKind(kind));
                             return ENGINE_ERROR_NOT_FOUND;
                         }
                         return ENGINE_OK;
                     });
}

engine_status ComponentHasFn(engine_handle world, u64 key, u32 kind_value, int* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    *out = 0;
    if (!KnownKind(kind_value))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "unknown component kind");
    }
    const engine_component_kind kind = static_cast<engine_component_kind>(kind_value);
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         const ecs::ComponentId id = IdOfKind(kind);
                         *out = id != ecs::kInvalidComponent && object.world.HasById(entity, id) ? 1 : 0;
                         return ENGINE_OK;
                     });
}

engine_status EntityNameSetFn(engine_handle world, u64 key, engine_string name)
{
    if (name.data == nullptr && name.length != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a string has a NULL pointer and a length");
    }
    if (name.length > ENGINE_NAME_MAX_BYTES)
    {
        return Fail(ENGINE_ERROR_TOO_LARGE, "the name is longer than ENGINE_NAME_MAX_BYTES");
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         EntityName value;
                         value.length = static_cast<u8>(name.length);
                         if (name.length != 0)
                         {
                             MemCopy(value.text, name.data, name.length);
                         }
                         return SetOrAdd(object.world, entity, value);
                     });
}

engine_status EntityNameGetFn(engine_handle world, u64 key, engine_buffer* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    out->data = nullptr;
    out->size = 0;
    out->owner = nullptr;
    out->release = nullptr;
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         const ecs::World& view = object.world;
                         const EntityName* name = view.Peek<EntityName>(entity);
                         if (name == nullptr)
                         {
                             return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no name");
                         }
                         MakeTextBuffer(StringView(name->text, name->length), out);
                         return ENGINE_OK;
                     });
}

engine_status QueryFn(engine_handle world, u32 mask, u64* keys, size_t capacity, size_t* total)
{
    if (total == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`total` is NULL");
    }
    *total = 0;
    if ((mask & ~ENGINE_COMPONENT_MASK_ALL) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the mask has an unknown component bit");
    }
    if (keys == nullptr && capacity != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`keys` is NULL with a capacity above zero");
    }
    return WithWorld(world, false, 0,
                     [&](WorldObject& object, ecs::Entity)
                     {
                         ecs::ComponentId wanted[ENGINE_COMPONENT_SCRIPT];
                         usize wanted_count = 0;
                         for (u32 kind = ENGINE_COMPONENT_TRANSFORM; kind <= ENGINE_COMPONENT_SCRIPT; ++kind)
                         {
                             if ((mask & ENGINE_COMPONENT_BIT(kind)) != 0)
                             {
                                 wanted[wanted_count++] = IdOfKind(static_cast<engine_component_kind>(kind));
                                 if (wanted[wanted_count - 1] == ecs::kInvalidComponent)
                                 {
                                     return ENGINE_OK;  // a component this build does not have: nothing has it
                                 }
                             }
                         }
                         const ecs::World& view = object.world;
                         usize count = 0;
                         for (u32 a = 0; a < view.ArchetypeCount(); ++a)
                         {
                             const ecs::Archetype& archetype = view.ArchetypeAt(a);
                             if (!archetype.Valid())
                             {
                                 continue;
                             }
                             bool matches = true;
                             for (usize i = 0; i < wanted_count && matches; ++i)
                             {
                                 matches = archetype.Has(wanted[i]);
                             }
                             if (!matches)
                             {
                                 continue;
                             }
                             for (u32 c = 0; c < archetype.ChunkCount(); ++c)
                             {
                                 const Span<ecs::Entity> entities = archetype.Entities(archetype.ChunkAt(c));
                                 for (usize e = 0; e < entities.Size(); ++e)
                                 {
                                     if (count < capacity)
                                     {
                                         keys[count] = entities[e].ToU64();
                                     }
                                     ++count;
                                 }
                             }
                         }
                         *total = count;
                         return ENGINE_OK;
                     });
}

// ---- physics (Phase 25 Day 408)

namespace
{

bool FiniteArray(const float* values, usize count)
{
    for (usize i = 0; i < count; ++i)
    {
        if (!Finite(values[i]))
        {
            return false;
        }
    }
    return true;
}

Vec3 ToVec3(const float* v)
{
    return Vec3(v[0], v[1], v[2]);
}

// The body of an entity, after making the bodies that are due (a body is made by the frame or by the first call that
// needs it). Null, with the failure set, when the entity has none.
physics::BodyId BodyOf(WorldObject& object, ecs::Entity entity, engine_status* status)
{
    physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
    if (object.physics_used)
    {
        physics::SyncBodies(object.world, physics);
    }
    const physics::BodyLink* link = object.world.Peek<physics::BodyLink>(entity);
    if (link == nullptr || physics.Find(link->id) == nullptr)
    {
        *status = Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no physics body (it needs a rigid body, a collider and "
                                               "a transform, and no parent)");
        return physics::BodyId();
    }
    *status = ENGINE_OK;
    return link->id;
}

engine_status FromBody(physics::BodyStatus status)
{
    switch (status)
    {
    case physics::BodyStatus::Ok: return ENGINE_OK;
    case physics::BodyStatus::StaleId: return Fail(ENGINE_ERROR_NOT_FOUND, "the physics body is gone");
    case physics::BodyStatus::WrongType: return Fail(ENGINE_ERROR_UNSUPPORTED, "a static body takes no force");
    case physics::BodyStatus::OutOfMemory: return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "out of memory");
    case physics::BodyStatus::Unsupported: return Fail(ENGINE_ERROR_UNSUPPORTED, "not supported by the physics");
    case physics::BodyStatus::Invalid: break;
    }
    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the physics refused the numbers");
}

physics::PhysicsQueryFilter FilterOf(u32 layer_mask)
{
    physics::PhysicsQueryFilter filter;
    filter.layer_mask = layer_mask;
    return filter;
}

engine_status OverlapKeys(WorldObject& object, const physics::Shape& shape, const Vec3& center, u32 layer_mask,
                          u64* keys, size_t capacity, size_t* total)
{
    physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
    if (object.physics_used)
    {
        physics::SyncBodies(object.world, physics);
    }
    Vector<physics::ColliderId> found;
    physics::Overlap(physics, shape, center, Quaternion::Identity(), FilterOf(layer_mask), &found);
    size_t count = 0;
    for (usize i = 0; i < found.Size(); ++i)
    {
        const physics::ColliderRecord* record = physics.FindCollider(found[i]);
        const physics::Body* body = record == nullptr ? nullptr : physics.Find(record->owner);
        if (body == nullptr)
        {
            continue;
        }
        if (count < capacity)
        {
            keys[count] = body->entity.ToU64();
        }
        ++count;
    }
    *total = count;
    return ENGINE_OK;
}

}  // namespace

engine_status PhysicsSetGravityFn(engine_handle world, const float* gravity)
{
    if (gravity == nullptr || !FiniteArray(gravity, 3))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the gravity is NULL or not finite");
    }
    return WithWorld(world, false, 0,
                     [&](WorldObject& object, ecs::Entity)
                     {
                         physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
                         physics::StepConfig config = physics.GetStepConfig();
                         config.gravity = ToVec3(gravity);
                         return FromBody(physics.SetStepConfig(config));
                     });
}

engine_status BodyApplyForceFn(engine_handle world, u64 key, const float* force, const float* point)
{
    if (force == nullptr || !FiniteArray(force, 3) || (point != nullptr && !FiniteArray(point, 3)))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the force or the point is NULL or not finite");
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         engine_status status = ENGINE_OK;
                         const physics::BodyId id = BodyOf(object, entity, &status);
                         if (status != ENGINE_OK)
                         {
                             return status;
                         }
                         physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
                         const physics::Body* body = physics.Find(id);
                         const Vec3 at = point != nullptr
                                             ? ToVec3(point)
                                             : body->position + body->orientation.RotateVector(body->mass.center);
                         return FromBody(physics.ApplyForce(id, ToVec3(force), at));
                     });
}

engine_status BodyApplyImpulseFn(engine_handle world, u64 key, const float* impulse, const float* point)
{
    if (impulse == nullptr || !FiniteArray(impulse, 3) || (point != nullptr && !FiniteArray(point, 3)))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the impulse or the point is NULL or not finite");
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         engine_status status = ENGINE_OK;
                         const physics::BodyId id = BodyOf(object, entity, &status);
                         if (status != ENGINE_OK)
                         {
                             return status;
                         }
                         physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
                         const physics::Body* body = physics.Find(id);
                         const Vec3 at = point != nullptr
                                             ? ToVec3(point)
                                             : body->position + body->orientation.RotateVector(body->mass.center);
                         return FromBody(physics.ApplyImpulse(id, ToVec3(impulse), at));
                     });
}

engine_status BodySetVelocityFn(engine_handle world, u64 key, const float* linear, const float* angular)
{
    if ((linear != nullptr && !FiniteArray(linear, 3)) || (angular != nullptr && !FiniteArray(angular, 3)))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a velocity is not finite");
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         engine_status status = ENGINE_OK;
                         const physics::BodyId id = BodyOf(object, entity, &status);
                         if (status != ENGINE_OK)
                         {
                             return status;
                         }
                         physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
                         const physics::Body* body = physics.Find(id);
                         physics::Velocity velocity;
                         velocity.linear = linear != nullptr ? ToVec3(linear) : body->linear_velocity;
                         velocity.angular = angular != nullptr ? ToVec3(angular) : body->angular_velocity;
                         return FromBody(physics.SetVelocity(id, velocity));
                     });
}

engine_status BodyGetVelocityFn(engine_handle world, u64 key, float* linear, float* angular)
{
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         engine_status status = ENGINE_OK;
                         const physics::BodyId id = BodyOf(object, entity, &status);
                         if (status != ENGINE_OK)
                         {
                             return status;
                         }
                         const physics::Body* body = object.world.Resource<physics::PhysicsWorld>().Find(id);
                         if (linear != nullptr)
                         {
                             linear[0] = body->linear_velocity.x;
                             linear[1] = body->linear_velocity.y;
                             linear[2] = body->linear_velocity.z;
                         }
                         if (angular != nullptr)
                         {
                             angular[0] = body->angular_velocity.x;
                             angular[1] = body->angular_velocity.y;
                             angular[2] = body->angular_velocity.z;
                         }
                         return ENGINE_OK;
                     });
}

engine_status BodyTeleportFn(engine_handle world, u64 key, const float* position, const float* rotation)
{
    if ((position != nullptr && !FiniteArray(position, 3)) || (rotation != nullptr && !FiniteArray(rotation, 4)))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the position or the rotation is not finite");
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         engine_status status = ENGINE_OK;
                         const physics::BodyId id = BodyOf(object, entity, &status);
                         if (status != ENGINE_OK)
                         {
                             return status;
                         }
                         physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
                         const physics::Body* body = physics.Find(id);
                         const Vec3 at = position != nullptr ? ToVec3(position) : body->position;
                         const Quaternion turn = rotation != nullptr
                                                     ? Quaternion(rotation[0], rotation[1], rotation[2], rotation[3])
                                                     : body->orientation;
                         const physics::BodyStatus moved = physics.SetTransform(id, at, turn);
                         if (moved != physics::BodyStatus::Ok)
                         {
                             return FromBody(moved);
                         }
                         // The entity follows at once, so a read before the next frame agrees with the body.
                         if (const ecs::LocalTransform* local = object.world.Peek<ecs::LocalTransform>(entity))
                         {
                             ecs::LocalTransform value = *local;
                             value.position = at;
                             value.rotation = turn;
                             object.world.Set<ecs::LocalTransform>(entity, value);
                         }
                         return ENGINE_OK;
                     });
}

engine_status PhysicsRaycastFn(engine_handle world, const float* origin, const float* direction, float max_distance,
                               u32 layer_mask, engine_ray_hit* hit, int* found)
{
    if (origin == nullptr || direction == nullptr || hit == nullptr || found == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "an argument is NULL");
    }
    *found = 0;
    if (StructSizeOf(hit) < sizeof(engine_ray_hit))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_ray_hit.struct_size is smaller than the struct");
    }
    if (!FiniteArray(origin, 3) || !FiniteArray(direction, 3) || !(Finite(max_distance) && max_distance > 0.0f) ||
        (direction[0] == 0.0f && direction[1] == 0.0f && direction[2] == 0.0f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the ray is not finite, has no direction or no length");
    }
    return WithWorld(world, false, 0,
                     [&](WorldObject& object, ecs::Entity)
                     {
                         physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
                         if (object.physics_used)
                         {
                             physics::SyncBodies(object.world, physics);
                         }
                         physics::PhysicsHit result;
                         if (!physics::RayCast(physics, ToVec3(origin), ToVec3(direction), max_distance,
                                               FilterOf(layer_mask), &result))
                         {
                             return ENGINE_OK;
                         }
                         const physics::Body* body = physics.Find(result.body);
                         hit->entity = body == nullptr ? 0 : body->entity.ToU64();
                         hit->point[0] = result.point.x;
                         hit->point[1] = result.point.y;
                         hit->point[2] = result.point.z;
                         hit->normal[0] = result.normal.x;
                         hit->normal[1] = result.normal.y;
                         hit->normal[2] = result.normal.z;
                         hit->distance = result.distance;
                         *found = 1;
                         return ENGINE_OK;
                     });
}

engine_status PhysicsOverlapSphereFn(engine_handle world, const float* center, float radius, u32 layer_mask, u64* keys,
                                     size_t capacity, size_t* total)
{
    if (total == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`total` is NULL");
    }
    *total = 0;
    if (center == nullptr || !FiniteArray(center, 3) || !(Finite(radius) && radius > 0.0f && radius <= 1.0e5f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the centre or the radius is NULL, not finite or not positive");
    }
    if (keys == nullptr && capacity != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`keys` is NULL with a capacity above zero");
    }
    return WithWorld(world, false, 0,
                     [&](WorldObject& object, ecs::Entity)
                     {
                         return OverlapKeys(object, physics::Shape::MakeSphere(radius), ToVec3(center), layer_mask,
                                            keys, capacity, total);
                     });
}

engine_status PhysicsOverlapBoxFn(engine_handle world, const float* center, const float* half_extents, u32 layer_mask,
                                  u64* keys, size_t capacity, size_t* total)
{
    if (total == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`total` is NULL");
    }
    *total = 0;
    if (center == nullptr || half_extents == nullptr || !FiniteArray(center, 3) || !FiniteArray(half_extents, 3) ||
        !(half_extents[0] > 0.0f && half_extents[1] > 0.0f && half_extents[2] > 0.0f) || half_extents[0] > 1.0e5f ||
        half_extents[1] > 1.0e5f || half_extents[2] > 1.0e5f)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT,
                    "the centre or the half extents are NULL, not finite or not positive");
    }
    if (keys == nullptr && capacity != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`keys` is NULL with a capacity above zero");
    }
    return WithWorld(world, false, 0,
                     [&](WorldObject& object, ecs::Entity)
                     {
                         return OverlapKeys(object, physics::Shape::MakeBox(ToVec3(half_extents)), ToVec3(center),
                                            layer_mask, keys, capacity, total);
                     });
}

// ---- joints, characters, events and snapshots (Phase 25 Day 409)

namespace
{

// The character of an entity, its capsule made again when a restore dropped it. Null, with the failure set, when the
// entity has no character.
CharacterEntry* CharacterOf(WorldObject& object, ecs::Entity entity, engine_status* status)
{
    physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
    if (object.physics_used)
    {
        physics::SyncBodies(object.world, physics);
    }
    const ecs::LocalTransform* transform = object.world.Peek<ecs::LocalTransform>(entity);
    if (!object.world.Has<CharacterComponent>(entity) || transform == nullptr)
    {
        *status = Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no character");
        return nullptr;
    }
    const u64 key = entity.ToU64();
    for (usize i = 0; i < object.characters.Size(); ++i)
    {
        CharacterEntry& entry = object.characters[i];
        if (entry.key != key)
        {
            continue;
        }
        if (physics.Find(entry.controller.BodyHandle()) == nullptr &&
            entry.controller.Create(&physics, entry.config, transform->position) != physics::BodyStatus::Ok)
        {
            *status = Fail(ENGINE_ERROR_INTERNAL, "the character's body could not be made again");
            return nullptr;
        }
        *status = ENGINE_OK;
        return &entry;
    }
    *status = Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no character");
    return nullptr;
}

void FillCharacterState(WorldObject& object, const CharacterEntry& entry, engine_character_state* out)
{
    if (out == nullptr)
    {
        return;
    }
    const physics::CharacterState& state = entry.controller.State();
    engine_character_state result = {};
    result.struct_size = sizeof(engine_character_state);
    result.on_ground = state.on_ground ? 1u : 0u;
    result.began_ground = entry.events.began_ground ? 1u : 0u;
    result.ended_ground = entry.events.ended_ground ? 1u : 0u;
    result.wall_hits = entry.events.wall_hits;
    const physics::Body* ground = object.world.Resource<physics::PhysicsWorld>().Find(state.ground_body);
    result.ground_entity = ground != nullptr && ground->entity.IsValid() ? ground->entity.ToU64() : 0;
    result.ground_normal[0] = state.ground_normal.x;
    result.ground_normal[1] = state.ground_normal.y;
    result.ground_normal[2] = state.ground_normal.z;
    result.wall_normal[0] = entry.events.wall_normal.x;
    result.wall_normal[1] = entry.events.wall_normal.y;
    result.wall_normal[2] = entry.events.wall_normal.z;
    result.vertical_speed = state.vertical_speed;
    MemCopy(out, &result, sizeof(result));
}

u64 KeyOf(ecs::Entity entity)
{
    return entity.IsValid() ? entity.ToU64() : 0;
}

void FillEvent(engine_physics_event* out, u32 kind, ecs::Entity a, ecs::Entity b, const Vec3& point, const Vec3& normal,
               f32 value)
{
    engine_physics_event event = {};
    event.struct_size = sizeof(engine_physics_event);
    event.kind = kind;
    event.entity_a = KeyOf(a);
    event.entity_b = KeyOf(b);
    event.point[0] = point.x;
    event.point[1] = point.y;
    event.point[2] = point.z;
    event.normal[0] = normal.x;
    event.normal[1] = normal.y;
    event.normal[2] = normal.z;
    event.value = value;
    MemCopy(out, &event, sizeof(event));
}

}  // namespace

engine_status JointReactionFn(engine_handle world, u64 key, float* force, float* torque)
{
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
                         if (object.physics_used)
                         {
                             physics::SyncBodies(object.world, physics);
                             physics::SyncJoints(object.world, physics);
                         }
                         const physics::JointLink* link = object.world.Peek<physics::JointLink>(entity);
                         Vec3 f(0.0f, 0.0f, 0.0f);
                         Vec3 t(0.0f, 0.0f, 0.0f);
                         if (link == nullptr || physics.JointReaction(link->id, &f, &t) != physics::BodyStatus::Ok)
                         {
                             return Fail(
                                 ENGINE_ERROR_NOT_FOUND,
                                 "the entity has no joint in the physics (none set, not made yet, or it broke)");
                         }
                         if (force != nullptr)
                         {
                             force[0] = f.x;
                             force[1] = f.y;
                             force[2] = f.z;
                         }
                         if (torque != nullptr)
                         {
                             torque[0] = t.x;
                             torque[1] = t.y;
                             torque[2] = t.z;
                         }
                         return ENGINE_OK;
                     });
}

engine_status CharacterMoveFn(engine_handle world, u64 key, const float* velocity, float dt,
                              engine_character_state* state)
{
    if (velocity == nullptr || !FiniteArray(velocity, 3) || !Finite(dt) || !(dt > 0.0f && dt <= 1.0f))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the velocity is NULL or not finite, or dt is not in (0, 1]");
    }
    if (state != nullptr)
    {
        const engine_status checked = CheckInStruct(state, sizeof(engine_character_state), "engine_character_state");
        if (checked != ENGINE_OK)
        {
            return checked;
        }
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         engine_status status = ENGINE_OK;
                         CharacterEntry* entry = CharacterOf(object, entity, &status);
                         if (entry == nullptr)
                         {
                             return status;
                         }
                         physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
                         entry->events = entry->controller.Move(&physics, ToVec3(velocity), dt);
                         FillCharacterState(object, *entry, state);
                         return ENGINE_OK;
                     });
}

engine_status CharacterJumpFn(engine_handle world, u64 key, float speed, engine_character_state* state)
{
    if (!Finite(speed) || speed < 0.0f || speed > 1000.0f)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the jump speed is not finite or not in 0..1000");
    }
    if (state != nullptr)
    {
        const engine_status checked = CheckInStruct(state, sizeof(engine_character_state), "engine_character_state");
        if (checked != ENGINE_OK)
        {
            return checked;
        }
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         engine_status status = ENGINE_OK;
                         CharacterEntry* entry = CharacterOf(object, entity, &status);
                         if (entry == nullptr)
                         {
                             return status;
                         }
                         if (speed > 0.0f && entry->controller.State().on_ground)
                         {
                             entry->controller.Jump(speed);
                         }
                         FillCharacterState(object, *entry, state);
                         return ENGINE_OK;
                     });
}

engine_status PhysicsEventsFn(engine_handle world, engine_physics_event* out, size_t capacity, size_t* total)
{
    if (total == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`total` is NULL");
    }
    *total = 0;
    if (out == nullptr && capacity != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL with a capacity above zero");
    }
    return WithWorld(
        world, false, 0,
        [&](WorldObject& object, ecs::Entity)
        {
            const physics::PhysicsEvents& events = object.world.Resource<physics::PhysicsWorld>().Events();
            const size_t count = events.Count();
            const size_t written = count < capacity ? count : capacity;
            for (size_t i = 0; i < written; ++i)
            {
                const engine_status checked = CheckInStruct(&out[i], sizeof(engine_physics_event),
                                                            "engine_physics_event");
                if (checked != ENGINE_OK)
                {
                    return checked;
                }
            }
            size_t at = 0;
            for (usize i = 0; i < events.contacts.Size(); ++i, ++at)
            {
                if (at >= written)
                {
                    continue;
                }
                const physics::ContactEvent& e = events.contacts[i];
                const u32 kind = e.kind == physics::ContactEventKind::Begin  ? ENGINE_PHYSICS_CONTACT_BEGIN
                                 : e.kind == physics::ContactEventKind::Stay ? ENGINE_PHYSICS_CONTACT_STAY
                                                                             : ENGINE_PHYSICS_CONTACT_END;
                FillEvent(&out[at], kind, e.entity_a, e.entity_b, e.point, e.normal, e.depth);
            }
            for (usize i = 0; i < events.triggers.Size(); ++i, ++at)
            {
                if (at >= written)
                {
                    continue;
                }
                const physics::TriggerEvent& e = events.triggers[i];
                FillEvent(&out[at], e.enter ? ENGINE_PHYSICS_TRIGGER_ENTER : ENGINE_PHYSICS_TRIGGER_EXIT,
                          e.entity_trigger, e.entity_other, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), 0.0f);
            }
            for (usize i = 0; i < events.joint_breaks.Size(); ++i, ++at)
            {
                if (at >= written)
                {
                    continue;
                }
                const physics::JointBreakEvent& e = events.joint_breaks[i];
                FillEvent(&out[at], ENGINE_PHYSICS_JOINT_BREAK, e.entity_a, e.entity_b, e.force, e.torque, 0.0f);
            }
            *total = count;
            return ENGINE_OK;
        });
}

engine_status PhysicsSnapshotFn(engine_handle world, engine_buffer* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    out->data = nullptr;
    out->size = 0;
    out->owner = nullptr;
    out->release = nullptr;
    return WithWorld(world, false, 0,
                     [&](WorldObject& object, ecs::Entity)
                     {
                         physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
                         if (object.physics_used)
                         {
                             physics::SyncBodies(object.world, physics);
                             physics::SyncJoints(object.world, physics);
                         }
                         bytes::ByteWriter writer;
                         physics.Snapshot(&writer);
                         const ByteSpan bytes = writer.Bytes();
                         MakeTextBuffer(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()), out);
                         return ENGINE_OK;
                     });
}

engine_status PhysicsRestoreFn(engine_handle world, const void* data, size_t size)
{
    if (data == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`data` is NULL");
    }
    return WithWorld(
        world, false, 0,
        [&](WorldObject& object, ecs::Entity)
        {
            physics::PhysicsWorld& physics = object.world.Resource<physics::PhysicsWorld>();
            if (object.physics_used)
            {
                physics::SyncBodies(object.world, physics);
                physics::SyncJoints(object.world, physics);
            }
            const physics::SnapshotStatus status = physics.Restore(ByteSpan(static_cast<const byte*>(data), size));
            switch (status)
            {
            case physics::SnapshotStatus::Ok: break;
            case physics::SnapshotStatus::UnknownVersion:
                return Fail(ENGINE_ERROR_UNSUPPORTED_VERSION, "the snapshot is of another version");
            case physics::SnapshotStatus::LibraryMismatch:
                return Fail(ENGINE_ERROR_MISSING_DEPENDENCY, "the snapshot names a shape the world does not hold");
            case physics::SnapshotStatus::OutOfMemory: return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "out of memory");
            default:
                SetLastError("corrupt: the physics snapshot is refused (%s)", physics::SnapshotStatusName(status));
                return ENGINE_ERROR_CORRUPT;
            }
            physics::WriteBackTransforms(object.world, physics, physics.Alpha());
            FollowCharacters(object);
            return ENGINE_OK;
        });
}

// ---- counters and jobs

engine_status CounterCreateFn(engine_handle engine, engine_handle* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    out->value = 0;
    Runtime* runtime = nullptr;
    const engine_status resolved = ResolveRuntime(engine, &runtime);
    if (resolved != ENGINE_OK)
    {
        return resolved;
    }
    LockGuard<Mutex> lock(runtime->table_mutex);
    usize slot = runtime->counters.Size();
    for (usize i = 0; i < runtime->counters.Size(); ++i)
    {
        if (runtime->counters[i].object.Get() == nullptr)
        {
            slot = i;
            break;
        }
    }
    if (slot == runtime->counters.Size() && slot >= kMaxSlots)
    {
        return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "too many counters");
    }
    if (!EngineObjectAcquire())
    {
        return Fail(ENGINE_ERROR_STALE_HANDLE, "the engine was destroyed");
    }
    if (slot == runtime->counters.Size())
    {
        runtime->counters.Push(Runtime::CounterSlot());
    }
    runtime->counters[slot].object = MakeUnique<CounterObject>();
    runtime->counters[slot].generation = NextGeneration();
    *out = MakeObjectHandle(kKindCounter, runtime->counters[slot].generation, slot);
    return ENGINE_OK;
}

engine_status CounterDestroyFn(engine_handle counter)
{
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(counter.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    UniquePtr<CounterObject> dying;
    {
        LockGuard<Mutex> lock(runtime->table_mutex);
        Runtime::CounterSlot* slot = nullptr;
        const engine_status status = ResolveSlot(runtime->counters, counter, kKindCounter, "counter", &slot);
        if (status != ENGINE_OK)
        {
            return status;
        }
        {
            LockGuard<Mutex> inner(slot->object->mutex);
            if (slot->object->pending != 0 || slot->object->waiters != 0)
            {
                SetLastError("in_use: %u job(s) of the counter have not finished, %u thread(s) wait on it",
                             static_cast<unsigned>(slot->object->pending),
                             static_cast<unsigned>(slot->object->waiters));
                return ENGINE_ERROR_IN_USE;
            }
        }
        dying = static_cast<UniquePtr<CounterObject>&&>(slot->object);
    }
    dying = nullptr;
    EngineObjectRelease();
    return ENGINE_OK;
}

engine_status JobsSubmitFn(engine_handle engine, engine_job_fn fn, void* user, engine_handle counter)
{
    if (fn == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`fn` is NULL");
    }
    Runtime* runtime = nullptr;
    const engine_status resolved = ResolveRuntime(engine, &runtime);
    if (resolved != ENGINE_OK)
    {
        return resolved;
    }
    CounterObject* object = nullptr;
    JobHost* host = nullptr;
    {
        LockGuard<Mutex> lock(runtime->table_mutex);
        // Counted first (and given back on a failure below): the queues of the scheduler hold a bounded number of jobs.
        if (runtime->jobs_in_flight.FetchAdd(1) >= Runtime::kMaxJobsInFlight)
        {
            runtime->jobs_in_flight.FetchSub(1);
            return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "too many jobs are queued: wait for some to finish");
        }
        Runtime::CounterSlot* slot = nullptr;
        const engine_status status = ResolveSlot(runtime->counters, counter, kKindCounter, "counter", &slot);
        if (status != ENGINE_OK)
        {
            runtime->jobs_in_flight.FetchSub(1);
            return status;
        }
        if (runtime->jobs.Get() == nullptr)
        {
            runtime->jobs = MakeUnique<JobHost>();
        }
        host = runtime->jobs.Get();
        object = slot->object.Get();
        LockGuard<Mutex> inner(object->mutex);
        ++object->pending;  // counted before the job can run: the counter cannot be destroyed under it
    }
    host->Submit(RunHostJob(fn, user, object, &runtime->jobs_in_flight));
    return ENGINE_OK;
}

engine_status CounterValueFn(engine_handle counter, u64* out)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    *out = 0;
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(counter.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    LockGuard<Mutex> lock(runtime->table_mutex);
    Runtime::CounterSlot* slot = nullptr;
    const engine_status status = ResolveSlot(runtime->counters, counter, kKindCounter, "counter", &slot);
    if (status != ENGINE_OK)
    {
        return status;
    }
    LockGuard<Mutex> inner(slot->object->mutex);
    *out = slot->object->pending;
    return ENGINE_OK;
}

engine_status CounterWaitFn(engine_handle counter, u64 timeout_ms)
{
    if (jobs::Scheduler::InsideJob())
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "counter_wait is not allowed inside a job");
    }
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(counter.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    CounterObject* object = nullptr;
    {
        LockGuard<Mutex> lock(runtime->table_mutex);
        Runtime::CounterSlot* slot = nullptr;
        const engine_status status = ResolveSlot(runtime->counters, counter, kKindCounter, "counter", &slot);
        if (status != ENGINE_OK)
        {
            return status;
        }
        object = slot->object.Get();
        ++object->waiters;  // the counter cannot be destroyed while a thread waits on it
    }
    bool reached = false;
    {
        UniqueLock<Mutex> lock(object->mutex);
        if (timeout_ms == 0xFFFFFFFFFFFFFFFFull)
        {
            object->changed.Wait(lock, [object] { return object->pending == 0; });
            reached = true;
        }
        else
        {
            reached = object->changed.WaitForMilliseconds(lock, timeout_ms, [object] { return object->pending == 0; });
        }
    }
    {
        LockGuard<Mutex> lock(runtime->table_mutex);
        --object->waiters;
    }
    return reached ? ENGINE_OK : Fail(ENGINE_ERROR_NOT_READY, "the jobs of the counter have not finished");
}

}  // namespace engine_placeholder::engine_c
