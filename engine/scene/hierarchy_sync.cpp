#include "containers/array.h"
#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "lang/scalar.h"
#include "log/assert.h"
#include "scene/render_sync.h"

namespace vse::scene
{

namespace
{

using ecs::Entity;
using ecs::Query;
using ecs::Read;
using ecs::With;
using ecs::Without;

template <class HiddenFilter> struct HierarchyQueries
{
    using Create = Query<Read<InstanceHierarchy>, Read<ecs::WorldTransform>, Without<HierarchyHandle>, HiddenFilter>;
    using All = Query<Read<InstanceHierarchy>, Read<ecs::WorldTransform>, Read<HierarchyHandle>, HiddenFilter>;
    UniquePtr<Create> create;
    UniquePtr<All> all;
    static constexpr bool kHidden = IsSame<HiddenFilter, With<Hidden>>;
};
using HierarchyOrphans = Query<Read<HierarchyHandle>, Without<InstanceHierarchy>>;

class HierarchySync
{
public:
    HierarchySync() = default;
    HierarchySync(HierarchySync&&) = default;

    void operator()(ecs::SystemContext& context)
    {
        SceneSyncReport& report = context.Resource<SceneSyncReport>();
        const SceneLink& link = context.Resource<SceneLink>();
        origin_ = link.origin;
        renderer::RenderScene* scene = link.scene;
        if (scene == nullptr)
        {
            return;
        }
        if (yes_.all == nullptr || world_id_ != context.WorldId())
        {
            yes_.create = MakeUnique<HierarchyQueries<With<Hidden>>::Create>(
                context.Query<Read<InstanceHierarchy>, Read<ecs::WorldTransform>, Without<HierarchyHandle>,
                              With<Hidden>>());
            yes_.all = MakeUnique<HierarchyQueries<With<Hidden>>::All>(
                context
                    .Query<Read<InstanceHierarchy>, Read<ecs::WorldTransform>, Read<HierarchyHandle>, With<Hidden>>());
            no_.create = MakeUnique<HierarchyQueries<Without<Hidden>>::Create>(
                context.Query<Read<InstanceHierarchy>, Read<ecs::WorldTransform>, Without<HierarchyHandle>,
                              Without<Hidden>>());
            no_.all = MakeUnique<HierarchyQueries<Without<Hidden>>::All>(
                context.Query<Read<InstanceHierarchy>, Read<ecs::WorldTransform>, Read<HierarchyHandle>,
                              Without<Hidden>>());
            orphans_ = MakeUnique<HierarchyOrphans>(context.Query<Read<HierarchyHandle>, Without<InstanceHierarchy>>());
            pending_.Clear();
            last_removal_seq_ = 0;
            world_id_ = context.WorldId();
        }
        // An add whose handle command could not land because the entity was destroyed at once.
        for (const Pending& p : pending_)
        {
            if (!context.Exists(p.entity))
            {
                Drop(*scene, p.id, report);
            }
        }
        pending_.Clear();
        const usize removed = context.RemovedCount<HierarchyHandle>();
        for (usize i = 0; i < removed; ++i)
        {
            const ecs::RemovedComponent<HierarchyHandle> entry = context.RemovedAt<HierarchyHandle>(i);
            if (entry.seq <= last_removal_seq_)
            {
                continue;
            }
            last_removal_seq_ = entry.seq;
            Drop(*scene, entry.value.id, report);
        }
        orphans_->ForEach(
            [&](Entity entity, const HierarchyHandle& handle)
            {
                Drop(*scene, handle.id, report);
                context.Commands().Remove<HierarchyHandle>(entity);
            });
        Sync(context, *scene, yes_, report);
        Sync(context, *scene, no_, report);
        yes_.create->Finish();
        yes_.all->Finish();
        no_.create->Finish();
        no_.all->Finish();
        orphans_->Finish();
    }

private:
    struct Pending
    {
        Entity entity;
        renderer::InstancedId id;
    };
    struct Held
    {
        renderer::InstancedId id;
        Array<renderer::GpuMeshPtr, renderer::kMaxSceneTreeMeshes> meshes;
    };

    template <class Queries>
    void Sync(ecs::SystemContext& context, renderer::RenderScene& scene_ref, Queries& q, SceneSyncReport& report)
    {
        renderer::RenderScene* scene = &scene_ref;
        constexpr bool hidden = Queries::kHidden;
        q.all->ForEach(
            [&](Entity entity, const InstanceHierarchy& mesh, const ecs::WorldTransform& w,
                const HierarchyHandle& handle)
            {
                Refresh(mesh, report);
                const renderer::InstancedObject* current = scene->FindInstanced(handle.id);
                if (current == nullptr)
                {
                    context.Commands().Remove<HierarchyHandle>(entity);  // the create query retries it
                    return;
                }
                renderer::InstancedObject want;
                if (!Make(mesh, w, origin_, hidden, &want))
                {
                    Drop(*scene, handle.id, report);
                    context.Commands().Remove<HierarchyHandle>(entity);
                    ++report.invalid;
                    return;
                }
                if (!Same(*current, want))
                {
                    scene->SetInstanced(handle.id, want);
                    ++report.hierarchies_set;
                    Hold(handle.id, mesh.meshes);
                }
            });
        q.create->ForEach(
            [&](Entity entity, const InstanceHierarchy& mesh, const ecs::WorldTransform& w)
            {
                Refresh(mesh, report);
                renderer::InstancedObject object;
                if (!Make(mesh, w, origin_, hidden, &object))
                {
                    ++report.invalid;
                    return;
                }
                const renderer::InstancedId id = scene->AddInstanced(object);
                if (!id.IsValid())
                {
                    ++report.refused_full;
                    return;
                }
                ++report.hierarchies_added;
                HierarchyHandle handle;
                handle.id = id;
                Hold(id, mesh.meshes);
                context.Commands().Add<HierarchyHandle>(entity, handle);
                pending_.Push(Pending{entity, id});
            });
    }

    // Recomposes the marked part of the tree before the scene looks at it.
    static void Refresh(const InstanceHierarchy& h, SceneSyncReport& report)
    {
        if (h.tree.Get() != nullptr && h.tree->NeedsUpdate())
        {
            report.hierarchy_nodes_updated += h.tree->UpdateWorldTransforms(nullptr);
        }
    }

    static bool Same(const renderer::InstancedObject& a, const renderer::InstancedObject& b)
    {
        for (u32 i = 0; i < renderer::kMaxSceneTreeMeshes; ++i)
        {
            if (a.tree_meshes[i] != b.tree_meshes[i] || a.tree_materials[i] != b.tree_materials[i])
            {
                return false;
            }
        }
        return a.tree.Get() == b.tree.Get() && a.flags == b.flags && a.max_distance == b.max_distance &&
               a.min_size_ratio == b.min_size_ratio && a.position.x == b.position.x && a.position.y == b.position.y &&
               a.position.z == b.position.z && a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y &&
               a.rotation.z == b.rotation.z && a.rotation.w == b.rotation.w && a.scale == b.scale;
    }

    static bool Make(const InstanceHierarchy& h, const ecs::WorldTransform& w, const Vec3d& origin, bool hidden,
                     renderer::InstancedObject* out)
    {
        if (h.tree.Get() == nullptr || h.tree->NodeCount() == 0 || h.tree->MeshCount() > renderer::kMaxSceneTreeMeshes)
        {
            return false;
        }
        for (u32 i = 0; i < h.tree->MeshCount(); ++i)
        {
            if (h.meshes[i].Get() == nullptr || !h.meshes[i]->Valid())
            {
                return false;
            }
        }
        if (!DecomposeUniform(RelativeMatrix(w, origin), &out->position, &out->rotation, &out->scale))
        {
            return false;
        }
        out->tree = h.tree;
        for (u32 i = 0; i < renderer::kMaxSceneTreeMeshes; ++i)
        {
            out->tree_meshes[i] = h.meshes[i].Get();
            out->tree_materials[i] = h.materials[i];
        }
        out->flags = h.flags | (hidden ? renderer::ObjectFlags::kHidden : 0u);
        return true;
    }

    // The meshes the scene's entries draw: a removal-tracked handle cannot hold a share, so the system does.
    void Hold(renderer::InstancedId id, const Array<renderer::GpuMeshPtr, renderer::kMaxSceneTreeMeshes>& meshes)
    {
        for (Held& h : held_)
        {
            if (h.id == id)
            {
                h.meshes = meshes;
                return;
            }
        }
        held_.Push(Held{id, meshes});
    }

    void Drop(renderer::RenderScene& scene, renderer::InstancedId id, SceneSyncReport& report)
    {
        if (id.IsValid() && scene.FindInstanced(id) != nullptr)
        {
            scene.RemoveInstanced(id);
            ++report.hierarchies_removed;
        }
        for (usize i = 0; i < held_.Size(); ++i)
        {
            if (held_[i].id == id)
            {
                held_[i] = held_[held_.Size() - 1];
                held_.Pop();
                break;
            }
        }
    }

    HierarchyQueries<With<Hidden>> yes_;
    HierarchyQueries<Without<Hidden>> no_;
    UniquePtr<HierarchyOrphans> orphans_;
    Vector<Pending> pending_;
    Vector<Held> held_;
    Vec3d origin_ = Vec3d(0, 0, 0);
    u64 world_id_ = 0;
    u64 last_removal_seq_ = 0;
};

}  // namespace

ecs::SystemDesc MakeHierarchySyncSystem(const char* name)
{
    ecs::SystemDesc desc(name, ecs::detail::SystemFn(HierarchySync()));
    return Move(desc)
        .Reads<InstanceHierarchy, Hidden, ecs::WorldTransform>()
        .Writes<HierarchyHandle>()
        .WritesResource<SceneLink>()
        .WritesResource<SceneSyncReport>();
}

}  // namespace vse::scene
