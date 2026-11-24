#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "ecs/query.h"
#include "ecs/transform.h"
#include "lang/scalar.h"
#include "log/assert.h"
#include "scene/render_sync.h"

namespace vse::scene
{

bool DecomposeUniform(const Mat4& matrix, Vec3* position, Quaternion* rotation, f32* scale)
{
    const Vec3 c0(matrix.col[0].x, matrix.col[0].y, matrix.col[0].z);
    const Vec3 c1(matrix.col[1].x, matrix.col[1].y, matrix.col[1].z);
    const Vec3 c2(matrix.col[2].x, matrix.col[2].y, matrix.col[2].z);
    const Vec3 t(matrix.col[3].x, matrix.col[3].y, matrix.col[3].z);
    const f32 s = c0.Length();
    if (!IsFinite(s) || !(s > 0.0f) || !IsFinite(t.x) || !IsFinite(t.y) || !IsFinite(t.z))
    {
        return false;
    }
    const f32 tolerance = 1.0e-3f * s;
    if (Abs(c1.Length() - s) > tolerance || Abs(c2.Length() - s) > tolerance)
    {
        return false;
    }
    const Vec3 x = c0 * (1.0f / s);
    const Vec3 y = c1 * (1.0f / s);
    const Vec3 z = c2 * (1.0f / s);
    if (Abs(x.Dot(y)) > 1.0e-3f || Abs(x.Dot(z)) > 1.0e-3f || Abs(y.Dot(z)) > 1.0e-3f)
    {
        return false;
    }
    if (x.Cross(y).Dot(z) < 0.0f)  // a mirror
    {
        return false;
    }
    *position = t;
    *rotation = Quaternion::FromMat3(Mat3::FromBasis(x, y, z));
    *scale = s;
    return true;
}

namespace
{

using ecs::Entity;
using ecs::Query;
using ecs::Read;
using ecs::With;
using ecs::Without;

template <class HiddenFilter> struct InstancedQueries
{
    using Create = Query<Read<InstancedMesh>, Read<ecs::WorldTransform>, Without<InstancedHandle>, HiddenFilter>;
    using All = Query<Read<InstancedMesh>, Read<ecs::WorldTransform>, Read<InstancedHandle>, HiddenFilter>;
    UniquePtr<Create> create;
    UniquePtr<All> all;
    static constexpr bool kHidden = IsSame<HiddenFilter, With<Hidden>>;
};
using InstancedOrphans = Query<Read<InstancedHandle>, Without<InstancedMesh>>;

class InstancedSync
{
public:
    InstancedSync() = default;
    InstancedSync(InstancedSync&&) = default;

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
            yes_.create = MakeUnique<InstancedQueries<With<Hidden>>::Create>(
                context
                    .Query<Read<InstancedMesh>, Read<ecs::WorldTransform>, Without<InstancedHandle>, With<Hidden>>());
            yes_.all = MakeUnique<InstancedQueries<With<Hidden>>::All>(
                context.Query<Read<InstancedMesh>, Read<ecs::WorldTransform>, Read<InstancedHandle>, With<Hidden>>());
            no_.create = MakeUnique<InstancedQueries<Without<Hidden>>::Create>(
                context.Query<Read<InstancedMesh>, Read<ecs::WorldTransform>, Without<InstancedHandle>,
                              Without<Hidden>>());
            no_.all = MakeUnique<InstancedQueries<Without<Hidden>>::All>(
                context
                    .Query<Read<InstancedMesh>, Read<ecs::WorldTransform>, Read<InstancedHandle>, Without<Hidden>>());
            orphans_ = MakeUnique<InstancedOrphans>(context.Query<Read<InstancedHandle>, Without<InstancedMesh>>());
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
        const usize removed = context.RemovedCount<InstancedHandle>();
        for (usize i = 0; i < removed; ++i)
        {
            const ecs::RemovedComponent<InstancedHandle> entry = context.RemovedAt<InstancedHandle>(i);
            if (entry.seq <= last_removal_seq_)
            {
                continue;
            }
            last_removal_seq_ = entry.seq;
            Drop(*scene, entry.value.id, report);
        }
        orphans_->ForEach(
            [&](Entity entity, const InstancedHandle& handle)
            {
                Drop(*scene, handle.id, report);
                context.Commands().Remove<InstancedHandle>(entity);
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
        renderer::GpuMeshPtr mesh;
    };

    template <class Queries>
    void Sync(ecs::SystemContext& context, renderer::RenderScene& scene_ref, Queries& q, SceneSyncReport& report)
    {
        renderer::RenderScene* scene = &scene_ref;
        constexpr bool hidden = Queries::kHidden;
        q.all->ForEach(
            [&](Entity entity, const InstancedMesh& mesh, const ecs::WorldTransform& w, const InstancedHandle& handle)
            {
                const renderer::InstancedObject* current = scene->FindInstanced(handle.id);
                if (current == nullptr)
                {
                    context.Commands().Remove<InstancedHandle>(entity);  // the create query retries it
                    return;
                }
                renderer::InstancedObject want;
                if (!Make(mesh, w, origin_, hidden, &want))
                {
                    Drop(*scene, handle.id, report);
                    context.Commands().Remove<InstancedHandle>(entity);
                    ++report.invalid;
                    return;
                }
                if (!Same(*current, want))
                {
                    scene->SetInstanced(handle.id, want);
                    ++report.instanced_set;
                    Hold(handle.id, mesh.mesh);
                }
            });
        q.create->ForEach(
            [&](Entity entity, const InstancedMesh& mesh, const ecs::WorldTransform& w)
            {
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
                ++report.instanced_added;
                InstancedHandle handle;
                handle.id = id;
                Hold(id, mesh.mesh);
                context.Commands().Add<InstancedHandle>(entity, handle);
                pending_.Push(Pending{entity, id});
            });
    }

    static bool Same(const renderer::InstancedObject& a, const renderer::InstancedObject& b)
    {
        return a.set.Get() == b.set.Get() && a.mesh == b.mesh && a.material == b.material && a.flags == b.flags &&
               a.max_distance == b.max_distance && a.min_size_ratio == b.min_size_ratio &&
               a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
               a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
               a.rotation.w == b.rotation.w && a.scale == b.scale;
    }

    static bool Make(const InstancedMesh& mesh, const ecs::WorldTransform& w, const Vec3d& origin, bool hidden,
                     renderer::InstancedObject* out)
    {
        if (mesh.set.Get() == nullptr || mesh.mesh.Get() == nullptr || !mesh.mesh->Valid() ||
            !IsFinite(mesh.max_distance) || mesh.max_distance < 0.0f || !IsFinite(mesh.min_size_ratio) ||
            mesh.min_size_ratio < 0.0f)
        {
            return false;
        }
        if (!DecomposeUniform(RelativeMatrix(w, origin), &out->position, &out->rotation, &out->scale))
        {
            return false;
        }
        out->set = mesh.set;
        out->mesh = mesh.mesh.Get();
        out->material = mesh.material;
        out->flags = mesh.flags | (hidden ? renderer::ObjectFlags::kHidden : 0u);
        out->max_distance = mesh.max_distance;
        out->min_size_ratio = mesh.min_size_ratio;
        return true;
    }

    // The meshes the scene's entries draw: a removal-tracked handle cannot hold a share, so the system does.
    void Hold(renderer::InstancedId id, const renderer::GpuMeshPtr& mesh)
    {
        for (Held& h : held_)
        {
            if (h.id == id)
            {
                h.mesh = mesh;
                return;
            }
        }
        held_.Push(Held{id, mesh});
    }

    void Drop(renderer::RenderScene& scene, renderer::InstancedId id, SceneSyncReport& report)
    {
        if (id.IsValid() && scene.FindInstanced(id) != nullptr)
        {
            scene.RemoveInstanced(id);
            ++report.instanced_removed;
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

    InstancedQueries<With<Hidden>> yes_;
    InstancedQueries<Without<Hidden>> no_;
    UniquePtr<InstancedOrphans> orphans_;
    Vector<Pending> pending_;
    Vector<Held> held_;
    Vec3d origin_ = Vec3d(0, 0, 0);
    u64 world_id_ = 0;
    u64 last_removal_seq_ = 0;
};

}  // namespace

ecs::SystemDesc MakeInstancedSyncSystem(const char* name)
{
    ecs::SystemDesc desc(name, ecs::detail::SystemFn(InstancedSync()));
    return Move(desc)
        .Reads<InstancedMesh, Hidden, ecs::WorldTransform>()
        .Writes<InstancedHandle>()
        .WritesResource<SceneLink>()
        .WritesResource<SceneSyncReport>();
}

}  // namespace vse::scene
