#include "script/entity_bindings.h"

#include "cstr/mem_ops.h"
#include "ecs/command_buffer.h"
#include "ecs/transform.h"
#include "script/merge_services.h"
#include "script/partition_context.h"

namespace vse::script
{

namespace
{

PartitionContext* Context()
{
    PartitionContext* context = CurrentPartitionContext();
    if (context == nullptr || context->world == nullptr)
    {
        RaiseScriptError("no world is available to this script");
        return nullptr;
    }
    return context;
}

// Reads the local transform as the script sees it: the tick-start component, then the script's own writes.
bool ReadLocal(const PartitionContext& context, ecs::Entity entity, ecs::LocalTransform* out)
{
    *out = ecs::LocalTransform();
    if (!entity.IsPlaceholder())
    {
        const ecs::LocalTransform* component = context.world->Get<ecs::LocalTransform>(entity);
        if (component == nullptr)
        {
            return false;
        }
        *out = *component;
    }
    if (context.overlay != nullptr)
    {
        const Value* position = context.overlay->Find(entity, kFieldPosition);
        if (position != nullptr)
        {
            out->position = position->AsVec3d();
        }
        const Value* rotation = context.overlay->Find(entity, kFieldRotation);
        if (rotation != nullptr)
        {
            out->rotation = rotation->AsQuat();
        }
        const Value* scale = context.overlay->Find(entity, kFieldScale);
        if (scale != nullptr)
        {
            out->scale = scale->AsVec3();
        }
    }
    return true;
}

bool NeedLocal(const PartitionContext& context, ecs::Entity entity, ecs::LocalTransform* out)
{
    if (!ReadLocal(context, entity, out))
    {
        RaiseScriptError("the entity has no transform");
        return false;
    }
    return true;
}

bool Record(PartitionContext* context, u16 kind, ecs::Entity target, const void* payload, u32 size)
{
    if (context->commands == nullptr)
    {
        RaiseScriptError("this script cannot change the world (no command buffer)");
        return false;
    }
    if (!context->commands->Record(kind, context->current_entity_key, target, payload, size))
    {
        RaiseScriptError("the command buffer is full");
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ entity

bool EntityValid(Value v)
{
    PartitionContext* context = Context();
    if (context == nullptr || v.Kind() != ValueKind::Entity)
    {
        return false;
    }
    const ecs::Entity e = v.AsEntity();
    if (e.IsPlaceholder())
    {
        return context->commands != nullptr && context->commands->IsReserved(e);
    }
    return context->world->Exists(e);
}

ecs::Entity EntitySelf()
{
    PartitionContext* context = Context();
    return context == nullptr ? ecs::Entity() : ecs::Entity::FromU64(context->current_entity_key);
}

ecs::Entity EntitySpawn(Value prefab)
{
    PartitionContext* context = Context();
    if (context == nullptr)
    {
        return ecs::Entity();
    }
    StringView name;
    if (prefab.Kind() == ValueKind::Text)
    {
        name = prefab.Text();
        const SceneServices* scene = context->scene;
        if (scene == nullptr || scene->has_prefab == nullptr || !scene->has_prefab(scene->user, name))
        {
            RaiseScriptError("no such prefab");
            return ecs::Entity();
        }
    }
    else if (prefab.Kind() != ValueKind::Nil)
    {
        RaiseScriptError("the prefab must be a name or nil");
        return ecs::Entity();
    }
    if (context->commands == nullptr)
    {
        RaiseScriptError("this script cannot change the world (no command buffer)");
        return ecs::Entity();
    }
    const ecs::Entity placeholder = context->commands->Reserve();
    if (!Record(context, kCommandSpawn, placeholder, name.Data(), static_cast<u32>(name.Length())))
    {
        return ecs::Entity();
    }
    return placeholder;
}

void EntityDestroy(ecs::Entity e)
{
    PartitionContext* context = Context();
    if (context != nullptr)
    {
        Record(context, kCommandDestroy, e, nullptr, 0);
    }
}

void EntitySetParent(ecs::Entity child, Value parent)
{
    PartitionContext* context = Context();
    if (context == nullptr)
    {
        return;
    }
    ecs::Entity parent_entity;
    if (parent.Kind() == ValueKind::Entity)
    {
        parent_entity = parent.AsEntity();
        const bool alive = parent_entity.IsPlaceholder()
                               ? (context->commands != nullptr && context->commands->IsReserved(parent_entity))
                               : context->world->Exists(parent_entity);
        if (!alive)
        {
            RaiseScriptError("the parent no longer exists");
            return;
        }
    }
    else if (parent.Kind() != ValueKind::Nil)
    {
        RaiseScriptError("the parent must be an entity or nil");
        return;
    }
    Record(context, kCommandSetParent, child, &parent_entity, sizeof(parent_entity));
}

Value EntityName(ecs::Entity e)
{
    PartitionContext* context = Context();
    if (context == nullptr || e.IsPlaceholder())
    {
        return Value();
    }
    const SceneServices* scene = context->scene;
    if (scene == nullptr || scene->name_of == nullptr)
    {
        return Value();
    }
    char buffer[160];
    const usize length = scene->name_of(scene->user, *context->world, e, buffer, sizeof(buffer));
    return length == 0 ? Value()
                       : Value::FromText(StringView(buffer, length < sizeof(buffer) ? length : sizeof(buffer)));
}

Value EntityFind(StringView name)
{
    PartitionContext* context = Context();
    if (context == nullptr)
    {
        return Value();
    }
    const SceneServices* scene = context->scene;
    if (scene == nullptr || scene->find == nullptr)
    {
        return Value();
    }
    const ecs::Entity found = scene->find(scene->user, *context->world, name);
    return found.IsValid() ? Value::FromEntity(found) : Value();
}

// ------------------------------------------------------------------ transform

Value TransformGet(ecs::Entity e)
{
    PartitionContext* context = Context();
    ecs::LocalTransform local;
    if (context == nullptr || !NeedLocal(*context, e, &local))
    {
        return Value();
    }
    Value out = Value::MakeMap();
    out.Set("position", Value::FromVec3d(local.position));
    out.Set("rotation", Value::FromQuat(local.rotation));
    out.Set("scale", Value::FromVec3(local.scale));
    return out;
}

Vec3d TransformPosition(ecs::Entity e)
{
    PartitionContext* context = Context();
    ecs::LocalTransform local;
    return context != nullptr && NeedLocal(*context, e, &local) ? local.position : Vec3d();
}

Quaternion TransformRotation(ecs::Entity e)
{
    PartitionContext* context = Context();
    ecs::LocalTransform local;
    return context != nullptr && NeedLocal(*context, e, &local) ? local.rotation : Quaternion::Identity();
}

Vec3 TransformScale(ecs::Entity e)
{
    PartitionContext* context = Context();
    ecs::LocalTransform local;
    return context != nullptr && NeedLocal(*context, e, &local) ? local.scale : Vec3(1.0f, 1.0f, 1.0f);
}

// The world matrix as of the tick start (the propagation system writes it after the merge); a placeholder has none.
Mat4 TransformWorld(ecs::Entity e)
{
    PartitionContext* context = Context();
    if (context == nullptr)
    {
        return Mat4::Identity();
    }
    if (e.IsPlaceholder())
    {
        ecs::LocalTransform local;
        return NeedLocal(*context, e, &local) ? local.ToMatrix() : Mat4::Identity();
    }
    const ecs::WorldTransform* world = context->world->Get<ecs::WorldTransform>(e);
    if (world == nullptr)
    {
        RaiseScriptError("the entity has no world transform");
        return Mat4::Identity();
    }
    return world->matrix;
}

void PutOverlay(PartitionContext* context, ecs::Entity e, u32 field, Value value)
{
    if (context->overlay != nullptr)
    {
        context->overlay->Put(e, field, Move(value));
    }
}

bool Finite3(Vec3d v)
{
    return v.x > -1.0e30 && v.x < 1.0e30 && v.y > -1.0e30 && v.y < 1.0e30 && v.z > -1.0e30 && v.z < 1.0e30;
}

bool Finite3(Vec3 v)
{
    return v.x > -1.0e30f && v.x < 1.0e30f && v.y > -1.0e30f && v.y < 1.0e30f && v.z > -1.0e30f && v.z < 1.0e30f;
}

void TransformSetPosition(ecs::Entity e, Vec3d position)
{
    PartitionContext* context = Context();
    ecs::LocalTransform local;
    if (context == nullptr || !NeedLocal(*context, e, &local))
    {
        return;
    }
    if (!Finite3(position))
    {
        RaiseScriptError("the position must be finite");
        return;
    }
    if (Record(context, kCommandSetPosition, e, &position, sizeof(position)))
    {
        PutOverlay(context, e, kFieldPosition, Value::FromVec3d(position));
    }
}

void TransformSetRotation(ecs::Entity e, Quaternion rotation)
{
    PartitionContext* context = Context();
    ecs::LocalTransform local;
    if (context == nullptr || !NeedLocal(*context, e, &local))
    {
        return;
    }
    const f32 length_squared = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z +
                               rotation.w * rotation.w;
    if (!(length_squared > 1.0e-12f && length_squared < 1.0e12f))
    {
        RaiseScriptError("the rotation must be a non-zero finite quaternion");
        return;
    }
    const f32 inverse = 1.0f / Sqrt(length_squared);
    rotation = Quaternion(rotation.x * inverse, rotation.y * inverse, rotation.z * inverse, rotation.w * inverse);
    if (Record(context, kCommandSetRotation, e, &rotation, sizeof(rotation)))
    {
        PutOverlay(context, e, kFieldRotation, Value::FromQuat(rotation));
    }
}

void TransformSetScale(ecs::Entity e, Vec3 scale)
{
    PartitionContext* context = Context();
    ecs::LocalTransform local;
    if (context == nullptr || !NeedLocal(*context, e, &local))
    {
        return;
    }
    if (!Finite3(scale))
    {
        RaiseScriptError("the scale must be finite");
        return;
    }
    if (Record(context, kCommandSetScale, e, &scale, sizeof(scale)))
    {
        PutOverlay(context, e, kFieldScale, Value::FromVec3(scale));
    }
}

void TransformTranslate(ecs::Entity e, Vec3d delta)
{
    PartitionContext* context = Context();
    ecs::LocalTransform local;
    if (context == nullptr || !NeedLocal(*context, e, &local))
    {
        return;
    }
    TransformSetPosition(e, local.position + delta);
}

// ------------------------------------------------------------------ the merge

template <class T> bool ReadPayload(Span<const u8> payload, T* out)
{
    if (payload.Size() != sizeof(T))
    {
        return false;
    }
    MemCopy(out, payload.Data(), sizeof(T));
    return true;
}

bool ApplySetPosition(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    Vec3d value;
    ecs::LocalTransform* local = ReadPayload(payload, &value) ? context.World()->Get<ecs::LocalTransform>(target)
                                                              : nullptr;
    if (local == nullptr)
    {
        return false;
    }
    local->position = value;
    return true;
}

bool ApplySetRotation(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    Quaternion value;
    ecs::LocalTransform* local = ReadPayload(payload, &value) ? context.World()->Get<ecs::LocalTransform>(target)
                                                              : nullptr;
    if (local == nullptr)
    {
        return false;
    }
    local->rotation = value;
    return true;
}

bool ApplySetScale(MergeContext& context, ecs::Entity target, Span<const u8> payload)
{
    Vec3 value;
    ecs::LocalTransform* local = ReadPayload(payload, &value) ? context.World()->Get<ecs::LocalTransform>(target)
                                                              : nullptr;
    if (local == nullptr)
    {
        return false;
    }
    local->scale = value;
    return true;
}

bool ApplySpawn(MergeContext& context, ecs::Entity placeholder, Span<const u8> payload)
{
    const ecs::Entity created = ecs::CreateTransformEntity(*context.World(), ecs::LocalTransform());
    if (!created.IsValid())
    {
        return false;
    }
    context.Bind(placeholder, created);
    if (payload.Size() != 0)
    {
        const MergeServices* services = static_cast<const MergeServices*>(context.User());
        const PrefabSource* prefabs = services != nullptr ? services->prefabs : nullptr;
        if (prefabs == nullptr || prefabs->spawn == nullptr ||
            !prefabs->spawn(prefabs->user, StringView(reinterpret_cast<const char*>(payload.Data()), payload.Size()),
                            context.World(), created))
        {
            return false;
        }
    }
    return true;
}

bool ApplyDestroy(MergeContext& context, ecs::Entity target, Span<const u8>)
{
    ecs::CommandBuffer commands;
    ecs::DestroyRecursive(*context.World(), commands, target);
    context.World()->Apply(commands);
    return true;
}

bool ApplySetParent(MergeContext& context, ecs::Entity child, Span<const u8> payload)
{
    ecs::Entity parent;
    if (!ReadPayload(payload, &parent))
    {
        return false;
    }
    parent = context.Resolve(parent);
    if (!parent.IsValid())
    {
        return ecs::ClearParent(*context.World(), child) == ecs::HierarchyStatus::Ok;
    }
    return ecs::SetParent(*context.World(), child, parent) == ecs::HierarchyStatus::Ok;
}

}  // namespace

bool RegisterEntityCommandKinds(CommandKindTable* kinds)
{
    bool ok = true;
    ok &= kinds->Register(kCommandSetPosition, CommandKind{"transform.set_position", &ApplySetPosition, false, true});
    ok &= kinds->Register(kCommandSetRotation, CommandKind{"transform.set_rotation", &ApplySetRotation, false, true});
    ok &= kinds->Register(kCommandSetScale, CommandKind{"transform.set_scale", &ApplySetScale, false, true});
    ok &= kinds->Register(kCommandSpawn, CommandKind{"entity.spawn", &ApplySpawn, true, false});
    ok &= kinds->Register(kCommandDestroy, CommandKind{"entity.destroy", &ApplyDestroy, false, false});
    ok &= kinds->Register(kCommandSetParent, CommandKind{"entity.set_parent", &ApplySetParent, false, true});
    return ok;
}

bool RegisterEntityBindings(BindingRegistry* registry)
{
    bool ok = true;
    ok &= registry->Add<&EntityValid>(
        "entity", "valid", ThreadRule::WorldRead, "e",
        "Returns whether `e` is an entity that exists (as of the tick start) or that this "
        "partition spawned this tick.");
    ok &= registry->Add<&EntitySelf>("entity", "self", ThreadRule::WorldRead, "",
                                     "Returns the entity whose script is running.");
    ok &= registry->Add<&EntitySpawn>(
        "entity", "spawn", ThreadRule::WorldWrite, "prefab",
        "Spawns an entity at the sync point and returns its handle now (usable in this "
        "tick's other calls); `prefab` is a prefab name or nil for an empty entity with a "
        "transform.");
    ok &= registry->Add<&EntityDestroy>("entity", "destroy", ThreadRule::WorldWrite, "e",
                                        "Destroys the entity and everything below it at the sync point.");
    ok &= registry->Add<&EntitySetParent>(
        "entity", "set_parent", ThreadRule::WorldWrite, "child, parent",
        "Makes `parent` the parent of `child` (nil makes it a root) at the sync point; a "
        "cycle or too deep a hierarchy is refused there and counted.");
    ok &= registry->Add<&EntityName>("entity", "name", ThreadRule::WorldRead, "e",
                                     "Returns the entity's name, or nil when it has none.");
    ok &= registry->Add<&EntityFind>("entity", "find", ThreadRule::WorldRead, "name",
                                     "Returns the entity with this name, or nil. Costs a search of the scene: keep the "
                                     "result.");
    ok &= registry->Add<&TransformGet>(
        "transform", "get", ThreadRule::WorldRead, "e",
        "Returns a table with the local `position`, `rotation` and `scale`, including this "
        "script's own writes this tick.");
    ok &= registry->Add<&TransformPosition>("transform", "position", ThreadRule::WorldRead, "e",
                                            "Returns the local position.");
    ok &= registry->Add<&TransformRotation>("transform", "rotation", ThreadRule::WorldRead, "e",
                                            "Returns the local rotation.");
    ok &= registry->Add<&TransformScale>("transform", "scale", ThreadRule::WorldRead, "e", "Returns the local scale.");
    ok &= registry->Add<&TransformWorld>("transform", "world", ThreadRule::WorldRead, "e",
                                         "Returns the world matrix (16 numbers, column-major) as of the tick start.");
    ok &= registry->Add<&TransformSetPosition>(
        "transform", "set_position", ThreadRule::WorldWrite, "e, position",
        "Sets the local position at the sync point; this script sees it at once.");
    ok &= registry->Add<&TransformSetRotation>(
        "transform", "set_rotation", ThreadRule::WorldWrite, "e, rotation",
        "Sets the local rotation (normalized) at the sync point; this script sees "
        "it at once.");
    ok &= registry->Add<&TransformSetScale>("transform", "set_scale", ThreadRule::WorldWrite, "e, scale",
                                            "Sets the local scale at the sync point; this script sees it at once.");
    ok &= registry->Add<&TransformTranslate>("transform", "translate", ThreadRule::WorldWrite, "e, delta",
                                             "Adds `delta` to the local position (as this script sees it).");
    return ok;
}

}  // namespace vse::script
