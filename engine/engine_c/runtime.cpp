// The runtime of the C API (Phase 23 Day 349): worlds and entities behind a property API, the frame, counters and jobs.
// Every function checks its handle and its arguments, copies in and out (no pointer into a world escapes) and returns
// an `engine_status`; nothing of the engine's C++ types, allocations or exceptions crosses the boundary.
#include "engine_c/runtime.h"
#include "engine_c/surface.h"

#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "ecs/transform.h"
#include "engine_c/engine_internal.h"
#include "engine_c/handles.h"
#include "engine_c/resources.h"
#include "engine_c/engine_objects.h"
#include "jobs/scheduler.h"
#include "jobs/topology.h"
#include "log/diagnostics_hooks.h"
#include "math/quaternion.h"
#include "mem/heap_allocator.h"
#include "os/os.h"

namespace engine_placeholder::engine_c
{

namespace
{

// The components the C API owns (the transform pair is the ECS's own).
struct MeshReference
{
    u64 mesh = 0;
    u32 material = 0;
};

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
    }
    return "component";
}

bool KnownKind(u32 kind)
{
    return kind >= ENGINE_COMPONENT_TRANSFORM && kind <= ENGINE_COMPONENT_MESH_REFERENCE;
}

ecs::ComponentId IdOfKind(engine_component_kind kind)
{
    switch (kind)
    {
    case ENGINE_COMPONENT_TRANSFORM: return ecs::ComponentOf<ecs::LocalTransform>();
    case ENGINE_COMPONENT_WORLD_TRANSFORM: return ecs::ComponentOf<ecs::WorldTransform>();
    case ENGINE_COMPONENT_NAME: return ecs::ComponentOf<EntityName>();
    case ENGINE_COMPONENT_MESH_REFERENCE: return ecs::ComponentOf<MeshReference>();
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

u32 NextGeneration()
{
    return g_generation.FetchAdd(1) + 1;
}

u32 LastGeneration()
{
    return g_generation.Load();
}

WorldObject::WorldObject()
{
    world.SetResource(ecs::TransformReport{});
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
    if (stats != nullptr && StructSizeOf(stats) < sizeof(engine_frame_stats))
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
    const u64 budget_ns = info->budget_us * 1000ull;
    engine_frame_stats result = {};
    result.struct_size = sizeof(engine_frame_stats);
    if (ResourceHost* resources = runtime->ResourcesIfCreated())
    {
        resources->PumpLoads();  // builds what asynchronous loads have read, within a small budget
    }
    if (SurfaceHost* surfaces = runtime->SurfacesIfCreated())
    {
        surfaces->RenderAll();  // the host's windows, one frame each, before the worlds step
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
        object->schedule.Run(object->world, static_cast<f32>(info->delta_seconds));
        ++result.worlds_run;
        result.entities += object->world.Stats().entities;
    }
    result.elapsed_us = (os::MonotonicTimeNanos() - start) / 1000;
    if (stats != nullptr)
    {
        *stats = result;
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

template <class T> engine_status SetOrAdd(ecs::World& world, ecs::Entity entity, const T& value)
{
    const ecs::Status status = world.Has<T>(entity) ? world.Set<T>(entity, value) : world.Add<T>(entity, value);
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
    engine_status checked = ENGINE_OK;
    if (kind == ENGINE_COMPONENT_TRANSFORM)
    {
        checked = CheckInStruct(value, sizeof(engine_transform), "engine_transform");
    }
    else
    {
        checked = CheckInStruct(value, sizeof(engine_mesh_reference), "engine_mesh_reference");
    }
    if (checked != ENGINE_OK)
    {
        return checked;
    }
    return WithWorld(world, true, key,
                     [&](WorldObject& object, ecs::Entity entity)
                     {
                         if (kind == ENGINE_COMPONENT_MESH_REFERENCE)
                         {
                             const engine_mesh_reference* in = static_cast<const engine_mesh_reference*>(value);
                             MeshReference reference;
                             reference.mesh = in->mesh;
                             reference.material = in->material;
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
                         const float length_squared = rotation.x * rotation.x + rotation.y * rotation.y +
                                                      rotation.z * rotation.z + rotation.w * rotation.w;
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
    const usize wanted = kind == ENGINE_COMPONENT_TRANSFORM         ? sizeof(engine_transform)
                         : kind == ENGINE_COMPONENT_WORLD_TRANSFORM ? sizeof(engine_world_transform)
                                                                    : sizeof(engine_mesh_reference);
    const char* what = kind == ENGINE_COMPONENT_TRANSFORM         ? "engine_transform"
                       : kind == ENGINE_COMPONENT_WORLD_TRANSFORM ? "engine_world_transform"
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
                         const MeshReference* reference = view.Peek<MeshReference>(entity);
                         if (reference == nullptr)
                         {
                             return Fail(ENGINE_ERROR_NOT_FOUND, "the entity has no mesh reference");
                         }
                         engine_mesh_reference result = {};
                         result.struct_size = sizeof(engine_mesh_reference);
                         result.mesh = reference->mesh;
                         result.material = reference->material;
                         MemCopy(out, &result, sizeof(result));
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
                         *out = object.world.HasById(entity, IdOfKind(kind)) ? 1 : 0;
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
                         ecs::ComponentId wanted[4];
                         usize wanted_count = 0;
                         for (u32 kind = ENGINE_COMPONENT_TRANSFORM; kind <= ENGINE_COMPONENT_MESH_REFERENCE; ++kind)
                         {
                             if ((mask & ENGINE_COMPONENT_BIT(kind)) != 0)
                             {
                                 wanted[wanted_count++] = IdOfKind(static_cast<engine_component_kind>(kind));
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
