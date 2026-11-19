// The model viewer's scene. See viewer_scene.h.
#include "viewer_scene.h"

#include "animation/pose.h"
#include "asset/cpu_loaders.h"
#include "asset/level.h"
#include "asset/memory_backend.h"
#include "algorithm.h"
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
    mesh_boxes_.Clear();
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
        else if (r.type == asset::types::kMesh)
        {
            asset::TypeRegistry types;
            asset::MeshView view;
            if (view.Open(ByteSpan(r.blob.Data(), r.blob.Size()), types).Failed())
            {
                continue;  // the manager reports it when the level loads the mesh
            }
            Aabb box;
            box.min = Vec3(view.BoundsMin()[0], view.BoundsMin()[1], view.BoundsMin()[2]);
            box.max = Vec3(view.BoundsMax()[0], view.BoundsMax()[1], view.BoundsMax()[2]);
            u32 position_offset = 0;
            bool has_position = false;
            for (u32 a = 0; a < view.AttributeCount(); ++a)
            {
                const asset::MeshAttribute attribute = view.Attribute(a);
                if (attribute.location == asset::kMeshLocationPosition && attribute.binding == 0 &&
                    attribute.format == rhi::VertexFormat::Float3)
                {
                    position_offset = attribute.offset;
                    has_position = true;
                }
            }
            if (view.MorphTargetCount() != 0 && has_position && view.VertexCount() != 0)
            {
                const ByteSpan bytes = view.VertexBytes();
                for (u32 v = 0; v < view.VertexCount(); ++v)
                {
                    f32 p[3];
                    MemCopy(p, bytes.Data() + static_cast<usize>(v) * view.VertexStride() + position_offset, sizeof(p));
                    const Vec3 at(p[0], p[1], p[2]);
                    box.min = v == 0 ? at : Vec3(Min(box.min.x, at.x), Min(box.min.y, at.y), Min(box.min.z, at.z));
                    box.max = v == 0 ? at : Vec3(Max(box.max.x, at.x), Max(box.max.y, at.y), Max(box.max.z, at.z));
                }
            }
            MeshBoxes boxes;
            boxes.box = box;
            for (u32 j = 0; view.Skinned() && j < view.SkinJointCount(); ++j)
            {
                const asset::MeshJointBounds jb = view.JointBounds(j);
                if (jb.used)
                {
                    Aabb joint_box;
                    joint_box.min = Vec3(jb.min[0], jb.min[1], jb.min[2]);
                    joint_box.max = Vec3(jb.max[0], jb.max[1], jb.max[2]);
                    boxes.joints.Push(j);
                    boxes.joint_boxes.Push(joint_box);
                }
            }
            mesh_boxes_.Insert(r.id.Value(), Move(boxes));
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
    // Each mesh entity's box in the world, as it is drawn now. A rigid mesh: its box under the entity's transform (the
    // vertices' own box for a morphed one: its blob bounds hold every target at full weight, and one broken target,
    // deltas a hundred times the mesh as a converter wrote them in a file found on 2026-11-20, framed a box thousands
    // of times the model). A skinned mesh: each joint's bind-pose box moved by its palette matrix, since a file's rest
    // pose need not be its bind pose (the same file stands up while its bind-pose box lies down).
    Aabb box;
    bool any = false;
    const auto add = [&](const Aabb& b)
    {
        if (!any)
        {
            box = b;
            any = true;
            return;
        }
        box.min = Vec3(Min(b.min.x, box.min.x), Min(b.min.y, box.min.y), Min(b.min.z, box.min.z));
        box.max = Vec3(Max(b.max.x, box.max.x), Max(b.max.y, box.max.y), Max(b.max.z, box.max.z));
    };
    if (world_.Get() == nullptr)
    {
        return box;
    }
    world_
        ->Query<ecs::Read<asset::MeshReference>, ecs::Read<ecs::WorldTransform>,
                ecs::Without<animation::SkeletonInstance>>()
        .ForEach(
            [&](const asset::MeshReference& reference, const ecs::WorldTransform& transform)
            {
                const MeshBoxes* local = mesh_boxes_.Find(reference.mesh.id);
                if (local != nullptr)
                {
                    add(TransformAabb(local->box, transform.matrix));
                }
            });
    world_
        ->Query<ecs::Read<asset::MeshReference>, ecs::Read<ecs::WorldTransform>,
                ecs::Read<animation::SkeletonInstance>>()
        .ForEach(
            [&](const asset::MeshReference& reference, const ecs::WorldTransform& transform,
                const animation::SkeletonInstance& instance)
            {
                const MeshBoxes* local = mesh_boxes_.Find(reference.mesh.id);
                if (local == nullptr)
                {
                    return;
                }
                if (local->joints.Empty() || instance.palette.Empty())
                {
                    add(TransformAabb(local->box, transform.matrix));
                    return;
                }
                for (usize k = 0; k < local->joints.Size(); ++k)
                {
                    if (local->joints[k] >= instance.palette.Size())
                    {
                        continue;
                    }
                    const animation::Mat3x4& m = instance.palette[local->joints[k]];  // row-major 3x4
                    const Aabb& jb = local->joint_boxes[k];
                    Aabb posed;
                    for (u32 corner = 0; corner < 8; ++corner)
                    {
                        const f32 x = (corner & 1) != 0 ? jb.max.x : jb.min.x;
                        const f32 y = (corner & 2) != 0 ? jb.max.y : jb.min.y;
                        const f32 z = (corner & 4) != 0 ? jb.max.z : jb.min.z;
                        const Vec3 at(m[0] * x + m[1] * y + m[2] * z + m[3], m[4] * x + m[5] * y + m[6] * z + m[7],
                                      m[8] * x + m[9] * y + m[10] * z + m[11]);
                        posed.min = corner == 0
                                        ? at
                                        : Vec3(Min(posed.min.x, at.x), Min(posed.min.y, at.y), Min(posed.min.z, at.z));
                        posed.max = corner == 0
                                        ? at
                                        : Vec3(Max(posed.max.x, at.x), Max(posed.max.y, at.y), Max(posed.max.z, at.z));
                    }
                    add(TransformAabb(posed, transform.matrix));
                }
            });
    return box;
}

}  // namespace model_viewer
