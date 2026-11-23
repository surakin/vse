#include "animation/components.h"
#include "engine_c/surface_scene.h"

#include "algorithm.h"
#include "cstr/mem_ops.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "engine_c/resources.h"
#include "engine_c/runtime.h"
#include "log/assert.h"

namespace vse::engine_c
{

namespace
{

constexpr u32 kMaxObjects = 4096;
constexpr u32 kMaxLights = 64;

bool SameOverrides(const SlotOverrides& a, const SlotOverrides& b);
bool SameHostHandles(const rhi::TextureHandle* a, const rhi::TextureHandle* b, u32 count);
bool IsHidden(const ecs::World& world, ecs::Entity entity);

}  // namespace

SurfaceScene::SurfaceScene(rhi::Device* device, rhi::Swapchain* swapchain, ResourceHost* resources,
                           renderer::BeforePresentFn before_present, void* before_present_user)
    : device_(device), resources_(resources)
{
    renderer::SceneFrameSetup setup;
    setup.device = device;
    setup.swapchain = swapchain;
    setup.max_objects = kMaxObjects;
    setup.max_lights = kMaxLights;
    setup.before_present = before_present;
    setup.before_present_user = before_present_user;
    if (!frame_.Create(setup))
    {
        return;
    }
    renderer::RenderSceneDesc desc;
    desc.max_objects = kMaxObjects;
    desc.max_lights = kMaxLights;
    scene_ = MakeUnique<renderer::RenderScene>(desc);
    // The lights are mirrored from the world's light entities and its environment at each frame (SyncLights).
    // A manager of this device's own over the memory backend the host adds content to: allow_fallbacks is off, so a
    // reference that does not resolve draws nothing.
    asset::ManagerConfig config;
    config.allow_fallbacks = false;
    config.buffer_pool_bytes = u64{64} << 20;
    manager_ = MakeUnique<asset::ResourceManager>(resources->Types(), config);
    context_.device = device;
    context_.resources = nullptr;  // a device reset rebuilds nothing yet (Day 368)
    context_.materials = frame_.Materials();
    context_.templates = frame_.Templates();
    asset::RegisterGpuLoaders(manager_.Get(), &context_);
    asset::RegisterSkeletonLoader(manager_.Get());  // a skinned mesh loads its skeleton first
    if (!manager_->Mount(StringView("/"), &resources->Memory(), 1000, &mount_).Ok())
    {
        return;
    }
    default_material_ = renderer::CreateGBufferMaterial(frame_.Materials(), frame_.Templates(),
                                                        renderer::GBufferOptions{}, "default material");
    renderer::GBufferOptions skinned_options;
    skinned_options.skinned = true;
    default_skinned_material_ = renderer::CreateGBufferMaterial(frame_.Materials(), frame_.Templates(), skinned_options,
                                                                "default skinned material");
    if (frame_.Materials()->SetVec4(default_material_, "base_color", Vec4(0.7f, 0.7f, 0.7f, 1.0f)) &&
        frame_.Materials()->SetVec4(default_skinned_material_, "base_color", Vec4(0.7f, 0.7f, 0.7f, 1.0f)))
    {
        valid_ = true;
    }
}

SurfaceScene::~SurfaceScene()
{
    if (device_ != nullptr)
    {
        device_->WaitIdle();  // frames may still be in flight
    }
    // Reverse order of creation: what the scene points at (meshes), then the frame, then the manager's backend mount.
    entries_.Clear();
    light_entries_.Clear();
    terrain_entries_.Clear();
    terrain_sources_.Clear();
    while (instances_.Size() != 0)
    {
        DestroyInstance(instances_.Size() - 1);
    }
    scene_ = nullptr;
    live_.Release();
    textures_.Clear();
    meshes_.Clear();
    materials_.Clear();
    frame_.Destroy();
    manager_ = nullptr;
}

const renderer::GpuMesh* SurfaceScene::ResolveMesh(u64 id)
{
    if (const Mesh* known = meshes_.Find(id))  // container audit Day 10: a scan per entity and slot
    {
        return known->handle.Get()->mesh.Get();
    }
    Mesh made;
    made.id = id;
    const asset::Status loaded = manager_->Load<asset::GpuMeshAsset>(asset::ResourceId::FromValue(id), &made.handle);
    if (!loaded.Ok())
    {
        return nullptr;  // not remembered: the host may add the mesh later, and the next frame asks again
    }
    {
        // The buffers are moved out of their upload state in one submission.
        renderer::GpuMesh* mesh = made.handle.Get()->mesh.Get();
        if (mesh != nullptr && !mesh->Ready())
        {
            device_->BeginFrame();
            rhi::CommandList* list = device_->CreateCommandList();
            list->Begin();
            mesh->RecordPrepare(list);
            list->End();
            rhi::CommandList* lists[] = {list};
            device_->Submit(lists, 1);
            device_->EndFrame();
            device_->WaitIdle();
            mesh->MarkReady();
        }
    }
    return meshes_.Emplace(id, static_cast<Mesh&&>(made)).value->handle.Get()->mesh.Get();
}

bool SurfaceScene::ResolveMaterial(u64 id, bool skinned, u32* index)
{
    if (id == 0)
    {
        *index = skinned ? default_skinned_material_.index : default_material_.index;
        return true;
    }
    if (const Material* known = materials_.Find(id))
    {
        *index = known->handle.Get()->material->id.index;
        return true;
    }
    Material made;
    made.id = id;
    if (!manager_->Load<asset::GpuMaterialAsset>(asset::ResourceId::FromValue(id), &made.handle).Ok())
    {
        return false;  // not remembered: the host may add the material later
    }
    *index = materials_.Emplace(id, static_cast<Material&&>(made)).value->handle.Get()->material->id.index;
    return true;
}

renderer::TerrainSourcePtr SurfaceScene::ResolveTerrainSource(u64 id)
{
    const asset::HeightMapView* view = nullptr;
    asset::ContentHash hash;
    if (resources_ == nullptr || resources_->PinHeightMap(id, &view, &hash) != ENGINE_OK)
    {
        return renderer::TerrainSourcePtr();  // not mounted (yet): the next frame asks again
    }
    for (usize i = 0; i < terrain_sources_.Size(); ++i)
    {
        if (terrain_sources_[i].id == id && terrain_sources_[i].hash == hash)
        {
            return terrain_sources_[i].source;
        }
    }
    renderer::TerrainSourcePtr made;
    if (!asset::MakeTerrainSource(device_, *view, nullptr, &made) || !made->Valid())
    {
        return renderer::TerrainSourcePtr();
    }
    for (usize i = 0; i < terrain_sources_.Size(); ++i)
    {
        if (terrain_sources_[i].id == id)
        {  // the resource was replaced: the entries that name the old data get the new one at their next update
            terrain_sources_[i].hash = hash;
            terrain_sources_[i].source = made;
            return made;
        }
    }
    TerrainSource entry;
    entry.id = id;
    entry.hash = hash;
    entry.source = made;
    terrain_sources_.Push(entry);
    return made;
}

renderer::GpuTexturePtr SurfaceScene::ResolveTexture(u64 id)
{
    if (const Texture* known = textures_.Find(id))
    {
        return known->handle.Get()->texture;
    }
    Texture made;
    made.id = id;
    if (!manager_->Load<asset::GpuTextureAsset>(asset::ResourceId::FromValue(id), &made.handle).Ok())
    {
        return renderer::GpuTexturePtr();
    }
    return textures_.Emplace(id, static_cast<Texture&&>(made)).value->handle.Get()->texture;
}

void SurfaceScene::SyncTerrains(WorldObject& world, SceneFrameReport* report)
{
    for (auto& [entity, e] : terrain_entries_)
    {
        e.seen = false;
    }
    auto query = world.world.Query<ecs::Read<TerrainComponent>, ecs::Read<ecs::WorldTransform>>();
    query.ForEach(
        [&](ecs::Entity entity, const TerrainComponent& terrain, const ecs::WorldTransform& placed)
        {
            if (IsHidden(world.world, entity))
            {
                return;
            }
            const u64 key = entity.ToU64();
            renderer::TerrainObject object;
            object.source = ResolveTerrainSource(terrain.height_map);
            object.world = placed.matrix;
            object.material.layer_count = terrain.layer_count;
            for (u32 i = 0; i < renderer::kMaxTerrainLayers; ++i)
            {
                object.material.layers[i].tiling = terrain.tiling[i];
                object.material.layers[i].tint = Vec3(terrain.tint[i][0], terrain.tint[i][1], terrain.tint[i][2]);
                object.material.layers[i].roughness = terrain.roughness[i];
                object.material.layers[i].metallic = terrain.metallic[i];
            }
            object.material.slope_layer = terrain.slope_layer;
            object.material.slope_start = terrain.slope_start;
            object.material.slope_end = terrain.slope_end;
            object.material.fade_start = terrain.fade_start;
            object.material.fade_end = terrain.fade_end;
            if (terrain.layer_texture != 0)
            {
                object.material.layer_array = ResolveTexture(terrain.layer_texture);
            }
            if (terrain.splat_texture != 0)
            {
                object.material.splat = ResolveTexture(terrain.splat_texture);
            }
            const bool usable = object.source && object.source->Valid() && renderer::TerrainWorldValid(object.world) &&
                                (terrain.layer_texture == 0 || object.material.layer_array) &&
                                (terrain.splat_texture == 0 || object.material.splat) &&
                                renderer::TerrainMaterialInvalid(object.material) == nullptr;
            if (!usable)
            {
                ++report->entities_unresolved;  // an entry it had stays unseen and is removed below
                return;
            }
            TerrainEntry* entry = terrain_entries_.Find(key);
            if (entry == nullptr)
            {
                const renderer::TerrainId id = scene_->AddTerrain(object);
                if (!id.IsValid())
                {
                    ++report->entities_unresolved;  // the scene has no room for another terrain
                    return;
                }
                TerrainEntry made;
                made.entity = key;
                made.id = id;
                made.seen = true;
                terrain_entries_.Emplace(key, made);
                return;
            }
            scene_->SetTerrain(entry->id, object);
            entry->seen = true;
        });
    for (u32 i = 0; i < terrain_entries_.Size();)
    {
        TerrainEntry& e = terrain_entries_.At(i);
        if (!e.seen)
        {
            scene_->RemoveTerrain(e.id);
            terrain_entries_.RemoveAt(i);
        }
        else
        {
            ++i;
        }
    }
}

void SurfaceScene::DestroyInstance(usize index)
{
    Instance& instance = instances_[index];
    if (instance.material.IsValid())
    {
        frame_.Materials()->DestroyMaterial(instance.material);
    }
    instance.textures.Clear();
    instances_[index] = static_cast<Instance&&>(instances_[instances_.Size() - 1]);
    instances_.Pop();
}

u32 SurfaceScene::MaterialFor(const SlotOverrides& overrides, u64 entity, u32 part, u32 base_index,
                              SceneFrameReport* report)
{
    if (overrides.count == 0)
    {
        return base_index;
    }
    // The device textures the host textures of the slots have now (this also stages their newest pixels).
    rhi::TextureHandle host_handles[SlotOverrides::kMax];
    for (u32 i = 0; i < overrides.count; ++i)
    {
        const SlotOverrides::Item& item = overrides.items[i];
        if (item.kind == ENGINE_SLOT_HOST_TEXTURE)
        {
            host_handles[i] = live_.Acquire(item.host_texture);
            if (!host_handles[i].IsValid())
            {
                live_.Revert();
            }
        }
    }
    Instance* instance = nullptr;
    for (usize i = 0; i < instances_.Size(); ++i)
    {
        if (instances_[i].entity == entity && instances_[i].part == part)
        {
            instance = &instances_[i];
            break;
        }
    }
    if (instance != nullptr && instance->base_index == base_index && SameOverrides(instance->content, overrides) &&
        instance->material.IsValid() && SameHostHandles(instance->host_handles, host_handles, overrides.count))
    {
        instance->seen = true;
        return instance->material.index;
    }
    if (instance != nullptr)
    {
        // changed: made again (the entry's object follows the new index)
        for (usize i = 0; i < instances_.Size(); ++i)
        {
            if (&instances_[i] == instance)
            {
                DestroyInstance(i);
                break;
            }
        }
    }
    renderer::MaterialSystem* materials = frame_.Materials();
    const renderer::MaterialId base = materials->MaterialAtIndex(base_index);
    Instance made;
    made.entity = entity;
    made.part = part;
    made.base_index = base_index;
    made.content = overrides;
    for (u32 i = 0; i < overrides.count; ++i)
    {
        made.host_handles[i] = host_handles[i];
    }
    if (!base.IsValid())
    {
        return base_index;
    }
    made.material = materials->CloneMaterial(base, "slot overrides");
    if (!made.material.IsValid())
    {
        return base_index;
    }
    for (u32 i = 0; i < overrides.count; ++i)
    {
        const SlotOverrides::Item& item = overrides.items[i];
        if (item.kind == ENGINE_SLOT_FLOAT)
        {
            materials->SetFloat(made.material, item.name, item.value[0]);
        }
        else if (item.kind == ENGINE_SLOT_COLOR || item.kind == ENGINE_SLOT_VEC4)
        {
            materials->SetVec4(made.material, item.name,
                               Vec4(item.value[0], item.value[1], item.value[2], item.value[3]));
        }
        else if (item.kind == ENGINE_SLOT_HOST_TEXTURE)
        {
            if (host_handles[i].IsValid())
            {
                materials->SetTexture(made.material, item.name, host_handles[i]);
            }  // else the slot keeps the material's own value
        }
        else
        {
            asset::Handle<asset::GpuTextureAsset> texture;
            if (manager_->Load<asset::GpuTextureAsset>(asset::ResourceId::FromValue(item.texture), &texture).Ok() &&
                texture.Get()->texture.Get() != nullptr)
            {
                materials->SetTexture(made.material, item.name, renderer::GpuTextureConstPtr(texture.Get()->texture));
                made.textures.Push(static_cast<asset::Handle<asset::GpuTextureAsset>&&>(texture));
            }
            else
            {
                ++report->entities_unresolved;  // the original texture stays
            }
        }
    }
    made.seen = true;
    const u32 index = made.material.index;
    instances_.Push(static_cast<Instance&&>(made));
    return index;
}

void SurfaceScene::SyncLabels(WorldObject& world, const renderer::Camera& camera, u32 width, u32 height,
                              SceneFrameReport* report)
{
    frame_.BeginLabels();
    if (runtime_ == nullptr)
    {
        return;
    }
    if (!frame_.LabelsAvailable())
    {  // the font is opened at the first frame that has a label and a font to draw it with
        Vector<u8> font;
        const u64 version = runtime_->CopyLabelFont(&font);
        if (version != 0 && version != label_font_version_)
        {
            label_font_version_ = version;
            frame_.OpenLabelFont(ByteSpan(reinterpret_cast<const byte*>(font.Data()), font.Size()));
        }
    }
    const Mat4 view_projection = camera.ViewProjection();
    const f32 tan_half = Tan(camera.fov_y * 0.5f);
    auto query = world.world.Query<ecs::Read<LabelComponent>, ecs::Read<ecs::WorldTransform>>();
    query.ForEach(
        [&](ecs::Entity entity, const LabelComponent& label, const ecs::WorldTransform& placed)
        {
            if (IsHidden(world.world, entity))
            {
                return;
            }
            if (!frame_.LabelsAvailable() || frame_.LabelsQueued() >= 1024)
            {
                ++report->labels_refused;
                return;
            }
            const Vec3 point(placed.matrix.col[3].x + label.offset[0], placed.matrix.col[3].y + label.offset[1],
                             placed.matrix.col[3].z + label.offset[2]);
            const Vec4 clip = view_projection * Vec4(point.x, point.y, point.z, 1.0f);
            if (!(clip.w > 0.0f))
            {
                return;  // behind the camera: nothing to draw, nothing refused
            }
            const f32 x = (clip.x / clip.w * 0.5f + 0.5f) * static_cast<f32>(width);
            const f32 y = (clip.y / clip.w * 0.5f + 0.5f) * static_cast<f32>(height);
            if (x < -512.0f || x > static_cast<f32>(width) + 512.0f || y < -512.0f ||
                y > static_cast<f32>(height) + 512.0f)
            {
                return;  // far off the screen
            }
            f32 pixels = label.size;
            if ((label.flags & ENGINE_LABEL_SIZE_METRES) != 0)
            {
                const f32 distance = (point - camera.position).Dot(camera.Forward());
                pixels = distance > 1e-4f && tan_half > 1e-6f
                             ? label.size * static_cast<f32>(height) * 0.5f / (distance * tan_half)
                             : 256.0f;
                // a set of sizes, so the glyph atlas holds a few rasterizations and not one per distance
                static const f32 kSizes[] = {8, 10, 12, 14, 16, 20, 24, 32, 48, 64, 96, 128, 192, 256};
                f32 best = kSizes[0];
                for (const f32 candidate : kSizes)
                {
                    if (Abs(candidate - pixels) < Abs(best - pixels))
                    {
                        best = candidate;
                    }
                }
                pixels = best;
            }
            if (pixels < 6.0f)
            {
                pixels = 6.0f;
            }
            if (pixels > 256.0f)
            {
                pixels = 256.0f;
            }
            const u32 color = static_cast<u32>(label.color[0] * 255.0f + 0.5f) |
                              (static_cast<u32>(label.color[1] * 255.0f + 0.5f) << 8) |
                              (static_cast<u32>(label.color[2] * 255.0f + 0.5f) << 16) |
                              (static_cast<u32>(label.color[3] * 255.0f + 0.5f) << 24);
            if (frame_.AddLabel(StringView(label.text, label.length), x, y, static_cast<u32>(pixels + 0.5f), color,
                                label.anchor))
            {
                ++report->labels_drawn;
            }
            else
            {
                ++report->labels_refused;
            }
        });
}

void SurfaceScene::SyncLights(WorldObject& world, SceneFrameReport* report)
{
    const Environment& environment = world.environment;
    // the ambient term from the environment
    renderer::Light ambient;
    ambient.type = renderer::LightType::Ambient;
    ambient.color = Vec3(environment.ambient_color[0], environment.ambient_color[1], environment.ambient_color[2]);
    ambient.intensity = environment.ambient_intensity;
    if (ambient_.IsValid())
    {
        scene_->SetLight(ambient_, ambient);
    }
    else
    {
        ambient_ = scene_->AddLight(ambient);
    }
    if (environment.sky_mode == ENGINE_SKY_PROCEDURAL)
    {
        renderer::SkySettings sky;
        sky.mode = renderer::SkyMode::Procedural;
        sky.zenith = Vec3(environment.sky_zenith[0], environment.sky_zenith[1], environment.sky_zenith[2]);
        sky.horizon = Vec3(environment.sky_horizon[0], environment.sky_horizon[1], environment.sky_horizon[2]);
        sky.ground = Vec3(environment.sky_ground[0], environment.sky_ground[1], environment.sky_ground[2]);
        sky.sun_direction = Vec3(environment.sun_direction[0], environment.sun_direction[1],
                                 environment.sun_direction[2]);
        sky.sun_color = Vec3(environment.sun_color[0], environment.sun_color[1], environment.sun_color[2]);
        sky.sun_size = environment.sun_size;
        sky.horizon_blend = environment.sky_horizon_blend;
        sky.exposure = environment.sky_exposure;
        sky.ambient_intensity = environment.sky_ambient_intensity;
        frame_.SetExposure(environment.exposure);
        frame_.SetSky(sky, environment.reflection_intensity);
    }
    else if (environment.sky_mode == ENGINE_SKY_NONE)
    {
        renderer::SkySettings sky;
        sky.mode = renderer::SkyMode::None;
        frame_.SetExposure(environment.exposure);
        frame_.SetSky(sky, environment.reflection_intensity);
    }
    else
    {
        frame_.SetEnvironment(environment.exposure,
                              Vec3(environment.sky_color[0], environment.sky_color[1], environment.sky_color[2]),
                              environment.reflection_intensity);
    }
    for (auto& [entity, e] : light_entries_)
    {
        e.seen = false;
    }
    u32 light_entities = 0;
    auto query = world.world.Query<ecs::Read<LightComponent>, ecs::Read<ecs::WorldTransform>>();
    query.ForEach(
        [&](ecs::Entity entity, const LightComponent& component, const ecs::WorldTransform& placed)
        {
            ++light_entities;
            renderer::Light light;
            light.color = Vec3(component.color[0], component.color[1], component.color[2]);
            light.intensity = component.intensity;
            if (component.kind == ENGINE_LIGHT_POINT || component.kind == ENGINE_LIGHT_SPOT)
            {
                light.type = component.kind == ENGINE_LIGHT_SPOT ? renderer::LightType::Spot
                                                                 : renderer::LightType::Point;
                light.position = Vec3(placed.matrix.col[3].x, placed.matrix.col[3].y, placed.matrix.col[3].z);
                light.radius = component.radius;
                light.inner_cone = component.inner_cone;
                light.outer_cone = component.outer_cone;
            }
            if (component.kind != ENGINE_LIGHT_POINT)
            {  // the axis toward the light (+Z for a host's light, which shines along -Z) under the entity's rotation
                const Vec3 col0(placed.matrix.col[0].x, placed.matrix.col[0].y, placed.matrix.col[0].z);
                const Vec3 col1(placed.matrix.col[1].x, placed.matrix.col[1].y, placed.matrix.col[1].z);
                const Vec3 col2(placed.matrix.col[2].x, placed.matrix.col[2].y, placed.matrix.col[2].z);
                const Vec3 back = col0 * component.axis[0] + col1 * component.axis[1] + col2 * component.axis[2];
                const f32 length = back.Length();
                if (!(length > 1e-12f))
                {
                    ++report->lights_refused;
                    return;
                }
                if (component.kind == ENGINE_LIGHT_DIRECTIONAL)
                {
                    light.type = renderer::LightType::Directional;
                }
                light.direction = back * (1.0f / length);
            }
            const u64 key = entity.ToU64();
            LightEntry* entry = light_entries_.Find(key);
            if (entry != nullptr)
            {
                scene_->SetLight(entry->id, light);
                entry->seen = true;
                return;
            }
            const renderer::LightId id = scene_->AddLight(light);
            if (!id.IsValid())
            {
                ++report->lights_refused;  // the scene is full
                return;
            }
            LightEntry fresh;
            fresh.entity = key;
            fresh.id = id;
            fresh.seen = true;
            light_entries_.Emplace(key, fresh);
        });
    for (u32 i = 0; i < light_entries_.Size();)  // the removal order the Vector gave, as for the objects
    {
        if (!light_entries_.At(i).seen)
        {
            scene_->RemoveLight(light_entries_.At(i).id);
            light_entries_.RemoveAt(i);
        }
        else
        {
            ++i;
        }
    }
    // the default sun of a world that has no light and no environment of its own
    const bool want_sun = light_entities == 0 && !world.environment_set;
    if (want_sun && !sun_.IsValid())
    {
        renderer::Light sun;
        sun.type = renderer::LightType::Directional;
        sun.direction = Vec3(0.3f, 0.8f, 0.5f).Normalized();
        sun.intensity = 2.0f;
        sun_ = scene_->AddLight(sun);
    }
    else if (!want_sun && sun_.IsValid())
    {
        scene_->RemoveLight(sun_);
        sun_ = renderer::LightId();
    }
}

namespace
{

// Field-wise, because the padding of the structs is not defined.
bool SameOverrides(const SlotOverrides& a, const SlotOverrides& b)
{
    if (a.count != b.count)
    {
        return false;
    }
    for (u32 i = 0; i < a.count; ++i)
    {
        const SlotOverrides::Item& x = a.items[i];
        const SlotOverrides::Item& y = b.items[i];
        if (MemCompare(x.name, y.name, sizeof(x.name)) != 0 || x.kind != y.kind || x.texture != y.texture ||
            x.host_texture != y.host_texture || MemCompare(x.value, y.value, sizeof(x.value)) != 0)
        {
            return false;
        }
    }
    return true;
}

bool SameHostHandles(const rhi::TextureHandle* a, const rhi::TextureHandle* b, u32 count)
{
    for (u32 i = 0; i < count; ++i)
    {
        if (a[i].IsValid() != b[i].IsValid() || (a[i].IsValid() && !(a[i] == b[i])))
        {
            return false;
        }
    }
    return true;
}

// Whether the entity or any of its ancestors is hidden.
bool IsHidden(const ecs::World& world, ecs::Entity entity)
{
    for (u32 depth = 0; depth < 64 && entity.IsValid() && world.Exists(entity); ++depth)
    {
        const VisibilityComponent* visibility = world.Peek<VisibilityComponent>(entity);
        if (visibility != nullptr && visibility->hidden != 0)
        {
            return true;
        }
        const ecs::Parent* parent = world.Peek<ecs::Parent>(entity);
        if (parent == nullptr)
        {
            return false;
        }
        entity = parent->entity;
    }
    return false;
}

// The camera an entity stands for, or false when the entity is not a usable camera (destroyed, no camera component, no
// world matrix, or a matrix whose axes collapse).
bool CameraOfEntity(const ecs::World& world, u64 key, f32 aspect_of_surface, renderer::Camera* out)
{
    const ecs::Entity entity = ecs::Entity::FromU64(key);
    if (!world.Exists(entity))
    {
        return false;
    }
    const CameraComponent* component = world.Peek<CameraComponent>(entity);
    const ecs::WorldTransform* placed = world.Peek<ecs::WorldTransform>(entity);
    if (component == nullptr || placed == nullptr)
    {
        return false;
    }
    const Mat4& m = placed->matrix;
    const Vec3 up_hint(m.col[1].x, m.col[1].y, m.col[1].z);
    Vec3 back(m.col[2].x, m.col[2].y, m.col[2].z);
    const f32 back_length = back.Length();
    if (!(back_length > 1e-12f))
    {
        return false;
    }
    back = back * (1.0f / back_length);
    Vec3 right = up_hint.Cross(back);
    const f32 right_length = right.Length();
    if (!(right_length > 1e-12f))
    {
        return false;
    }
    right = right * (1.0f / right_length);
    const Vec3 up = back.Cross(right);
    out->position = Vec3(m.col[3].x, m.col[3].y, m.col[3].z);
    out->rotation = Mat3::FromBasis(right, up, back);
    out->fov_y = component->fov_y;
    out->near_plane = component->near_plane;
    out->far_plane = component->far_plane;
    out->aspect = (component->flags & ENGINE_CAMERA_FIXED_ASPECT) != 0 ? component->aspect : aspect_of_surface;
    return true;
}

}  // namespace

SceneFrameReport SurfaceScene::Render(WorldObject& world, u64 camera_entity, u32 width, u32 height, f32 seconds,
                                      f32 delta_seconds, const renderer::FrameTarget* target)
{
    SceneFrameReport report;
    live_.BeginFrame();
    const f32 aspect = height != 0 ? static_cast<f32>(width) / static_cast<f32>(height) : 1.0f;
    for (auto& [entity, e] : entries_)
    {
        e.seen = false;
    }
    for (usize i = 0; i < instances_.Size(); ++i)
    {
        instances_[i].seen = false;
    }
    auto query = world.world.Query<ecs::Read<MeshReference>, ecs::Read<ecs::WorldTransform>>();
    query.ForEach(
        [&](ecs::Entity entity, const MeshReference& reference, const ecs::WorldTransform& placed)
        {
            const u64 key = entity.ToU64();
            Entry* entry = entries_.Find(key);  // container audit Day 10: a scan of every entry, per drawn entity
            if (IsHidden(world.world, entity))
            {
                return;  // not drawn: an entry it had stays unseen and is removed below
            }
            const renderer::GpuMesh* mesh = reference.mesh != 0 ? ResolveMesh(reference.mesh) : nullptr;
            u32 material = 0;
            const bool skinned = mesh != nullptr && mesh->Layout().skin_stride != 0;
            const animation::SkeletonInstance* skeleton = world.world.Peek<animation::SkeletonInstance>(entity);
            const bool posed = skeleton != nullptr && skeleton->palette.Size() > 0;
            // The entity's morph weights (maintenance, 2026-11-20), as many as the mesh has targets.
            const animation::MorphWeights* morph = world.world.Peek<animation::MorphWeights>(entity);
            u32 morph_count = 0;
            if (morph != nullptr && mesh != nullptr)
            {
                morph_count = Min(Min(morph->count, animation::kMaxMorphWeights), mesh->MorphTargetCount());
            }
            const bool resolved = mesh != nullptr && (!skinned || posed) &&
                                  ResolveMaterial(reference.material_resource, skinned, &material);
            if (!resolved)
            {
                if (reference.mesh != 0)
                {
                    ++report.entities_unresolved;
                }
                return;  // an entry it had stays unseen and is removed below
            }
            // What the entity draws: the mesh whole, or one part per mesh slot when the host named slot materials.
            struct Part
            {
                u32 material = 0;
                u32 first = 0;
                u32 count = 0;
            };
            Part parts[kMaxParts];
            u32 part_total = 1;
            parts[0].material = material;
            const SlotMaterialsComponent* slots = world.world.Peek<SlotMaterialsComponent>(entity);
            const SlotOverrides* wanted = world.world.Peek<SlotOverrides>(entity);
            bool per_slot = slots != nullptr && slots->count != 0;
            for (u32 i = 0; wanted != nullptr && i < wanted->count; ++i)
            {
                per_slot = per_slot || wanted->items[i].mesh_slot != SlotOverrides::kAllMeshSlots;
            }
            if (per_slot && mesh->RangeCount() != 0 && mesh->RangeCount() <= kMaxParts)
            {
                part_total = mesh->RangeCount();
                for (u32 i = 0; i < part_total; ++i)
                {
                    const renderer::MeshRange range = mesh->Range(i);
                    parts[i].material = material;
                    parts[i].first = range.first_index;
                    parts[i].count = range.index_count;
                    if (slots != nullptr && i < slots->count && slots->material[i] != 0 &&
                        !ResolveMaterial(slots->material[i], skinned, &parts[i].material))
                    {
                        ++report.entities_unresolved;  // that slot's material is not mounted: it is not drawn
                        parts[i].count = 0xFFFFFFFFu;
                    }
                }
            }
            if (const SlotOverrides* overrides = world.world.Peek<SlotOverrides>(entity))
            {
                for (u32 i = 0; i < part_total; ++i)
                {
                    SlotOverrides mine;  // the overrides of every mesh slot and those of this slot
                    for (u32 k = 0; k < overrides->count; ++k)
                    {
                        const u32 target = overrides->items[k].mesh_slot;
                        if (target == SlotOverrides::kAllMeshSlots || (part_total > 1 && target == i))
                        {
                            mine.items[mine.count++] = overrides->items[k];
                        }
                    }
                    parts[i].material = MaterialFor(mine, key, i, parts[i].material, &report);
                }
            }
            bool same = entry != nullptr && entry->mesh == reference.mesh &&
                        entry->material == reference.material_resource && entry->count == part_total;
            for (u32 i = 0; same && i < part_total; ++i)
            {
                same = entry->part_material[i] == parts[i].material && entry->part_first[i] == parts[i].first &&
                       entry->part_count[i] == parts[i].count;
            }
            if (entry != nullptr && !same)
            {
                for (u32 i = 0; i < entry->count; ++i)
                {
                    if (entry->objects[i].IsValid())
                    {
                        scene_->RemoveObject(entry->objects[i]);  // another mesh or material: made again
                        entry->objects[i] = renderer::ObjectId{};
                    }
                }
                entry->count = 0;
            }
            if (entry == nullptr)
            {
                Entry fresh;
                fresh.entity = key;
                entry = entries_.Emplace(key, fresh).value;
            }
            if (entry->count == 0)
            {
                bool all = true;
                for (u32 i = 0; i < part_total; ++i)
                {
                    entry->part_material[i] = parts[i].material;
                    entry->part_first[i] = parts[i].first;
                    entry->part_count[i] = parts[i].count;
                    if (parts[i].count == 0xFFFFFFFFu)
                    {
                        entry->objects[i] = renderer::ObjectId{};  // not drawn: its material is missing
                        continue;
                    }
                    renderer::RenderObjectDesc desc;
                    desc.mesh = mesh;
                    desc.material = parts[i].material;
                    desc.first_index = parts[i].first;
                    desc.index_count = parts[i].count;
                    desc.transform = placed.matrix;
                    if (morph_count != 0)
                    {
                        desc.morph_weights = morph->weights.Data();
                        desc.morph_weight_count = morph_count;
                    }
                    if (skinned)
                    {
                        desc.joint_matrices = skeleton->palette[0].Data();
                        desc.joint_count = static_cast<u32>(skeleton->palette.Size() < 256 ? skeleton->palette.Size()
                                                                                           : 256);
                    }
                    entry->objects[i] = scene_->AddObject(desc);
                    all = all && entry->objects[i].IsValid();
                }
                if (!all)
                {
                    for (u32 i = 0; i < part_total; ++i)  // all of an entity's slots or none
                    {
                        if (entry->objects[i].IsValid())
                        {
                            scene_->RemoveObject(entry->objects[i]);
                            entry->objects[i] = renderer::ObjectId{};
                        }
                    }
                }
                else
                {
                    entry->count = part_total;
                }
                entry->mesh = reference.mesh;
                entry->material = reference.material_resource;
                entry->transform = placed.matrix;
            }
            else if (MemCompare(&entry->transform, &placed.matrix, sizeof(Mat4)) != 0)
            {
                for (u32 i = 0; i < entry->count; ++i)
                {
                    if (entry->objects[i].IsValid())
                    {
                        scene_->SetTransform(entry->objects[i], placed.matrix);
                    }
                }
                entry->transform = placed.matrix;
            }
            if (mesh->MorphTargetCount() != 0)
            {  // the weights move in storage and change every frame a clip plays: pointed at again each frame
                for (u32 i = 0; i < entry->count; ++i)
                {
                    if (entry->objects[i].IsValid())
                    {
                        scene_->SetMorphWeights(entry->objects[i], morph_count != 0 ? morph->weights.Data() : nullptr,
                                                morph_count);
                    }
                }
            }
            if (skinned)
            {  // the palette is rewritten by every animation update
                for (u32 i = 0; i < entry->count; ++i)
                {
                    if (entry->objects[i].IsValid())
                    {
                        scene_->SetJointMatrices(
                            entry->objects[i], skeleton->palette[0].Data(),
                            static_cast<u32>(skeleton->palette.Size() < 256 ? skeleton->palette.Size() : 256));
                    }
                }
            }
            entry->seen = entry->count != 0;
        });
    // Front to back, the last entry moved into a removed one's slot: the order the objects are removed in (and so the
    // ids the scene hands out next) is the one the Vector gave.
    for (u32 i = 0; i < entries_.Size();)
    {
        Entry& e = entries_.At(i);
        if (!e.seen)
        {
            for (u32 k = 0; k < e.count; ++k)
            {
                if (e.objects[k].IsValid())
                {
                    scene_->RemoveObject(e.objects[k]);
                }
            }
            entries_.RemoveAt(i);
        }
        else
        {
            ++i;
        }
    }
    for (usize i = 0; i < instances_.Size();)
    {
        if (!instances_[i].seen)
        {
            DestroyInstance(i);  // the last one takes its place
        }
        else
        {
            ++i;
        }
    }
    live_.Flush();     // the copies the slots asked for, before the frame samples the textures
    live_.EndFrame();  // textures no slot names any more are dropped
    SyncLights(world, &report);
    SyncTerrains(world, &report);
    renderer::Camera camera;  // the default camera unless the host named one that works
    camera.SetLookAt(Vec3(0.0f, 1.5f, 5.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    camera.aspect = aspect;
    camera.near_plane = 0.1f;
    camera.far_plane = 200.0f;
    report.camera_status = ENGINE_OK;
    if (camera_entity != 0 && !CameraOfEntity(world.world, camera_entity, aspect, &camera))
    {
        report.camera_status = ENGINE_ERROR_NOT_FOUND;  // the default camera stays
    }
    SyncLabels(world, camera, width, height, &report);
    last_camera_ = camera;
    last_width_ = width;
    last_height_ = height;
    frame_.SetTime(seconds, delta_seconds);
    report.status = frame_.Render(scene_.Get(), &camera, target);
    report.objects_drawn = frame_.GBufferStatistics().drawn_objects;
    return report;
}

bool SurfaceScene::PixelRay(f32 x, f32 y, f32* origin, f32* direction) const
{
    if (last_width_ == 0 || last_height_ == 0)
    {
        return false;
    }
    const f32 ndc_x = (x / static_cast<f32>(last_width_)) * 2.0f - 1.0f;
    const f32 ndc_y = (y / static_cast<f32>(last_height_)) * 2.0f - 1.0f;  // clip y points down (Vulkan)
    const Mat4 inverse = last_camera_.InverseViewProjection();
    const Vec4 near_h = inverse * Vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    const Vec4 far_h = inverse * Vec4(ndc_x, ndc_y, 1.0f, 1.0f);
    const Vec3 near_point(near_h.x / near_h.w, near_h.y / near_h.w, near_h.z / near_h.w);
    const Vec3 far_point(far_h.x / far_h.w, far_h.y / far_h.w, far_h.z / far_h.w);
    const Vec3 toward = (far_point - near_point).Normalized();
    origin[0] = last_camera_.position.x;
    origin[1] = last_camera_.position.y;
    origin[2] = last_camera_.position.z;
    direction[0] = toward.x;
    direction[1] = toward.y;
    direction[2] = toward.z;
    return true;
}

bool SurfaceScene::Project(const f32* point, f32* out) const
{
    if (last_width_ == 0 || last_height_ == 0)
    {
        return false;
    }
    const Vec3 p(point[0], point[1], point[2]);
    out[2] = (p - last_camera_.position).Dot(last_camera_.Forward());
    const Vec4 clip = last_camera_.ViewProjection() * Vec4(p.x, p.y, p.z, 1.0f);
    if (clip.w > 0.0f)
    {
        out[0] = (clip.x / clip.w * 0.5f + 0.5f) * static_cast<f32>(last_width_);
        out[1] = (clip.y / clip.w * 0.5f + 0.5f) * static_cast<f32>(last_height_);
    }
    else
    {
        out[0] = 0.0f;
        out[1] = 0.0f;
    }
    return true;
}

}  // namespace vse::engine_c
