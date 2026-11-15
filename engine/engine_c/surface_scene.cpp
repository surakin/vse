#include "engine_c/surface_scene.h"

#include "cstr/mem_ops.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "engine_c/resources.h"
#include "engine_c/runtime.h"
#include "log/assert.h"

namespace engine_placeholder::engine_c
{

namespace
{

constexpr u32 kMaxObjects = 4096;
constexpr u32 kMaxLights = 64;

bool SameOverrides(const SlotOverrides& a, const SlotOverrides& b);

}  // namespace

SurfaceScene::SurfaceScene(rhi::Device* device, rhi::Swapchain* swapchain, ResourceHost* resources,
                           renderer::BeforePresentFn before_present, void* before_present_user)
    : device_(device)
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
    if (!manager_->Mount(StringView("/"), &resources->Memory(), 1000, &mount_).Ok())
    {
        return;
    }
    default_material_ = renderer::CreateGBufferMaterial(frame_.Materials(), frame_.Templates(),
                                                        renderer::GBufferOptions{}, "default material");
    if (frame_.Materials()->SetVec4(default_material_, "base_color", Vec4(0.7f, 0.7f, 0.7f, 1.0f)))
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
    while (instances_.Size() != 0)
    {
        DestroyInstance(instances_.Size() - 1);
    }
    scene_ = nullptr;
    meshes_.Clear();
    materials_.Clear();
    frame_.Destroy();
    manager_ = nullptr;
}

void SurfaceScene::Rebind(rhi::Device* device, rhi::Swapchain* swapchain)
{
    device_ = device;
    frame_.Rebind(device, swapchain);
}

const renderer::GpuMesh* SurfaceScene::ResolveMesh(u64 id)
{
    for (usize i = 0; i < meshes_.Size(); ++i)
    {
        if (meshes_[i].id == id)
        {
            return meshes_[i].handle.Get()->mesh.Get();
        }
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
    meshes_.Push(static_cast<Mesh&&>(made));
    return meshes_[meshes_.Size() - 1].handle.Get()->mesh.Get();
}

bool SurfaceScene::ResolveMaterial(u64 id, u32* index)
{
    if (id == 0)
    {
        *index = default_material_.index;
        return true;
    }
    for (usize i = 0; i < materials_.Size(); ++i)
    {
        if (materials_[i].id == id)
        {
            *index = materials_[i].handle.Get()->material->id.index;
            return true;
        }
    }
    Material made;
    made.id = id;
    if (!manager_->Load<asset::GpuMaterialAsset>(asset::ResourceId::FromValue(id), &made.handle).Ok())
    {
        return false;  // not remembered: the host may add the material later
    }
    materials_.Push(static_cast<Material&&>(made));
    *index = materials_[materials_.Size() - 1].handle.Get()->material->id.index;
    return true;
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

u32 SurfaceScene::MaterialFor(const SlotOverrides& overrides, u64 entity, u32 base_index, SceneFrameReport* report)
{
    if (overrides.count == 0)
    {
        return base_index;
    }
    Instance* instance = nullptr;
    for (usize i = 0; i < instances_.Size(); ++i)
    {
        if (instances_[i].entity == entity)
        {
            instance = &instances_[i];
            break;
        }
    }
    if (instance != nullptr && instance->base_index == base_index && SameOverrides(instance->content, overrides) &&
        instance->material.IsValid())
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
    made.base_index = base_index;
    made.content = overrides;
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
        else if (item.kind == ENGINE_SLOT_COLOR)
        {
            materials->SetVec4(made.material, item.name,
                               Vec4(item.value[0], item.value[1], item.value[2], item.value[3]));
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
    frame_.SetEnvironment(environment.exposure,
                          Vec3(environment.sky_color[0], environment.sky_color[1], environment.sky_color[2]));
    for (usize i = 0; i < light_entries_.Size(); ++i)
    {
        light_entries_[i].seen = false;
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
            if (component.kind == ENGINE_LIGHT_POINT)
            {
                light.type = renderer::LightType::Point;
                light.position = Vec3(placed.matrix.col[3].x, placed.matrix.col[3].y, placed.matrix.col[3].z);
                light.radius = component.radius;
            }
            else
            {  // the light shines along the entity's -Z: the direction toward it is +Z
                const Vec3 back(placed.matrix.col[2].x, placed.matrix.col[2].y, placed.matrix.col[2].z);
                const f32 length = back.Length();
                if (!(length > 1e-12f))
                {
                    ++report->lights_refused;
                    return;
                }
                light.type = renderer::LightType::Directional;
                light.direction = back * (1.0f / length);
            }
            const u64 key = entity.ToU64();
            LightEntry* entry = nullptr;
            for (usize i = 0; i < light_entries_.Size(); ++i)
            {
                if (light_entries_[i].entity == key)
                {
                    entry = &light_entries_[i];
                    break;
                }
            }
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
            light_entries_.Push(fresh);
        });
    for (usize i = 0; i < light_entries_.Size();)
    {
        if (!light_entries_[i].seen)
        {
            scene_->RemoveLight(light_entries_[i].id);
            light_entries_[i] = light_entries_[light_entries_.Size() - 1];
            light_entries_.Pop();
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
            MemCompare(x.value, y.value, sizeof(x.value)) != 0)
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
                                      f32 delta_seconds)
{
    SceneFrameReport report;
    const f32 aspect = height != 0 ? static_cast<f32>(width) / static_cast<f32>(height) : 1.0f;
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        entries_[i].seen = false;
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
            Entry* entry = nullptr;
            for (usize i = 0; i < entries_.Size(); ++i)
            {
                if (entries_[i].entity == key)
                {
                    entry = &entries_[i];
                    break;
                }
            }
            if (IsHidden(world.world, entity))
            {
                return;  // not drawn: an entry it had stays unseen and is removed below
            }
            const renderer::GpuMesh* mesh = reference.mesh != 0 ? ResolveMesh(reference.mesh) : nullptr;
            u32 material = 0;
            const bool resolved = mesh != nullptr && ResolveMaterial(reference.material_resource, &material);
            if (!resolved)
            {
                if (reference.mesh != 0)
                {
                    ++report.entities_unresolved;
                }
                return;  // an entry it had stays unseen and is removed below
            }
            if (const SlotOverrides* overrides = world.world.Peek<SlotOverrides>(entity))
            {
                material = MaterialFor(*overrides, key, material, &report);
            }
            if (entry != nullptr && (entry->mesh != reference.mesh || entry->material != reference.material_resource ||
                                     entry->material_index != material))
            {
                scene_->RemoveObject(entry->object);  // another mesh or material: made again
                entry->object = renderer::ObjectId{};
            }
            if (entry == nullptr)
            {
                Entry fresh;
                fresh.entity = key;
                entries_.Push(fresh);
                entry = &entries_[entries_.Size() - 1];
            }
            if (!entry->object.IsValid())
            {
                renderer::RenderObjectDesc desc;
                desc.mesh = mesh;
                desc.material = material;
                desc.transform = placed.matrix;
                entry->object = scene_->AddObject(desc);
                entry->mesh = reference.mesh;
                entry->material = reference.material_resource;
                entry->material_index = material;
                entry->transform = placed.matrix;
            }
            else if (MemCompare(&entry->transform, &placed.matrix, sizeof(Mat4)) != 0)
            {
                scene_->SetTransform(entry->object, placed.matrix);
                entry->transform = placed.matrix;
            }
            entry->seen = entry->object.IsValid();
        });
    for (usize i = 0; i < entries_.Size();)
    {
        if (!entries_[i].seen)
        {
            if (entries_[i].object.IsValid())
            {
                scene_->RemoveObject(entries_[i].object);
            }
            entries_[i] = entries_[entries_.Size() - 1];
            entries_.Pop();
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
    SyncLights(world, &report);
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
    last_camera_ = camera;
    last_width_ = width;
    last_height_ = height;
    frame_.SetTime(seconds, delta_seconds);
    report.status = frame_.Render(scene_.Get(), &camera, nullptr);
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

}  // namespace engine_placeholder::engine_c
