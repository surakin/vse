#include "scene/render_sync.h"

#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "animation/components.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "lang/scalar.h"
#include "log/assert.h"

namespace engine_placeholder::scene
{

namespace
{

using ecs::Added;
using ecs::Changed;
using ecs::Entity;
using ecs::Query;
using ecs::Read;
using ecs::With;
using ecs::Without;
using ecs::Write;

bool SameMatrixBits(const Mat4& a, const Mat4& b)
{
    for (int c = 0; c < 4; ++c)
    {
        if (a.col[c].x != b.col[c].x || a.col[c].y != b.col[c].y || a.col[c].z != b.col[c].z ||
            a.col[c].w != b.col[c].w)
        {
            return false;
        }
    }
    return true;
}

bool SameLight(const renderer::Light& a, const renderer::Light& b)
{
    return a.type == b.type && a.color.x == b.color.x && a.color.y == b.color.y && a.color.z == b.color.z &&
           a.intensity == b.intensity && a.direction.x == b.direction.x && a.direction.y == b.direction.y &&
           a.direction.z == b.direction.z && a.position.x == b.position.x && a.position.y == b.position.y &&
           a.position.z == b.position.z && a.radius == b.radius;
}

bool FiniteVec(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

// The values RenderScene::AddLight would abort on.
bool LightValid(const renderer::Light& l)
{
    if (!FiniteVec(l.color) || !FiniteVec(l.direction) || !FiniteVec(l.position) || !IsFinite(l.intensity) ||
        !IsFinite(l.radius) || l.intensity < 0.0f)
    {
        return false;
    }
    return l.type != renderer::LightType::Point || l.radius > 0.0f;
}

renderer::Light MakeLight(const LightSource& source, const Mat4& world)
{
    renderer::Light light;
    light.type = source.type;
    light.color = source.color;
    light.intensity = source.intensity;
    light.radius = source.radius;
    light.position = Vec3(world.col[3].x, world.col[3].y, world.col[3].z);
    const Vec3 x(world.col[0].x, world.col[0].y, world.col[0].z);
    const Vec3 y(world.col[1].x, world.col[1].y, world.col[1].z);
    const Vec3 z(world.col[2].x, world.col[2].y, world.col[2].z);
    const Vec3 turned = x * source.direction.x + y * source.direction.y + z * source.direction.z;
    light.direction = turned.LengthSquared() > 0.0f ? turned.Normalized() : source.direction;
    return light;
}

bool MeshUsable(const MeshRenderer& mr)
{
    return mr.mesh && mr.mesh->Valid();
}

u32 FlagsOf(const MeshRenderer& mr, bool hidden)
{
    return mr.flags | (hidden ? renderer::ObjectFlags::kHidden : 0u);
}

// The queries for one value of the Hidden filter (With<Hidden> or Without<Hidden>).
template <class HiddenFilter> struct MeshQueries
{
    using Create = Query<Read<MeshRenderer>, Read<ecs::WorldTransform>, Without<RenderHandle>, HiddenFilter>;
    using Edited = Query<Changed<MeshRenderer>, Read<MeshRenderer>, Read<ecs::WorldTransform>, Write<RenderHandle>,
                         HiddenFilter>;
    using Moved = Query<Added<RenderHandle>, Read<MeshRenderer>, Read<RenderHandle>, HiddenFilter>;
    UniquePtr<Create> create;
    UniquePtr<Edited> edited;
    UniquePtr<Moved> moved;
    static constexpr bool kHidden = IsSame<HiddenFilter, With<Hidden>>;
};

using LightCreate = Query<Read<LightSource>, Read<ecs::WorldTransform>, Without<RenderHandle>, Without<MeshRenderer>>;
using LightEdited = Query<Changed<LightSource>, Read<LightSource>, Read<ecs::WorldTransform>, Read<RenderHandle>,
                          Without<MeshRenderer>>;
using TransformMoved = Query<Changed<ecs::WorldTransform>, Read<ecs::WorldTransform>, Read<RenderHandle>>;
using SkinnedPalettes = Query<Read<animation::SkeletonInstance>, Read<RenderHandle>>;
using ObjectOrphans = Query<Added<RenderHandle>, Write<RenderHandle>, Without<MeshRenderer>>;
using LightOrphans = Query<Added<RenderHandle>, Write<RenderHandle>, Without<LightSource>>;

class RenderSync
{
public:
    RenderSync() = default;
    RenderSync(RenderSync&&) = default;

    void operator()(ecs::SystemContext& context)
    {
        SceneSyncReport& report = context.Resource<SceneSyncReport>();
        ++report.frames;
        renderer::RenderScene* scene = context.Resource<SceneLink>().scene;
        if (scene == nullptr)
        {
            ++report.no_scene;
            return;
        }
        if (scene_ == nullptr || world_id_ != context.WorldId())
        {
            ENGINE_ASSERT(scene_ == nullptr || scene_ == scene,
                          "RenderSync: the linked RenderScene changed between frames");
            Build(context);
            scene_ = scene;
            world_id_ = context.WorldId();
            keep_.Resize(scene->ObjectCapacity());
        }
        ENGINE_ASSERT(scene_ == scene, "RenderSync: the linked RenderScene changed between frames");
        Sync(context, *scene, report);
    }

private:
    void Build(ecs::SystemContext& context)
    {
        yes_.create = MakeUnique<MeshQueries<With<Hidden>>::Create>(
            context.Query<Read<MeshRenderer>, Read<ecs::WorldTransform>, Without<RenderHandle>, With<Hidden>>());
        yes_.edited = MakeUnique<MeshQueries<With<Hidden>>::Edited>(
            context.Query<Changed<MeshRenderer>, Read<MeshRenderer>, Read<ecs::WorldTransform>, Write<RenderHandle>,
                          With<Hidden>>());
        yes_.moved = MakeUnique<MeshQueries<With<Hidden>>::Moved>(
            context.Query<Added<RenderHandle>, Read<MeshRenderer>, Read<RenderHandle>, With<Hidden>>());
        no_.create = MakeUnique<MeshQueries<Without<Hidden>>::Create>(
            context.Query<Read<MeshRenderer>, Read<ecs::WorldTransform>, Without<RenderHandle>, Without<Hidden>>());
        no_.edited = MakeUnique<MeshQueries<Without<Hidden>>::Edited>(
            context.Query<Changed<MeshRenderer>, Read<MeshRenderer>, Read<ecs::WorldTransform>, Write<RenderHandle>,
                          Without<Hidden>>());
        no_.moved = MakeUnique<MeshQueries<Without<Hidden>>::Moved>(
            context.Query<Added<RenderHandle>, Read<MeshRenderer>, Read<RenderHandle>, Without<Hidden>>());
        light_create_ = MakeUnique<LightCreate>(
            context
                .Query<Read<LightSource>, Read<ecs::WorldTransform>, Without<RenderHandle>, Without<MeshRenderer>>());
        light_edited_ = MakeUnique<LightEdited>(
            context.Query<Changed<LightSource>, Read<LightSource>, Read<ecs::WorldTransform>, Read<RenderHandle>,
                          Without<MeshRenderer>>());
        transform_moved_ = MakeUnique<TransformMoved>(
            context.Query<Changed<ecs::WorldTransform>, Read<ecs::WorldTransform>, Read<RenderHandle>>());
        skinned_ = MakeUnique<SkinnedPalettes>(context.Query<Read<animation::SkeletonInstance>, Read<RenderHandle>>());
        object_orphans_ = MakeUnique<ObjectOrphans>(
            context.Query<Added<RenderHandle>, Write<RenderHandle>, Without<MeshRenderer>>());
        light_orphans_ = MakeUnique<LightOrphans>(
            context.Query<Added<RenderHandle>, Write<RenderHandle>, Without<LightSource>>());
        pending_.Clear();
        keep_.Clear();
        last_removal_seq_ = 0;  // a new world numbers its removals from 1
    }

    void DropObject(renderer::RenderScene& scene, renderer::ObjectId id, SceneSyncReport& report)
    {
        if (id.IsValid() && scene.FindObject(id) != nullptr)
        {
            scene.RemoveObject(id);
            keep_[id.index].Reset();
            ++report.objects_removed;
        }
    }
    // Drops every object of a handle (one per mesh slot) and forgets them there.
    void DropObjects(renderer::RenderScene& scene, RenderHandle& handle, SceneSyncReport& report)
    {
        DropObject(scene, handle.object, report);
        for (u32 i = 0; i < handle.more_count; ++i)
        {
            DropObject(scene, handle.more[i], report);
            handle.more[i] = renderer::ObjectId{};
        }
        handle.more_count = 0;
        handle.object = renderer::ObjectId{};
    }
    void DropLight(renderer::RenderScene& scene, renderer::LightId id, SceneSyncReport& report)
    {
        if (id.IsValid() && scene.FindLight(id) != nullptr)
        {
            scene.RemoveLight(id);
            ++report.lights_removed;
        }
    }

    // Points a skinned object at the entity's joint palette (the animation runtime's SkeletonInstance): the renderer
    // borrows the memory and copies it when the frame is prepared, so only a changed pointer or count is written here.
    void ApplySkin(renderer::RenderScene& scene, const RenderHandle& handle,
                   const animation::SkeletonInstance* skeleton, SceneSyncReport& report)
    {
        for (u32 i = 0; i < ObjectCountOf(handle); ++i)
        {
            ApplySkinTo(scene, ObjectAt(handle, i), skeleton, report);
        }
    }
    void ApplySkinTo(renderer::RenderScene& scene, renderer::ObjectId id, const animation::SkeletonInstance* skeleton,
                     SceneSyncReport& report)
    {
        const renderer::RenderObject* object = id.IsValid() ? scene.FindObject(id) : nullptr;
        if (object == nullptr || !object->mesh->Skinned())
        {
            return;
        }
        const f32* matrices = nullptr;
        u32 count = 0;
        if (skeleton != nullptr && skeleton->palette.Size() > 0 && skeleton->palette.Size() <= 256)
        {
            matrices = skeleton->palette.Data()->Data();
            count = static_cast<u32>(skeleton->palette.Size());
        }
        if (object->joint_matrices != matrices || object->joint_count != count)
        {
            scene.SetJointMatrices(id, matrices, count);
            ++report.joints_set;
        }
    }

    // What an entity draws: one part for a mesh drawn whole, one per slot for a mesh with slots and slot materials.
    struct Part
    {
        u32 material = 0;
        u32 first_index = 0;
        u32 index_count = 0;
    };
    static u32 PartsOf(const MeshRenderer& mr, Part* parts)
    {
        const u32 ranges = mr.mesh->RangeCount();
        if (mr.slot_count == 0 || ranges == 0 || ranges > kMaxSlotMaterials)
        {
            parts[0] = Part{mr.material, 0, 0};
            return 1;
        }
        for (u32 i = 0; i < ranges; ++i)
        {
            const renderer::MeshRange range = mr.mesh->Range(i);
            const u32 chosen = i < mr.slot_count ? mr.slot_materials[i] : kInheritMaterial;
            parts[i] = Part{chosen == kInheritMaterial ? mr.material : chosen, range.first_index, range.index_count};
        }
        return ranges;
    }
    static renderer::ObjectId ObjectAt(const RenderHandle& handle, u32 i)
    {
        return i == 0 ? handle.object : handle.more[i - 1];
    }
    static u32 ObjectCountOf(const RenderHandle& handle) { return handle.object.IsValid() ? 1 + handle.more_count : 0; }

    // Adds the entity's objects into `handle`; its first object is invalid when the mesh is unusable (counted) or the
    // scene is full (counted, and nothing of the entity stays).
    void AddObjects(renderer::RenderScene& scene, const MeshRenderer& mr, const Mat4& world, bool hidden,
                    SceneSyncReport& report, RenderHandle* handle)
    {
        handle->object = renderer::ObjectId{};
        handle->more_count = 0;
        if (!MeshUsable(mr))
        {
            ++report.invalid;
            return;
        }
        if (mr.slot_count != 0 && mr.mesh->RangeCount() > kMaxSlotMaterials)
        {
            ++report.invalid;  // drawn whole, below
        }
        Part parts[kMaxSlotMaterials];
        const u32 count = PartsOf(mr, parts);
        for (u32 i = 0; i < count; ++i)
        {
            renderer::RenderObjectDesc desc;
            desc.mesh = mr.mesh.Get();
            desc.material = parts[i].material;
            desc.first_index = parts[i].first_index;
            desc.index_count = parts[i].index_count;
            desc.transform = world;
            desc.flags = FlagsOf(mr, hidden);
            const renderer::ObjectId id = scene.AddObject(desc);
            if (!id.IsValid())
            {
                ++report.refused_full;
                DropObjects(scene, *handle, report);  // all of the entity or none
                return;
            }
            keep_[id.index] = mr.mesh;
            ++report.objects_added;
            if (i == 0)
            {
                handle->object = id;
            }
            else
            {
                handle->more[handle->more_count++] = id;
            }
        }
    }

    // Brings an existing object in line with its component: re-adds it if the mesh or material differ, else updates the
    // flags.
    void Refresh(renderer::RenderScene& scene, ecs::SystemContext& context, Entity entity, RenderHandle& handle,
                 const MeshRenderer& mr, const Mat4& world, bool hidden, SceneSyncReport& report)
    {
        const renderer::RenderObject* object = handle.object.IsValid() ? scene.FindObject(handle.object) : nullptr;
        if (object == nullptr)
        {
            return;
        }
        bool same = MeshUsable(mr) && object->mesh == mr.mesh.Get();
        if (same)
        {
            Part parts[kMaxSlotMaterials];
            const u32 count = PartsOf(mr, parts);
            same = count == ObjectCountOf(handle);
            for (u32 i = 0; same && i < count; ++i)
            {
                const renderer::RenderObject* part = scene.FindObject(ObjectAt(handle, i));
                same = part != nullptr && part->material == parts[i].material &&
                       part->first_index == parts[i].first_index && part->index_count == parts[i].index_count;
            }
        }
        if (!same)
        {
            DropObjects(scene, handle, report);
            AddObjects(scene, mr, world, hidden, report, &handle);
            ApplySkin(scene, handle, context.Peek<animation::SkeletonInstance>(entity), report);
            ++report.readded;
            if (!handle.object.IsValid() && !handle.light.IsValid())
            {
                context.Commands().Remove<RenderHandle>(entity);  // nothing mirrored: the create query retries it
            }
            return;
        }
        const u32 flags = FlagsOf(mr, hidden);
        for (u32 i = 0; i < ObjectCountOf(handle); ++i)
        {
            const renderer::RenderObject* part = scene.FindObject(ObjectAt(handle, i));
            if (part != nullptr && part->flags != flags)
            {
                scene.SetFlags(ObjectAt(handle, i), flags);
                ++report.flags_set;
            }
        }
    }

    void SetLightIfChanged(renderer::RenderScene& scene, renderer::LightId id, const LightSource& source,
                           const Mat4& world, SceneSyncReport& report)
    {
        const renderer::Light* current = id.IsValid() ? scene.FindLight(id) : nullptr;
        if (current == nullptr)
        {
            return;
        }
        const renderer::Light next = MakeLight(source, world);
        if (!LightValid(next))
        {
            ++report.invalid;
            return;
        }
        if (!SameLight(*current, next))
        {
            scene.SetLight(id, next);
            ++report.lights_set;
        }
    }

    template <class Queries>
    void SyncMesh(ecs::SystemContext& context, renderer::RenderScene& scene, Queries& q, SceneSyncReport& report)
    {
        constexpr bool hidden = Queries::kHidden;
        q.edited->ForEach([&](Entity entity, const MeshRenderer& mr, const ecs::WorldTransform& w, RenderHandle& handle)
                          { Refresh(scene, context, entity, handle, mr, w.matrix, hidden, report); });
        // A tag change moves the entity to another archetype, which the arrival stamp of its handle shows.
        q.moved->ForEach(
            [&](const MeshRenderer& mr, const RenderHandle& handle)
            {
                for (u32 i = 0; i < ObjectCountOf(handle); ++i)
                {
                    const renderer::RenderObject* object = scene.FindObject(ObjectAt(handle, i));
                    if (object != nullptr && object->flags != FlagsOf(mr, hidden))
                    {
                        scene.SetFlags(ObjectAt(handle, i), FlagsOf(mr, hidden));
                        ++report.flags_set;
                    }
                }
            });
    }

    template <class Queries>
    void CreateMesh(ecs::SystemContext& context, renderer::RenderScene& scene, Queries& q, SceneSyncReport& report)
    {
        constexpr bool hidden = Queries::kHidden;
        q.create->ForEach(
            [&](Entity entity, const MeshRenderer& mr, const ecs::WorldTransform& w)
            {
                RenderHandle handle;
                AddObjects(scene, mr, w.matrix, hidden, report, &handle);
                if (handle.object.IsValid())
                {
                    ApplySkin(scene, handle, context.Peek<animation::SkeletonInstance>(entity), report);
                    context.Commands().Add<RenderHandle>(entity, handle);
                    pending_.Push(Pending{entity, handle});
                }
            });
    }

    void Sync(ecs::SystemContext& context, renderer::RenderScene& scene, SceneSyncReport& report)
    {
        // 0. Objects made last frame for entities that were destroyed before their handle was applied.
        for (usize i = 0; i < pending_.Size(); ++i)
        {
            if (!context.Exists(pending_[i].entity))
            {
                DropObjects(scene, pending_[i].handle, report);
                DropLight(scene, pending_[i].handle.light, report);
            }
        }
        pending_.Clear();

        // 1. Destroyed entities and removed handles, from the world's removal log.
        const usize removed = context.RemovedCount<RenderHandle>();
        for (usize i = 0; i < removed; ++i)
        {
            const ecs::RemovedComponent<RenderHandle> entry = context.RemovedAt<RenderHandle>(i);
            if (entry.seq <= last_removal_seq_)
            {
                continue;
            }
            last_removal_seq_ = entry.seq;
            RenderHandle removed_handle = entry.value;
            DropObjects(scene, removed_handle, report);
            DropLight(scene, removed_handle.light, report);
        }

        // 2. Entities that lost their MeshRenderer or LightSource but kept a handle.
        object_orphans_->ForEach(
            [&](RenderHandle& handle)
            {
                if (handle.object.IsValid())
                {
                    DropObjects(scene, handle, report);
                }
            });
        light_orphans_->ForEach(
            [&](Entity entity, RenderHandle& handle)
            {
                if (handle.light.IsValid())
                {
                    DropLight(scene, handle.light, report);
                    handle.light = renderer::LightId{};
                }
                if (!handle.object.IsValid() && !handle.light.IsValid())
                {
                    context.Commands().Remove<RenderHandle>(entity);
                }
            });

        // 3. Changed meshes, materials, flags and the Hidden tag.
        SyncMesh(context, scene, yes_, report);
        SyncMesh(context, scene, no_, report);

        // 4. Moved objects and lights, only where the matrix differs.
        transform_moved_->ForEach(
            [&](Entity entity, const ecs::WorldTransform& w, const RenderHandle& handle)
            {
                for (u32 i = 0; i < ObjectCountOf(handle); ++i)
                {
                    const renderer::RenderObject* object = scene.FindObject(ObjectAt(handle, i));
                    if (object != nullptr && !SameMatrixBits(object->transform, w.matrix))
                    {
                        scene.SetTransform(ObjectAt(handle, i), w.matrix);
                        ++report.transforms_set;
                    }
                }
                if (handle.light.IsValid())
                {
                    if (const LightSource* source = context.Peek<LightSource>(entity))
                    {
                        SetLightIfChanged(scene, handle.light, *source, w.matrix, report);
                    }
                }
            });
        light_edited_->ForEach([&](const LightSource& source, const ecs::WorldTransform& w, const RenderHandle& handle)
                               { SetLightIfChanged(scene, handle.light, source, w.matrix, report); });

        // 4b. Joint palettes: an entity's palette may have been rebuilt at another address (the animation system fills
        // it in place, but a skeleton change or a first fill reallocates it).
        skinned_->ForEach([&](const animation::SkeletonInstance& skeleton, const RenderHandle& handle)
                          { ApplySkin(scene, handle, &skeleton, report); });

        // 5. New entities.
        CreateMesh(context, scene, yes_, report);
        CreateMesh(context, scene, no_, report);
        light_create_->ForEach(
            [&](Entity entity, const LightSource& source, const ecs::WorldTransform& w)
            {
                const renderer::Light light = MakeLight(source, w.matrix);
                if (!LightValid(light))
                {
                    ++report.invalid;
                    return;
                }
                const renderer::LightId id = scene.AddLight(light);
                if (!id.IsValid())
                {
                    ++report.refused_full;
                    return;
                }
                ++report.lights_added;
                RenderHandle handle;
                handle.light = id;
                context.Commands().Add<RenderHandle>(entity, handle);
                pending_.Push(Pending{entity, handle});
            });

        yes_.create->Finish();
        yes_.edited->Finish();
        yes_.moved->Finish();
        no_.create->Finish();
        no_.edited->Finish();
        no_.moved->Finish();
        light_create_->Finish();
        light_edited_->Finish();
        transform_moved_->Finish();
        skinned_->Finish();
        object_orphans_->Finish();
        light_orphans_->Finish();
    }

    struct Pending
    {
        Entity entity;
        RenderHandle handle;
    };

    MeshQueries<With<Hidden>> yes_;
    MeshQueries<Without<Hidden>> no_;
    UniquePtr<LightCreate> light_create_;
    UniquePtr<LightEdited> light_edited_;
    UniquePtr<TransformMoved> transform_moved_;
    UniquePtr<SkinnedPalettes> skinned_;
    UniquePtr<ObjectOrphans> object_orphans_;
    UniquePtr<LightOrphans> light_orphans_;
    Vector<Pending> pending_;
    Vector<renderer::GpuMeshPtr> keep_;
    renderer::RenderScene* scene_ = nullptr;
    u64 world_id_ = 0;
    u64 last_removal_seq_ = 0;
};

Vec3 Axis(const Vec4& column)
{
    const Vec3 v(column.x, column.y, column.z);
    return v.LengthSquared() > 0.0f ? v.Normalized() : v;
}

}  // namespace

ecs::SystemDesc MakeRenderSyncSystem(const char* name)
{
    ecs::SystemDesc desc(name, ecs::detail::SystemFn(RenderSync()));
    return Move(desc)
        .Reads<MeshRenderer, LightSource, ecs::WorldTransform, animation::SkeletonInstance>()
        .Writes<RenderHandle>()
        .WritesResource<SceneLink>()
        .WritesResource<SceneSyncReport>();
}

ecs::SystemDesc MakeCameraSystem(const char* name)
{
    ecs::SystemDesc desc(name, ecs::detail::SystemFn(
                                   [](ecs::SystemContext& context)
                                   {
                                       CameraOutput& out = context.Resource<CameraOutput>();
                                       out.valid = false;
                                       const Entity entity = context.ReadResource<ActiveCamera>().entity;
                                       const Camera* camera = context.Peek<Camera>(entity);
                                       const ecs::WorldTransform* world = context.Peek<ecs::WorldTransform>(entity);
                                       if (camera == nullptr || world == nullptr)
                                       {
                                           return;
                                       }
                                       out.camera.position = Vec3(world->matrix.col[3].x, world->matrix.col[3].y,
                                                                  world->matrix.col[3].z);
                                       out.camera.rotation.col[0] = Axis(world->matrix.col[0]);
                                       out.camera.rotation.col[1] = Axis(world->matrix.col[1]);
                                       out.camera.rotation.col[2] = Axis(world->matrix.col[2]);
                                       out.camera.fov_y = camera->fov_y;
                                       out.camera.aspect = camera->aspect;
                                       out.camera.near_plane = camera->near_plane;
                                       out.camera.far_plane = camera->far_plane;
                                       out.valid = true;
                                   }));
    return Move(desc).Reads<Camera, ecs::WorldTransform>().ReadsResource<ActiveCamera>().WritesResource<CameraOutput>();
}

}  // namespace engine_placeholder::scene
