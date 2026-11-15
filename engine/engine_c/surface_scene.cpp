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
    // The default environment until lights are components (Day 361): a sun and a little ambient light.
    renderer::Light sun;
    sun.type = renderer::LightType::Directional;
    sun.direction = Vec3(0.3f, 0.8f, 0.5f).Normalized();
    sun.intensity = 2.0f;
    scene_->AddLight(sun);
    renderer::Light ambient;
    ambient.type = renderer::LightType::Ambient;
    ambient.intensity = 0.2f;
    scene_->AddLight(ambient);
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

namespace
{

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
            if (entry != nullptr && (entry->mesh != reference.mesh || entry->material != reference.material_resource))
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
