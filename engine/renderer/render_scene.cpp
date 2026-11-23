#include "renderer/render_scene.h"

#include "algorithm.h"
#include "containers/span.h"
#include "lang/scalar.h"
#include "lang/new.h"

#include "log/assert.h"
#include "spatial/query.h"
#include "spatial/spatial_set.h"

namespace vse::renderer
{

namespace
{

void CheckLight(const Light& light)
{
    ENGINE_ASSERT(IsFinite(light.intensity) && light.intensity >= 0.0f,
                  "light: intensity must be finite and not negative");
    ENGINE_ASSERT(IsFinite(light.color.x) && IsFinite(light.color.y) && IsFinite(light.color.z),
                  "light: the color must be finite");
    ENGINE_ASSERT(IsFinite(light.position.x) && IsFinite(light.position.y) && IsFinite(light.position.z),
                  "light: the position must be finite");
    ENGINE_ASSERT(IsFinite(light.direction.x) && IsFinite(light.direction.y) && IsFinite(light.direction.z),
                  "light: the direction must be finite");
    ENGINE_ASSERT((light.type != LightType::Point && light.type != LightType::Spot) ||
                      (IsFinite(light.radius) && light.radius > 0.0f),
                  "light: a point or spot light needs a positive radius");
    ENGINE_ASSERT(light.type != LightType::Spot ||
                      (IsFinite(light.inner_cone) && IsFinite(light.outer_cone) && light.inner_cone >= 0.0f &&
                       light.inner_cone < light.outer_cone && light.outer_cone <= 1.5707964f),
                  "light: a spot light needs 0 <= inner_cone < outer_cone <= pi/2");
}

// Next generation for a slot, skipping 0 (which means "invalid").
u32 NextGeneration(u32 g)
{
    return g + 1 == 0 ? 1 : g + 1;
}

}  // namespace

RenderScene::RenderScene(const RenderSceneDesc& desc)
{
    ENGINE_ASSERT(desc.max_objects > 0 && desc.max_lights > 0 && desc.max_lights <= kMaxLights,
                  "RenderScene: capacities must be at least 1 and the light capacity at most kMaxLights");
    object_capacity_ = desc.max_objects;
    light_capacity_ = desc.max_lights;
    objects_.Resize(object_capacity_);
    lights_.Resize(light_capacity_);
    terrain_capacity_ = desc.max_terrains;
    terrains_.Resize(terrain_capacity_);
    instanced_capacity_ = desc.max_instanced;
    instanced_.Resize(instanced_capacity_);
}

struct RenderScene::CullIndex
{
    spatial::SpatialSet set;
    Vector<spatial::SetHandle> handle;  // per object slot
};

RenderScene::~RenderScene() = default;  // after CullIndex is complete: the UniquePtr deleter needs the full type

namespace
{
bool UsableBox(const Aabb& box)
{
    return IsFinite(box.min.x) && IsFinite(box.min.y) && IsFinite(box.min.z) && IsFinite(box.max.x) &&
           IsFinite(box.max.y) && IsFinite(box.max.z) && !box.IsEmpty();
}
}  // namespace

void RenderScene::IndexObject(u32 slot)
{
    if (cull_index_ == nullptr)
    {
        cull_index_ = MakeUnique<CullIndex>();
    }
    CullIndex& index = *cull_index_;
    if (index.handle.Size() <= slot)
    {
        const usize old = index.handle.Size();
        index.handle.Resize(slot + 1);
        for (usize i = old; i < index.handle.Size(); ++i)
        {
            index.handle[i] = spatial::kInvalidSetHandle;
        }
    }
    const Aabb& box = objects_[slot].object.bounds.box;
    index.handle[slot] = UsableBox(box) ? index.set.AddDynamic(box, slot, 0) : spatial::kInvalidSetHandle;
    if (index.handle[slot] == spatial::kInvalidSetHandle)
    {  // an unusable box, or a full set: the object is tested one by one
        ++unindexed_;
    }
}

void RenderScene::UnindexObject(u32 slot)
{
    CullIndex& index = *cull_index_;
    if (index.handle[slot] != spatial::kInvalidSetHandle)
    {
        index.set.Remove(index.handle[slot]);
        index.handle[slot] = spatial::kInvalidSetHandle;
    }
    else
    {
        --unindexed_;
    }
}

void RenderScene::OptimizeCulling(u32 budget)
{
    if (cull_mode_ == CullMode::Bvh && cull_index_ != nullptr)
    {
        cull_index_->set.Optimize(budget);
    }
}

namespace
{

// The bind-pose bounds of a mesh, inflated about their center by a skinned object's bounds scale.
Bounds SkinBounds(const GpuMesh& mesh, f32 scale)
{
    Bounds b = mesh.GetBounds();
    if (mesh.Skinned() && scale != 1.0f && scale > 0.0f)
    {
        const Vec3 center = b.box.Center();
        b.box.min = center + (b.box.min - center) * scale;
        b.box.max = center + (b.box.max - center) * scale;
        b.sphere.radius *= scale;
    }
    return b;
}

// An object's bounds in object space: its pose's when it is skinned and has a palette the mesh can bound
// (2026-11-20), else the mesh's, scaled for a skinned mesh by `skin_bounds_scale`.
Bounds ObjectBounds(const GpuMesh& mesh, f32 scale, const f32* palette, u32 count)
{
    Bounds posed;
    if (mesh.Skinned() && palette != nullptr && mesh.PosedBounds(palette, count, &posed))
    {
        return posed;
    }
    return SkinBounds(mesh, scale);
}

}  // namespace

ObjectId RenderScene::AddObject(const RenderObjectDesc& desc)
{
    ENGINE_ASSERT(desc.mesh != nullptr && desc.mesh->Valid(), "AddObject: the mesh is null or empty");
    ENGINE_ASSERT(desc.joint_matrices == nullptr ||
                      (desc.mesh->Skinned() && desc.joint_count > 0 && desc.joint_count <= 256),
                  "AddObject: a palette needs a skinned mesh and 1 to 256 matrices");
    ENGINE_ASSERT(desc.index_count == 0 ? desc.first_index == 0
                                        : (desc.first_index <= desc.mesh->IndexCount() &&
                                           desc.index_count <= desc.mesh->IndexCount() - desc.first_index),
                  "AddObject: the index range lies outside the mesh");
    u32 slot;
    if (free_object_ != kNoSlot)
    {
        slot = free_object_;
        free_object_ = objects_[slot].next_free;
    }
    else if (slots_used_ < object_capacity_)
    {
        slot = slots_used_++;
    }
    else
    {
        return ObjectId{};
    }
    ObjectSlot& s = objects_[slot];
    s.alive = true;
    s.object.mesh = desc.mesh;
    s.object.material = desc.material;
    s.object.transform = desc.transform;
    s.object.flags = desc.flags;
    s.object.joint_matrices = desc.joint_matrices;
    s.object.joint_count = desc.joint_count;
    ENGINE_ASSERT(desc.morph_weights == nullptr || desc.morph_weight_count <= desc.mesh->MorphTargetCount(),
                  "AddObject: more morph weights than the mesh has targets");
    s.object.morph_weights = desc.morph_weights;
    s.object.morph_weight_count = desc.morph_weights != nullptr ? desc.morph_weight_count : 0;
    s.object.skin_bounds_scale = desc.skin_bounds_scale;
    s.object.first_index = desc.first_index;
    s.object.index_count = desc.index_count;
    s.local = ObjectBounds(*desc.mesh, desc.skin_bounds_scale, desc.joint_matrices, desc.joint_count);
    s.object.bounds = TransformBounds(s.local, desc.transform);
    ++object_count_;
    IndexObject(slot);
    return ObjectId{slot, s.generation};
}

void RenderScene::RemoveObject(ObjectId id)
{
    ENGINE_ASSERT(FindObject(id) != nullptr, "RemoveObject: the id is invalid or stale");
    UnindexObject(id.index);
    ObjectSlot& s = objects_[id.index];
    s.alive = false;
    s.object = RenderObject();
    s.local = Bounds();
    s.generation = NextGeneration(s.generation);
    s.next_free = free_object_;
    free_object_ = id.index;
    --object_count_;
}

void RenderScene::SetTransform(ObjectId id, const Mat4& transform)
{
    ENGINE_ASSERT(FindObject(id) != nullptr, "SetTransform: the id is invalid or stale");
    RenderObject& o = objects_[id.index].object;
    o.transform = transform;
    MoveBounds(id.index);
}

void RenderScene::MoveBounds(u32 slot)
{
    RenderObject& o = objects_[slot].object;
    o.bounds = TransformBounds(objects_[slot].local, o.transform);
    CullIndex& index = *cull_index_;
    if (index.handle[slot] != spatial::kInvalidSetHandle && UsableBox(o.bounds.box))
    {
        index.set.Move(index.handle[slot], o.bounds.box);
    }
    else
    {
        UnindexObject(slot);
        IndexObject(slot);
    }
}

void RenderScene::SetJointMatrices(ObjectId id, const f32* matrices, u32 count)
{
    ENGINE_ASSERT(FindObject(id) != nullptr, "SetJointMatrices: the id is invalid or stale");
    RenderObject& o = objects_[id.index].object;
    ENGINE_ASSERT(o.mesh->Skinned() && (matrices == nullptr || (count > 0 && count <= 256)),
                  "SetJointMatrices: the mesh is rigid or the count is outside 1 to 256");
    o.joint_matrices = matrices;
    o.joint_count = matrices != nullptr ? count : 0;
    objects_[id.index].local = ObjectBounds(*o.mesh, o.skin_bounds_scale, o.joint_matrices, o.joint_count);
    MoveBounds(id.index);
}

void RenderScene::UpdatePoseBounds(ObjectId id)
{
    ENGINE_ASSERT(FindObject(id) != nullptr, "UpdatePoseBounds: the id is invalid or stale");
    RenderObject& o = objects_[id.index].object;
    if (o.mesh->Skinned())
    {
        objects_[id.index].local = ObjectBounds(*o.mesh, o.skin_bounds_scale, o.joint_matrices, o.joint_count);
        MoveBounds(id.index);
    }
}

void RenderScene::SetMorphWeights(ObjectId id, const f32* weights, u32 count)
{
    ENGINE_ASSERT(FindObject(id) != nullptr, "SetMorphWeights: the id is invalid or stale");
    RenderObject& o = objects_[id.index].object;
    ENGINE_ASSERT(weights == nullptr || count <= o.mesh->MorphTargetCount(),
                  "SetMorphWeights: more weights than the mesh has targets");
    o.morph_weights = weights;
    o.morph_weight_count = weights != nullptr ? count : 0;
}

void RenderScene::SetFlags(ObjectId id, u32 flags)
{
    ENGINE_ASSERT(FindObject(id) != nullptr, "SetFlags: the id is invalid or stale");
    objects_[id.index].object.flags = flags;
}

const RenderObject* RenderScene::FindObject(ObjectId id) const
{
    if (!id.IsValid() || id.index >= slots_used_)
    {
        return nullptr;
    }
    const ObjectSlot& s = objects_[id.index];
    return s.alive && s.generation == id.generation ? &s.object : nullptr;
}

bool RenderScene::SlotAlive(u32 slot) const
{
    ENGINE_ASSERT(slot < slots_used_, "SlotAlive: slot out of range");
    return objects_[slot].alive;
}

const RenderObject& RenderScene::ObjectAt(u32 slot) const
{
    ENGINE_ASSERT(slot < slots_used_ && objects_[slot].alive, "ObjectAt: slot out of range or dead");
    return objects_[slot].object;
}

LightId RenderScene::AddLight(const Light& light)
{
    CheckLight(light);
    u32 slot;
    if (free_light_ != kNoSlot)
    {
        slot = free_light_;
        free_light_ = lights_[slot].next_free;
    }
    else if (light_slots_used_ < light_capacity_)
    {
        slot = light_slots_used_++;
    }
    else
    {
        return LightId{};
    }
    LightSlot& s = lights_[slot];
    s.alive = true;
    s.light = light;
    ++light_count_;
    return LightId{slot, s.generation};
}

void RenderScene::RemoveLight(LightId id)
{
    ENGINE_ASSERT(FindLight(id) != nullptr, "RemoveLight: the id is invalid or stale");
    LightSlot& s = lights_[id.index];
    s.alive = false;
    s.light = Light();
    s.generation = NextGeneration(s.generation);
    s.next_free = free_light_;
    free_light_ = id.index;
    --light_count_;
}

void RenderScene::SetLight(LightId id, const Light& light)
{
    ENGINE_ASSERT(FindLight(id) != nullptr, "SetLight: the id is invalid or stale");
    CheckLight(light);
    lights_[id.index].light = light;
}

const Light* RenderScene::FindLight(LightId id) const
{
    if (!id.IsValid() || id.index >= light_slots_used_)
    {
        return nullptr;
    }
    const LightSlot& s = lights_[id.index];
    return s.alive && s.generation == id.generation ? &s.light : nullptr;
}

bool RenderScene::LightSlotAlive(u32 slot) const
{
    ENGINE_ASSERT(slot < light_slots_used_, "LightSlotAlive: slot out of range");
    return lights_[slot].alive;
}

const Light& RenderScene::LightAt(u32 slot) const
{
    ENGINE_ASSERT(slot < light_slots_used_ && lights_[slot].alive, "LightAt: slot out of range or dead");
    return lights_[slot].light;
}

u32 RenderScene::Cull(const Frustum& frustum, u32* out_slots, u32 capacity) const
{
    ENGINE_ASSERT(out_slots != nullptr && capacity >= slots_used_,
                  "Cull: the output array is null or smaller than the slot count");
    if (cull_mode_ == CullMode::Bvh && cull_index_ != nullptr)
    {
        return CullBvh(frustum, out_slots);
    }
    return CullFlat(frustum, out_slots);
}

u32 RenderScene::CullBvh(const Frustum& frustum, u32* out_slots) const
{
    u32 count = 0;
    const spatial::QueryStats stats = spatial::Overlap(cull_index_->set, Frustumd(frustum), spatial::QueryFilter(),
                                                       [&](const spatial::Item& item)
                                                       {
                                                           const u32 slot = static_cast<u32>(item.key);
                                                           const ObjectSlot& s = objects_[slot];
                                                           if ((s.object.flags & ObjectFlags::kHidden) == 0 &&
                                                               frustum.Visible(s.object.bounds))
                                                           {
                                                               out_slots[count++] = slot;
                                                           }
                                                           return true;
                                                       });
    if (stats.refused_input)
    {  // a frustum the tree refuses (a plane that is not finite): the flat scan decides, so both modes agree
        return CullFlat(frustum, out_slots);
    }
    if (unindexed_ != 0)
    {  // objects with a box the hierarchy cannot hold are tested one by one, as the flat scan does
        for (u32 slot = 0; slot < slots_used_; ++slot)
        {
            const ObjectSlot& s = objects_[slot];
            if (s.alive && cull_index_->handle[slot] == spatial::kInvalidSetHandle &&
                (s.object.flags & ObjectFlags::kHidden) == 0 && frustum.Visible(s.object.bounds))
            {
                out_slots[count++] = slot;
            }
        }
    }
    // Slots in increasing order, as the flat scan gives them.
    Sort(Span<u32>(out_slots, count), [](u32 a, u32 b) { return a < b; });
    return count;
}

u32 RenderScene::CullFlat(const Frustum& frustum, u32* out_slots) const
{
    u32 count = 0;
    for (u32 slot = 0; slot < slots_used_; ++slot)
    {
        const ObjectSlot& s = objects_[slot];
        if (!s.alive || (s.object.flags & ObjectFlags::kHidden) != 0)
        {
            continue;
        }
        if (frustum.Visible(s.object.bounds))
        {
            out_slots[count++] = slot;
        }
    }
    return count;
}

namespace
{
void CheckTerrain(const TerrainObject& t)
{
    ENGINE_ASSERT(t.source && t.source->Valid(), "terrain: the source is missing or not valid");
    ENGINE_ASSERT(TerrainWorldValid(t.world), "terrain: the world matrix is not a translation and a turn about y");
}
}  // namespace

TerrainId RenderScene::AddTerrain(const TerrainObject& terrain)
{
    CheckTerrain(terrain);
    u32 slot;
    if (free_terrain_ != kNoSlot)
    {
        slot = free_terrain_;
        free_terrain_ = terrains_[slot].next_free;
    }
    else if (terrain_slots_used_ < terrain_capacity_)
    {
        slot = terrain_slots_used_++;
    }
    else
    {
        return TerrainId{};
    }
    TerrainSlot& s = terrains_[slot];
    s.alive = true;
    s.terrain = terrain;
    ++terrain_count_;
    return TerrainId{slot, s.generation};
}

void RenderScene::RemoveTerrain(TerrainId id)
{
    ENGINE_ASSERT(FindTerrain(id) != nullptr, "RemoveTerrain: the id is invalid or stale");
    TerrainSlot& s = terrains_[id.index];
    s.alive = false;
    s.terrain = TerrainObject();
    s.generation = NextGeneration(s.generation);
    s.next_free = free_terrain_;
    free_terrain_ = id.index;
    --terrain_count_;
}

void RenderScene::SetTerrain(TerrainId id, const TerrainObject& terrain)
{
    ENGINE_ASSERT(FindTerrain(id) != nullptr, "SetTerrain: the id is invalid or stale");
    CheckTerrain(terrain);
    terrains_[id.index].terrain = terrain;
}

const TerrainObject* RenderScene::FindTerrain(TerrainId id) const
{
    if (!id.IsValid() || id.index >= terrain_slots_used_)
    {
        return nullptr;
    }
    const TerrainSlot& s = terrains_[id.index];
    return s.alive && s.generation == id.generation ? &s.terrain : nullptr;
}

bool RenderScene::TerrainSlotAlive(u32 slot) const
{
    ENGINE_ASSERT(slot < terrain_slots_used_, "TerrainSlotAlive: slot out of range");
    return terrains_[slot].alive;
}

const TerrainObject& RenderScene::TerrainAt(u32 slot) const
{
    ENGINE_ASSERT(slot < terrain_slots_used_ && terrains_[slot].alive, "TerrainAt: slot out of range or dead");
    return terrains_[slot].terrain;
}

namespace
{
void CheckInstanced(const InstancedObject& o)
{
    ENGINE_ASSERT(o.set != nullptr && o.mesh != nullptr, "instanced: the set or the mesh is null");
    ENGINE_ASSERT(IsFinite(o.scale) && o.scale > 0.0f && IsFinite(o.position.x) && IsFinite(o.position.y) &&
                      IsFinite(o.position.z),
                  "instanced: the position or the scale is not valid");
    ENGINE_ASSERT(IsFinite(o.rotation.x) && IsFinite(o.rotation.y) && IsFinite(o.rotation.z) &&
                      IsFinite(o.rotation.w) && o.rotation.LengthSquared() > 0.5f && o.rotation.LengthSquared() < 1.5f,
                  "instanced: the rotation is not a unit quaternion");
}
}  // namespace

InstancedId RenderScene::AddInstanced(const InstancedObject& object)
{
    CheckInstanced(object);
    u32 slot;
    if (free_instanced_ != kNoSlot)
    {
        slot = free_instanced_;
        free_instanced_ = instanced_[slot].next_free;
    }
    else if (instanced_slots_used_ < instanced_capacity_)
    {
        slot = instanced_slots_used_++;
    }
    else
    {
        return InstancedId{};
    }
    InstancedSlot& s = instanced_[slot];
    s.alive = true;
    s.object = object;
    ++instanced_count_;
    return InstancedId{slot, s.generation};
}

void RenderScene::RemoveInstanced(InstancedId id)
{
    ENGINE_ASSERT(FindInstanced(id) != nullptr, "RemoveInstanced: the id is invalid or stale");
    InstancedSlot& s = instanced_[id.index];
    s.alive = false;
    s.object = InstancedObject();
    s.generation = NextGeneration(s.generation);
    s.next_free = free_instanced_;
    free_instanced_ = id.index;
    --instanced_count_;
}

void RenderScene::SetInstanced(InstancedId id, const InstancedObject& object)
{
    ENGINE_ASSERT(FindInstanced(id) != nullptr, "SetInstanced: the id is invalid or stale");
    CheckInstanced(object);
    instanced_[id.index].object = object;
}

const InstancedObject* RenderScene::FindInstanced(InstancedId id) const
{
    if (!id.IsValid() || id.index >= instanced_slots_used_)
    {
        return nullptr;
    }
    const InstancedSlot& s = instanced_[id.index];
    return s.alive && s.generation == id.generation ? &s.object : nullptr;
}

bool RenderScene::InstancedSlotAlive(u32 slot) const
{
    ENGINE_ASSERT(slot < instanced_slots_used_, "InstancedSlotAlive: slot out of range");
    return instanced_[slot].alive;
}

const InstancedObject& RenderScene::InstancedAt(u32 slot) const
{
    ENGINE_ASSERT(slot < instanced_slots_used_ && instanced_[slot].alive, "InstancedAt: slot out of range or dead");
    return instanced_[slot].object;
}

}  // namespace vse::renderer
