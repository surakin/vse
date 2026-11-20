#include "scene/level_spawner.h"

#include "ecs/transform.h"
#include "lang/status_try.h"

namespace vse::scene
{
namespace
{

constexpr usize kMaxIncludeDepth = 16;

asset::Status Spawn(asset::ResourceManager& manager, asset::ResourceId id, const storage::ComponentRegistry& registry,
                    ecs::World& world, storage::KeyIndex& index, SpawnedLevel* out, usize depth)
{
    if (depth > kMaxIncludeDepth)
    {
        return asset::Status::Make(asset::ErrorCode::LimitExceeded, "levels include each other more than 16 deep");
    }
    // A level that two included levels both include is spawned once.
    for (usize i = 0; i < out->levels.Size(); ++i)
    {
        if (out->levels[i].Id() == id)
        {
            return asset::Status::Success();
        }
    }
    asset::Handle<asset::LevelSummary> level;
    ENGINE_TRY(manager.Load<asset::LevelSummary>(id, &level));
    const asset::LevelSummary* summary = level.Get();
    // The included levels first.
    for (usize i = 0; i < summary->resources.Size(); ++i)
    {
        if ((summary->resources[i].roles & asset::kLevelRoleInclude) != 0)
        {
            ENGINE_TRY(Spawn(manager, summary->resources[i].id, registry, world, index, out, depth + 1));
        }
    }
    asset::LevelData data;
    ENGINE_TRY(asset::LoadLevelBlob(summary->bytes, manager.Types(), registry, world, index, &data));
    for (usize i = 0; i < data.entities.Size(); ++i)
    {
        out->entities.Push(data.entities[i].key);
    }
    // The derived state a level does not store: a world transform for every entity that has a local one.
    for (usize i = 0; i < data.entities.Size(); ++i)
    {
        const ecs::Entity entity = index.Find(data.entities[i].key);
        if (entity.IsValid() && world.Peek<ecs::LocalTransform>(entity) != nullptr &&
            world.Peek<ecs::WorldTransform>(entity) == nullptr)
        {
            if (world.Add(entity, ecs::WorldTransform{}) != ecs::Status::Ok)
            {
                return asset::Status::Make(asset::ErrorCode::Other, "the world refused a world transform");
            }
        }
    }
    // A renderer for every entity that names a mesh.
    for (usize i = 0; i < data.entities.Size(); ++i)
    {
        const ecs::Entity entity = index.Find(data.entities[i].key);
        if (!entity.IsValid())
        {
            continue;
        }
        const asset::MeshReference* reference = world.Peek<asset::MeshReference>(entity);
        if (reference == nullptr)
        {
            continue;
        }
        asset::Handle<asset::GpuMeshAsset> mesh;
        ENGINE_TRY(manager.Load<asset::GpuMeshAsset>(asset::ResourceId::FromValue(reference->mesh.id), &mesh));
        MeshRenderer renderer_component;
        renderer_component.mesh = mesh.Get()->mesh;
        renderer_component.flags = reference->flags;
        if (reference->material.id != 0)
        {
            asset::Handle<asset::GpuMaterialAsset> material;
            ENGINE_TRY(
                manager.Load<asset::GpuMaterialAsset>(asset::ResourceId::FromValue(reference->material.id), &material));
            renderer_component.material = material.Get()->material->id.index;
            out->materials.Push(material.Get()->material);
        }
        if (const asset::MeshSlotMaterials* slots = world.Peek<asset::MeshSlotMaterials>(entity))
        {
            u32 used = 0;
            for (u32 s = 0; s < asset::kMaxLevelSlotMaterials && s < kMaxSlotMaterials; ++s)
            {
                const u64 slot_id = slots->At(s).id;
                if (slot_id == 0)
                {
                    renderer_component.slot_materials[s] = kInheritMaterial;
                    continue;
                }
                asset::Handle<asset::GpuMaterialAsset> slot_material;
                ENGINE_TRY(
                    manager.Load<asset::GpuMaterialAsset>(asset::ResourceId::FromValue(slot_id), &slot_material));
                renderer_component.slot_materials[s] = slot_material.Get()->material->id.index;
                out->materials.Push(slot_material.Get()->material);
                used = s + 1;
            }
            renderer_component.slot_count = used;
        }
        if (world.Add(entity, static_cast<MeshRenderer&&>(renderer_component)) != ecs::Status::Ok)
        {
            return asset::Status::Make(asset::ErrorCode::Other, "the world refused a mesh renderer");
        }
    }
    out->levels.Push(static_cast<asset::Handle<asset::LevelSummary>&&>(level));
    return asset::Status::Success();
}

}  // namespace

asset::Status SpawnLevel(asset::ResourceManager& manager, asset::ResourceId id,
                         const storage::ComponentRegistry& registry, ecs::World& world, storage::KeyIndex& index,
                         SpawnedLevel* out)
{
    DestroyLevel(out, world, index);
    const asset::Status status = Spawn(manager, id, registry, world, index, out, 0);
    if (status.Failed())
    {
        DestroyLevel(out, world, index);
    }
    return status;
}

void DestroyLevel(SpawnedLevel* level, ecs::World& world, storage::KeyIndex& index)
{
    for (usize i = level->entities.Size(); i > 0; --i)
    {
        const ecs::Entity entity = index.Find(level->entities[i - 1]);
        if (entity.IsValid())
        {
            static_cast<void>(world.Destroy(entity));
        }
    }
    static_cast<void>(index.Sync(world));
    level->entities.Clear();
    level->materials.Clear();
    level->levels.Clear();
}

}  // namespace vse::scene
