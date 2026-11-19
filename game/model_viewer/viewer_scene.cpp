// The model viewer's scene. See viewer_scene.h.
#include "viewer_scene.h"

#include "animation/pose.h"
#include "asset/cpu_loaders.h"
#include "asset/level.h"
#include "asset/memory_backend.h"
#include "containers/array.h"
#include "cstr/format.h"
#include "log/log.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "scene/components.h"
#include "scene/render_sync.h"
#include "scene/scene_components.h"
#include "storage/builtin_components.h"

namespace model_viewer
{

ViewerScene::ViewerScene() = default;

ViewerScene::~ViewerScene()
{
    Destroy();
}

bool ViewerScene::Fail(const char* what)
{
    error_ = String<HeapAllocator>(HeapAllocator::Default(), StringView(what));
    return false;
}

void ViewerScene::Destroy()
{
    if (device_ != nullptr)
    {
        device_->WaitIdle();  // frames may still use the meshes
    }
    if (world_.Get() != nullptr)
    {
        scene::DestroyLevel(&level_, *world_, index_);
    }
    schedule_.Reset();
    world_.Reset();  // the entities hold shares of the meshes
    animation_.Reset();
    resolver_.Reset();
    clip_handles_.Clear();
    clips_.Clear();
    posed_.Clear();
    morphed_.Clear();
    scene_.Reset();
    manager_.Reset();
    backend_.Reset();
    registry_.Reset();
    device_ = nullptr;
}

bool ViewerScene::Create(rhi::Device* device, renderer::MaterialSystem* materials,
                         const renderer::GBufferTemplates& templates, const ViewerOptions& options)
{
    Destroy();
    device_ = device;
    animate_ = options.animate;

    // 1. The import, in memory, with a level of the default scene's nodes.
    asset::ImportOptions import;
    import.emit_level = true;
    asset::ImportResult result;
    asset::GltfError gltf_error;
    const asset::Status imported = asset::ImportGltf(options.file, *options.source, import, &result, &gltf_error);
    if (imported.Failed())
    {
        return Fail(asset::GltfMessage(gltf_error, imported));
    }
    report_ = Move(result.report);
    asset::ResourceId level_id;
    for (const asset::ImportedResource& r : result.resources)
    {
        if (r.type == asset::types::kLevel)
        {
            level_id = r.id;
        }
        else if (r.type == asset::types::kAnimation)
        {
            ClipInfo clip;
            clip.id = r.id;
            const StringView path = r.path.View();
            usize slash = path.Length();
            for (usize i = 0; i < path.Length(); ++i)
            {
                slash = path[i] == '/' ? i : slash;
            }
            clip.name = String<HeapAllocator>(
                HeapAllocator::Default(),
                slash < path.Length() ? path.Substring(slash + 1, path.Length() - slash - 1) : path);
            clips_.Push(Move(clip));
        }
    }
    if (level_id.Value() == 0)
    {
        return Fail("the file has no scene to show");
    }

    // 2. The blobs in a memory backend, mounted on a manager that loads them onto the device.
    backend_ = MakeUnique<asset::HostMemoryBackend>(types_, u64{2} << 30);
    if (asset::ImportIntoBackend(*backend_, result).Failed())
    {
        return Fail("the imported resources could not be kept in memory");
    }
    asset::ManagerConfig config;
    config.buffer_pool_bytes = u64{256} << 20;
    manager_ = MakeUnique<asset::ResourceManager>(types_, config);
    context_.device = device;
    context_.resources = options.resources;
    context_.materials = materials;
    context_.templates = templates;
    asset::RegisterCpuLoaders(manager_.Get());
    asset::RegisterGpuLoaders(manager_.Get(), &context_);
    if (manager_->Mount(StringView("/"), backend_.Get(), 0).Failed())
    {
        return Fail("the imported resources could not be mounted");
    }

    // 3. The world: the level's entities (with the file's lights, or else a sun), an ambient term, and the systems that
    // move and mirror them.
    registry_ = MakeUnique<storage::ComponentRegistry>();
    if (storage::RegisterBuiltinComponents(registry_.Get()).Failed() ||
        asset::RegisterLevelComponents(registry_.Get()).Failed() ||
        animation::RegisterAnimationComponents(registry_.Get()).Failed() ||
        scene::RegisterSceneComponents(registry_.Get()).Failed())
    {
        return Fail("the components could not be registered");
    }
    renderer::RenderSceneDesc desc;
    desc.max_objects = 65536;
    desc.max_lights = 8;
    scene_ = MakeUnique<renderer::RenderScene>(desc);
    world_ = MakeUnique<ecs::World>();
    world_->SetResource(ecs::TransformReport{});
    world_->SetResource(scene::SceneLink{scene_.Get()});
    world_->SetResource(scene::SceneSyncReport{});
    schedule_ = MakeUnique<ecs::Schedule>();
    schedule_->Add(ecs::MakeTransformSystem("transforms").InStage(ecs::kPostUpdate));
    schedule_->Add(scene::MakeRenderSyncSystem("render sync").InStage(ecs::kRender));
    if (!schedule_->Build().ok)
    {
        return Fail("the schedule does not build");
    }
    const asset::Status spawned = scene::SpawnLevel(*manager_, level_id, *registry_, *world_, index_, &level_);
    if (spawned.Failed())
    {
        return Fail(
            "the file's scene could not be loaded onto the device (a mesh, a texture or a material was refused)");
    }
    // The file's own lights (KHR_lights_punctual) when it has any; the default sun only when it has none.
    u32 file_lights = 0;
    world_->Query<ecs::Read<scene::LightSource>>().ForEach([&](const scene::LightSource&) { ++file_lights; });
    if (file_lights == 0)
    {
        scene::LightSource sun;
        sun.type = renderer::LightType::Directional;
        sun.direction = Vec3(0.4f, 0.85f, 0.35f);
        sun.color = Vec3(1.0f, 0.97f, 0.92f);
        sun.intensity = 1.2f;
        world_->Create(ecs::LocalTransform{}, ecs::WorldTransform{}, sun);
    }
    scene::LightSource ambient;
    ambient.type = renderer::LightType::Ambient;
    ambient.color = Vec3(0.55f, 0.6f, 0.7f);
    ambient.intensity = 0.35f;
    world_->Create(ecs::LocalTransform{}, ecs::WorldTransform{}, ambient);
    world_->Query<ecs::Read<scene::MeshRenderer>>().ForEach(
        [&](const scene::MeshRenderer& mr)
        {
            if (mr.mesh.Get() != nullptr)
            {
                triangles_ += mr.mesh->IndexCount() / 3;
            }
        });

    // 4. The clips: loaded (the resolver finds them only while something holds them), and the first one played.
    for (const ClipInfo& clip : clips_)
    {
        asset::Handle<asset::ClipView> handle;
        const auto status = manager_->Load<asset::ClipView>(clip.id, &handle);
        if (status.Failed())
        {
            char what[256];
            FormatTo(what, sizeof(what), "a clip of the file could not be loaded: %s", status.Message());
            return Fail(what);
        }
        clip_handles_.Push(Move(handle));
    }
    world_->Query<ecs::Read<animation::SkeletonInstance>>().ForEach(
        [&](ecs::Entity e, const animation::SkeletonInstance&) { posed_.Push(e); });
    skinned_ = static_cast<u32>(posed_.Size());
    world_->Query<ecs::Read<animation::MorphWeights>>().ForEach([&](ecs::Entity e, const animation::MorphWeights&)
                                                                { morphed_.Push(e); });
    resolver_ = MakeUnique<animation::ManagerResolver>(manager_.Get());
    for (const ecs::Entity e : posed_)
    {
        BindPose(
            e);  // a skinned mesh no clip poses is drawn in its bind pose (the renderer refuses one with no palette)
    }
    animation_ = MakeUnique<animation::AnimationSystem>(resolver_.Get());
    current_clip_ = clips_.Size();
    if (animate_ && !clips_.Empty())
    {
        PlayClip(options.clip < clips_.Size() ? options.clip : 0);
    }
    return true;
}

void ViewerScene::PrepareMeshes()
{
    Vector<renderer::GpuMesh*> pending;
    world_->Query<ecs::Read<scene::MeshRenderer>>().ForEach(
        [&](const scene::MeshRenderer& mr)
        {
            renderer::GpuMesh* mesh = mr.mesh.Get();
            if (mesh == nullptr || mesh->Ready())
            {
                return;
            }
            for (const renderer::GpuMesh* known : pending)
            {
                if (known == mesh)
                {
                    return;  // several entities share a mesh
                }
            }
            pending.Push(mesh);
        });
    if (pending.Empty() || device_ == nullptr)
    {
        return;
    }
    device_->BeginFrame();
    rhi::CommandList* list = device_->CreateCommandList();
    if (list == nullptr)
    {
        device_->EndFrame();
        return;
    }
    list->Begin();
    for (renderer::GpuMesh* mesh : pending)
    {
        mesh->RecordPrepare(list);
    }
    list->End();
    const Array<rhi::CommandList*, 1> lists = {list};
    const bool submitted = device_->Submit(lists.Data(), 1) != 0;
    device_->EndFrame();
    device_->WaitIdle();
    if (submitted)
    {
        for (renderer::GpuMesh* mesh : pending)
        {
            mesh->MarkReady();
        }
    }
}

void ViewerScene::PlayClip(usize index)
{
    current_clip_ = index;
    // A clip of morph weights only plays on the entities that have weights; a skeletal clip on the entities of its
    // skeleton (a file can hold several). The others stop: a clip they cannot play would be skipped every frame.
    const asset::ClipView& clip = *clip_handles_[index].Get();
    const bool weights_only = clip.Skeleton().IsNone();
    const auto play = [&](ecs::Entity e)
    {
        animation::AnimationPlayer player;
        player.clip = clips_[index].id.Value();
        player.looping = true;
        world_->Set<animation::AnimationPlayer>(e, Move(player));
    };
    const auto stop = [&](ecs::Entity e)
    {
        if (world_->Get<animation::AnimationPlayer>(e) != nullptr &&
            world_->Remove<animation::AnimationPlayer>(e) != ecs::Status::Ok)
        {
            Log(LogCore, LogVerbosity::Warn, "model_viewer: a clip could not be stopped on an entity");
        }
    };
    for (const ecs::Entity e : posed_)
    {
        const animation::SkeletonInstance* inst = world_->Get<animation::SkeletonInstance>(e);
        const bool fits = weights_only ? world_->Get<animation::MorphWeights>(e) != nullptr
                                       : inst != nullptr && inst->skeleton == clip.Skeleton().Value();
        if (fits)
        {
            play(e);
        }
        else
        {
            stop(e);
            BindPose(e);
        }
    }
    for (const ecs::Entity e : morphed_)
    {
        if (world_->Get<animation::SkeletonInstance>(e) != nullptr)
        {
            continue;  // done above
        }
        if (weights_only)
        {
            play(e);
        }
        else
        {
            stop(e);
        }
    }
}

void ViewerScene::BindPose(ecs::Entity e)
{
    animation::SkeletonInstance* inst = world_->Get<animation::SkeletonInstance>(e);
    const asset::SkeletonView* skeleton = inst != nullptr
                                              ? resolver_->FindSkeleton(asset::ResourceId::FromValue(inst->skeleton))
                                              : nullptr;
    if (skeleton == nullptr)
    {
        return;
    }
    animation::LocalPose bind;
    animation::SetBindPose(*skeleton, &bind);
    animation::ModelPose model;
    animation::ModelFromLocal(*skeleton, bind, &model);
    inst->palette.Resize(bind.joint_count);
    animation::SkinningMatrices(*skeleton, model, Span<animation::Mat3x4>(inst->palette.Data(), inst->palette.Size()));
}

void ViewerScene::NextClip()
{
    if (!animate_ || clips_.Empty())
    {
        return;
    }
    PlayClip(current_clip_ + 1 < clips_.Size() ? current_clip_ + 1 : 0);
}

void ViewerScene::Update(f32 seconds, jobs::Scheduler* scheduler)
{
    if (world_.Get() == nullptr)
    {
        return;
    }
    PrepareMeshes();
    if (animate_ && current_clip_ < clips_.Size())
    {
        animation_->Update(world_.Get(), scheduler, paused_ ? 0.0f : seconds);
    }
    schedule_->Run(*world_, seconds);
}

Aabb ViewerScene::Bounds() const
{
    // A morphed mesh's bounds hold every target at full weight, so one broken target (deltas a hundred times the
    // mesh, as a converter wrote them in a file found on 2026-11-20) would frame a box thousands of times too big.
    // The meshes without targets frame the view when there are any; the morphed ones only when nothing else is.
    bool plain_exists = false;
    for (u32 slot = 0; scene_.Get() != nullptr && slot < scene_->SlotCount(); ++slot)
    {
        plain_exists = plain_exists ||
                       (scene_->SlotAlive(slot) && scene_->ObjectAt(slot).mesh->MorphTargetCount() == 0);
    }
    Aabb box;
    bool any = false;
    for (u32 slot = 0; scene_.Get() != nullptr && slot < scene_->SlotCount(); ++slot)
    {
        if (!scene_->SlotAlive(slot) || (plain_exists && scene_->ObjectAt(slot).mesh->MorphTargetCount() != 0))
        {
            continue;
        }
        const Aabb& b = scene_->ObjectAt(slot).bounds.box;
        if (!any)
        {
            box = b;
            any = true;
            continue;
        }
        box.min = Vec3(b.min.x < box.min.x ? b.min.x : box.min.x, b.min.y < box.min.y ? b.min.y : box.min.y,
                       b.min.z < box.min.z ? b.min.z : box.min.z);
        box.max = Vec3(b.max.x > box.max.x ? b.max.x : box.max.x, b.max.y > box.max.y ? b.max.y : box.max.y,
                       b.max.z > box.max.z ? b.max.z : box.max.z);
    }
    return box;
}

}  // namespace model_viewer
