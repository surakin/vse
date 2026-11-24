#include "engine_c/instances.h"

#include "containers/shared_ptr.h"
#include "engine_c/engine_internal.h"
#include "engine_c/runtime.h"
#include "lang/scalar.h"
#include "sync/mutex.h"

namespace vse::engine_c
{

namespace
{

constexpr f64 kLimit = renderer::kMaxInstanceCoordinate;
constexpr u32 kMaxBulk = 65536;
constexpr u32 kDefaultMaxInstances = 1u << 20;

bool FiniteF(f32 v)
{
    return IsFinite(v);
}

bool FiniteD(f64 v)
{
    return v - v == 0.0 && v <= kLimit && v >= -kLimit;
}

bool ValidRotation(const f32 r[4])
{
    for (u32 i = 0; i < 4; ++i)
    {
        if (!FiniteF(r[i]))
        {
            return false;
        }
    }
    const f32 length2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3];
    return length2 > 0.99f && length2 < 1.01f;
}

Quaternion RotationOf(const f32 r[4])
{
    return Quaternion(r[0], r[1], r[2], r[3]).Normalized();
}

// Checks and converts a placement; false with the failure set.
engine_status ReadPlacement(const engine_instance_placement* in, InstancePlacement* out)
{
    if (in == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the placement is NULL");
    }
    if (in->struct_size < sizeof(engine_instance_placement))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "engine_instance_placement is smaller than this library's");
    }
    if ((in->flags & ~ENGINE_INSTANCE_HIDDEN) != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the placement has an unknown flag bit");
    }
    if (!FiniteD(in->position[0]) || !FiniteD(in->position[1]) || !FiniteD(in->position[2]) ||
        !ValidRotation(in->rotation) || !FiniteF(in->scale) || !(in->scale > 0.0f) || !FiniteF(in->max_distance) ||
        in->max_distance < 0.0f || !FiniteF(in->min_size_ratio) || in->min_size_ratio < 0.0f)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT,
                    "a placement value is not finite, a rotation is not a unit quaternion, or a scale or distance is "
                    "not valid");
    }
    out->position = Vec3d(in->position[0], in->position[1], in->position[2]);
    out->rotation = RotationOf(in->rotation);
    out->scale = in->scale;
    out->max_distance = in->max_distance;
    out->min_size_ratio = in->min_size_ratio;
    out->hidden = (in->flags & ENGINE_INSTANCE_HIDDEN) != 0;
    return ENGINE_OK;
}

engine_status ReadItemImpl(const engine_instance* in, u32 item_size, renderer::InstanceTransform* t,
                           renderer::InstanceExtra* extra)
{
    if (item_size != sizeof(engine_instance))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "`item_size` is not sizeof(engine_instance)");
    }
    if (!FiniteD(in->position[0]) || !FiniteD(in->position[1]) || !FiniteD(in->position[2]) ||
        !ValidRotation(in->rotation) || !FiniteF(in->scale[0]) || !FiniteF(in->scale[1]) || !FiniteF(in->scale[2]) ||
        !(in->scale[0] > 0.0f) || !(in->scale[1] > 0.0f) || !(in->scale[2] > 0.0f) || !FiniteF(in->tint[0]) ||
        !FiniteF(in->tint[1]) || !FiniteF(in->tint[2]) || !FiniteF(in->tint[3]))
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT,
                    "an instance value is not finite, not a unit rotation or not above 0");
    }
    t->position = Vec3d(in->position[0], in->position[1], in->position[2]);
    t->rotation = RotationOf(in->rotation);
    t->scale = Vec3(in->scale[0], in->scale[1], in->scale[2]);
    if (extra != nullptr)
    {
        extra->tint = Vec4(in->tint[0], in->tint[1], in->tint[2], in->tint[3]);
        extra->custom = in->custom;
    }
    return ENGINE_OK;
}

u64 PackId(renderer::InstanceId id)
{
    return (static_cast<u64>(id.generation) << 32) | id.index;
}

renderer::InstanceId UnpackId(u64 packed)
{
    renderer::InstanceId id;
    id.index = static_cast<u32>(packed & 0xFFFFFFFFu);
    id.generation = static_cast<u32>(packed >> 32);
    return id;
}

template <class Fn> engine_status WithInstanceWorld(engine_handle world, Fn&& fn)
{
    Runtime* runtime = CurrentRuntime();
    if (runtime == nullptr)
    {
        return Fail(world.value == 0 ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_STALE_HANDLE, "no engine exists");
    }
    LockGuard<Mutex> lock(runtime->world_mutex);
    engine_status status = ENGINE_OK;
    WorldObject* object = FindWorld(*runtime, world, &status);
    if (object == nullptr)
    {
        return status;
    }
    return fn(*object);
}

WorldInstanceSet* FindSetImpl(WorldInstances& instances, u64 id)
{
    for (usize i = 0; i < instances.sets.Size(); ++i)
    {
        if (instances.sets[i].id == id)
        {
            return &instances.sets[i];
        }
    }
    return nullptr;
}

WorldInstanceTree* FindTreeImpl(WorldInstances& instances, u64 id)
{
    for (usize i = 0; i < instances.trees.Size(); ++i)
    {
        if (instances.trees[i].id == id)
        {
            return &instances.trees[i];
        }
    }
    return nullptr;
}

engine_status MapSetStatus(renderer::InstanceStatus status)
{
    switch (status)
    {
    case renderer::InstanceStatus::Ok: return ENGINE_OK;
    case renderer::InstanceStatus::Full: return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the set is full");
    case renderer::InstanceStatus::StaleId: return Fail(ENGINE_ERROR_NOT_FOUND, "no such instance");
    case renderer::InstanceStatus::NoData: return Fail(ENGINE_ERROR_INTERNAL, "the set holds no instance data");
    case renderer::InstanceStatus::InvalidValue:
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "an instance value is not valid");
    }
    return Fail(ENGINE_ERROR_INTERNAL, "unknown instance status");
}

}  // namespace

engine_status ReadInstanceItem(const engine_instance* item, u32 item_size, renderer::InstanceTransform* transform,
                               renderer::InstanceExtra* extra)
{
    return ReadItemImpl(item, item_size, transform, extra);
}

WorldInstanceSet* FindInstanceSet(WorldInstances& instances, u64 id)
{
    return FindSetImpl(instances, id);
}

WorldInstanceTree* FindInstanceTree(WorldInstances& instances, u64 id)
{
    return FindTreeImpl(instances, id);
}

engine_status InstanceSetCreateFn(engine_handle world, const engine_instance_placement* placement, u64 mesh,
                                  u32 max_instances, u64* out_set)
{
    if (out_set == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out_set` is NULL");
    }
    *out_set = 0;
    InstancePlacement placed;
    const engine_status read = ReadPlacement(placement, &placed);
    if (read != ENGINE_OK)
    {
        return read;
    }
    if (max_instances == 0)
    {
        max_instances = kDefaultMaxInstances;
    }
    if (max_instances > renderer::kMaxInstances)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`max_instances` is above 16,777,216");
    }
    return WithInstanceWorld(world,
                             [&](WorldObject& object)
                             {
                                 WorldInstances& w = object.instances;
                                 if (w.sets.Size() + w.trees.Size() >= kMaxWorldInstanceObjects)
                                 {
                                     return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the world holds 4096 sets and trees");
                                 }
                                 renderer::InstanceSetDesc desc;
                                 desc.has_data = true;
                                 desc.max_instances = max_instances;
                                 WorldInstanceSet entry;
                                 entry.set = renderer::InstanceSet::Create(desc);
                                 if (entry.set.Get() == nullptr)
                                 {
                                     return Fail(ENGINE_ERROR_INTERNAL, "the set could not be made");
                                 }
                                 entry.id = w.next_id++;
                                 entry.mesh = mesh;
                                 entry.placement = placed;
                                 *out_set = entry.id;
                                 w.sets.Push(static_cast<WorldInstanceSet&&>(entry));
                                 return ENGINE_OK;
                             });
}

engine_status InstanceSetUpdateFn(engine_handle world, u64 set, const engine_instance_placement* placement, u64 mesh)
{
    InstancePlacement placed;
    const engine_status read = ReadPlacement(placement, &placed);
    if (read != ENGINE_OK)
    {
        return read;
    }
    return WithInstanceWorld(world,
                             [&](WorldObject& object)
                             {
                                 WorldInstanceSet* entry = FindSetImpl(object.instances, set);
                                 if (entry == nullptr)
                                 {
                                     return Fail(ENGINE_ERROR_NOT_FOUND, "no such set");
                                 }
                                 entry->mesh = mesh;
                                 entry->placement = placed;
                                 return ENGINE_OK;
                             });
}

engine_status InstanceSetAddManyFn(engine_handle world, u64 set, const engine_instance* items, u32 count, u32 item_size,
                                   u64* out_ids)
{
    if (items == nullptr && count != 0)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`items` is NULL");
    }
    if (count > kMaxBulk)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "more than 65,536 instances in one call");
    }
    if (item_size != sizeof(engine_instance))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "`item_size` is not sizeof(engine_instance)");
    }
    return WithInstanceWorld(
        world,
        [&](WorldObject& object)
        {
            WorldInstanceSet* entry = FindSetImpl(object.instances, set);
            if (entry == nullptr)
            {
                return Fail(ENGINE_ERROR_NOT_FOUND, "no such set");
            }
            renderer::InstanceSet& s = *entry->set.Get();
            if (s.Count() + count > s.Capacity())
            {
                return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the set cannot hold that many instances");
            }
            Vector<renderer::InstanceTransform> transforms;
            Vector<renderer::InstanceExtra> extras;
            transforms.Resize(count);
            extras.Resize(count);
            for (u32 i = 0; i < count; ++i)
            {
                const engine_status ok = ReadItemImpl(&items[i], item_size, &transforms[i], &extras[i]);
                if (ok != ENGINE_OK)
                {
                    return ok;  // nothing was added
                }
            }
            for (u32 i = 0; i < count; ++i)
            {
                renderer::InstanceId id;
                const renderer::InstanceStatus added = s.Add(transforms[i], &extras[i], &id);
                if (added != renderer::InstanceStatus::Ok)
                {
                    return MapSetStatus(added);  // cannot happen after the checks above
                }
                if (out_ids != nullptr)
                {
                    out_ids[i] = PackId(id);
                }
            }
            return ENGINE_OK;
        });
}

engine_status InstanceSetSetTransformFn(engine_handle world, u64 set, u64 instance, const engine_instance* item,
                                        u32 item_size)
{
    if (item == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`item` is NULL");
    }
    renderer::InstanceTransform t;
    renderer::InstanceExtra extra;
    const engine_status read = ReadItemImpl(item, item_size, &t, &extra);
    if (read != ENGINE_OK)
    {
        return read;
    }
    return WithInstanceWorld(world,
                             [&](WorldObject& object)
                             {
                                 WorldInstanceSet* entry = FindSetImpl(object.instances, set);
                                 if (entry == nullptr)
                                 {
                                     return Fail(ENGINE_ERROR_NOT_FOUND, "no such set");
                                 }
                                 const renderer::InstanceId id = UnpackId(instance);
                                 const engine_status moved = MapSetStatus(entry->set->SetTransform(id, t));
                                 if (moved != ENGINE_OK)
                                 {
                                     return moved;
                                 }
                                 return MapSetStatus(entry->set->SetData(id, extra));
                             });
}

engine_status InstanceSetRemoveFn(engine_handle world, u64 set, u64 instance)
{
    return WithInstanceWorld(world,
                             [&](WorldObject& object)
                             {
                                 WorldInstanceSet* entry = FindSetImpl(object.instances, set);
                                 if (entry == nullptr)
                                 {
                                     return Fail(ENGINE_ERROR_NOT_FOUND, "no such set");
                                 }
                                 return MapSetStatus(entry->set->Remove(UnpackId(instance)));
                             });
}

engine_status InstanceSetGetTransformFn(engine_handle world, u64 set, u64 instance, engine_instance* out, u32 item_size)
{
    if (out == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out` is NULL");
    }
    if (item_size != sizeof(engine_instance))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "`item_size` is not sizeof(engine_instance)");
    }
    return WithInstanceWorld(world,
                             [&](WorldObject& object)
                             {
                                 WorldInstanceSet* entry = FindSetImpl(object.instances, set);
                                 if (entry == nullptr)
                                 {
                                     return Fail(ENGINE_ERROR_NOT_FOUND, "no such set");
                                 }
                                 const renderer::InstanceId id = UnpackId(instance);
                                 renderer::InstanceTransform t;
                                 renderer::InstanceExtra extra;
                                 const engine_status got = MapSetStatus(entry->set->Get(id, &t));
                                 if (got != ENGINE_OK)
                                 {
                                     return got;
                                 }
                                 const engine_status data = MapSetStatus(entry->set->GetData(id, &extra));
                                 if (data != ENGINE_OK)
                                 {
                                     return data;
                                 }
                                 out->position[0] = t.position.x;
                                 out->position[1] = t.position.y;
                                 out->position[2] = t.position.z;
                                 out->rotation[0] = t.rotation.x;
                                 out->rotation[1] = t.rotation.y;
                                 out->rotation[2] = t.rotation.z;
                                 out->rotation[3] = t.rotation.w;
                                 out->scale[0] = t.scale.x;
                                 out->scale[1] = t.scale.y;
                                 out->scale[2] = t.scale.z;
                                 out->tint[0] = extra.tint.x;
                                 out->tint[1] = extra.tint.y;
                                 out->tint[2] = extra.tint.z;
                                 out->tint[3] = extra.tint.w;
                                 out->custom = extra.custom;
                                 return ENGINE_OK;
                             });
}

engine_status InstanceTreeCreateFn(engine_handle world, const engine_instance_placement* placement, const u64* meshes,
                                   const f32* mesh_bounds, u32 mesh_count, const engine_tree_node* nodes,
                                   u32 node_count, u32 node_size, u64* out_tree)
{
    if (out_tree == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`out_tree` is NULL");
    }
    *out_tree = 0;
    InstancePlacement placed;
    const engine_status read = ReadPlacement(placement, &placed);
    if (read != ENGINE_OK)
    {
        return read;
    }
    if (node_size != sizeof(engine_tree_node))
    {
        return Fail(ENGINE_ERROR_STRUCT_SIZE, "`node_size` is not sizeof(engine_tree_node)");
    }
    if (meshes == nullptr || mesh_bounds == nullptr || nodes == nullptr || mesh_count == 0 ||
        mesh_count > ENGINE_INSTANCE_MESHES_MAX || node_count == 0 || node_count > renderer::kMaxTreeNodes)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT,
                    "`meshes`, `mesh_bounds` or `nodes` is NULL, or a count is 0 or above its limit");
    }
    Vector<renderer::TreeMeshInfo> infos;
    infos.Resize(mesh_count);
    for (u32 i = 0; i < mesh_count; ++i)
    {
        const f32* b = mesh_bounds + 6 * i;
        for (u32 k = 0; k < 6; ++k)
        {
            if (!FiniteF(b[k]))
            {
                return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a mesh box is not finite");
            }
        }
        if (b[0] > b[3] || b[1] > b[4] || b[2] > b[5])
        {
            return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a mesh box has a minimum above its maximum");
        }
        infos[i].bounds = Aabb::FromMinMax(Vec3(b[0], b[1], b[2]), Vec3(b[3], b[4], b[5]));
    }
    return WithInstanceWorld(
        world,
        [&](WorldObject& object)
        {
            WorldInstances& w = object.instances;
            if (w.sets.Size() + w.trees.Size() >= kMaxWorldInstanceObjects)
            {
                return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the world holds 4096 sets and trees");
            }
            renderer::InstanceTreeBuilder builder(Span<const renderer::TreeMeshInfo>(infos.Data(), infos.Size()));
            Vector<renderer::TreeNodeId> ids;
            ids.Resize(node_count);
            for (u32 i = 0; i < node_count; ++i)
            {
                const engine_tree_node& n = nodes[i];
                renderer::InstanceTransform t;
                renderer::InstanceExtra extra;
                engine_instance as_item = {};
                for (u32 k = 0; k < 3; ++k)
                {
                    as_item.position[k] = n.position[k];
                    as_item.scale[k] = n.scale[k];
                }
                for (u32 k = 0; k < 4; ++k)
                {
                    as_item.rotation[k] = n.rotation[k];
                    as_item.tint[k] = n.tint[k];
                }
                as_item.custom = n.custom;
                const engine_status ok = ReadItemImpl(&as_item, sizeof(engine_instance), &t, &extra);
                if (ok != ENGINE_OK)
                {
                    return ok;
                }
                if (n.mesh != renderer::kTreeNoMesh && n.mesh >= mesh_count)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a node names a mesh index at or above `mesh_count`");
                }
                renderer::TreeNodeDesc desc;
                desc.position = t.position;
                desc.rotation = t.rotation;
                desc.scale = t.scale;
                desc.mesh = n.mesh;
                desc.extra = extra;
                desc.flags = n.flags;
                renderer::TreeStatus status;
                if (n.parent < 0)
                {
                    status = builder.AddRoot(desc, &ids[i]);
                }
                else if (static_cast<u32>(n.parent) < i)
                {
                    status = builder.AddChild(ids[static_cast<u32>(n.parent)], desc, &ids[i]);
                }
                else
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a node's parent is not an earlier node");
                }
                if (status == renderer::TreeStatus::Full || status == renderer::TreeStatus::TooDeep)
                {
                    return Fail(ENGINE_ERROR_LIMIT_EXCEEDED, "the tree has too many nodes or is too deep");
                }
                if (status != renderer::TreeStatus::Ok)
                {
                    return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "a node value is not valid");
                }
                if (ids[i].value != i)
                {
                    return Fail(ENGINE_ERROR_INTERNAL, "the builder numbered a node out of order");
                }
            }
            WorldInstanceTree entry;
            entry.tree = builder.Finish(nullptr);
            if (entry.tree.Get() == nullptr)
            {
                return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the tree could not be built");
            }
            entry.id = w.next_id++;
            entry.mesh_count = mesh_count;
            for (u32 i = 0; i < mesh_count; ++i)
            {
                entry.meshes[i] = meshes[i];
            }
            entry.placement = placed;
            *out_tree = entry.id;
            w.trees.Push(static_cast<WorldInstanceTree&&>(entry));
            return ENGINE_OK;
        });
}

engine_status InstanceTreeUpdateFn(engine_handle world, u64 tree, const engine_instance_placement* placement)
{
    InstancePlacement placed;
    const engine_status read = ReadPlacement(placement, &placed);
    if (read != ENGINE_OK)
    {
        return read;
    }
    return WithInstanceWorld(world,
                             [&](WorldObject& object)
                             {
                                 WorldInstanceTree* entry = FindTreeImpl(object.instances, tree);
                                 if (entry == nullptr)
                                 {
                                     return Fail(ENGINE_ERROR_NOT_FOUND, "no such tree");
                                 }
                                 entry->placement = placed;
                                 return ENGINE_OK;
                             });
}

engine_status InstanceTreeSetLocalFn(engine_handle world, u64 tree, u32 node, const engine_instance* item,
                                     u32 item_size)
{
    if (item == nullptr)
    {
        return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "`item` is NULL");
    }
    renderer::InstanceTransform t;
    const engine_status read = ReadItemImpl(item, item_size, &t, nullptr);
    if (read != ENGINE_OK)
    {
        return read;
    }
    return WithInstanceWorld(world,
                             [&](WorldObject& object)
                             {
                                 WorldInstanceTree* entry = FindTreeImpl(object.instances, tree);
                                 if (entry == nullptr)
                                 {
                                     return Fail(ENGINE_ERROR_NOT_FOUND, "no such tree");
                                 }
                                 renderer::TreeNodeId id;
                                 id.value = node;
                                 switch (entry->tree->SetLocal(id, t.position, t.rotation, t.scale))
                                 {
                                 case renderer::TreeStatus::Ok: return ENGINE_OK;
                                 case renderer::TreeStatus::UnknownNode:
                                     return Fail(ENGINE_ERROR_NOT_FOUND, "no such node");
                                 default:
                                     return Fail(ENGINE_ERROR_INVALID_ARGUMENT, "the local transform is not valid");
                                 }
                             });
}

engine_status InstanceDestroyFn(engine_handle world, u64 id)
{
    return WithInstanceWorld(world,
                             [&](WorldObject& object)
                             {
                                 WorldInstances& w = object.instances;
                                 for (usize i = 0; i < w.sets.Size(); ++i)
                                 {
                                     if (w.sets[i].id == id)
                                     {
                                         w.sets[i] = static_cast<WorldInstanceSet&&>(w.sets[w.sets.Size() - 1]);
                                         w.sets.Pop();
                                         return ENGINE_OK;
                                     }
                                 }
                                 for (usize i = 0; i < w.trees.Size(); ++i)
                                 {
                                     if (w.trees[i].id == id)
                                     {
                                         w.trees[i] = static_cast<WorldInstanceTree&&>(w.trees[w.trees.Size() - 1]);
                                         w.trees.Pop();
                                         return ENGINE_OK;
                                     }
                                 }
                                 return Fail(ENGINE_ERROR_NOT_FOUND, "no such set or tree");
                             });
}

}  // namespace vse::engine_c
