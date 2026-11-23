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
using ecs::Without;

bool SameMatrix(const Mat4& a, const Mat4& b)
{
    for (u32 c = 0; c < 4; ++c)
    {
        for (u32 r = 0; r < 4; ++r)
        {
            if (a.col[c][r] != b.col[c][r])
            {
                return false;
            }
        }
    }
    return true;
}

bool SameLayer(const renderer::TerrainLayer& a, const renderer::TerrainLayer& b)
{
    return a.tiling == b.tiling && a.tint.x == b.tint.x && a.tint.y == b.tint.y && a.tint.z == b.tint.z &&
           a.roughness == b.roughness && a.metallic == b.metallic;
}

bool SameMaterial(const renderer::TerrainMaterial& a, const renderer::TerrainMaterial& b)
{
    if (a.layer_count != b.layer_count || a.slope_layer != b.slope_layer || a.slope_start != b.slope_start ||
        a.slope_end != b.slope_end || a.fade_start != b.fade_start || a.fade_end != b.fade_end ||
        a.layer_array.Get() != b.layer_array.Get() || a.splat.Get() != b.splat.Get())
    {
        return false;
    }
    for (u32 i = 0; i < renderer::kMaxTerrainLayers; ++i)
    {
        if (!SameLayer(a.layers[i], b.layers[i]))
        {
            return false;
        }
    }
    return true;
}

using ecs::With;

// The queries for one value of the Hidden filter (With<Hidden> or Without<Hidden>): the tag has no value to peek at.
template <class HiddenFilter> struct TerrainQueries
{
    using Create = Query<Read<Terrain>, Read<ecs::WorldTransform>, Without<TerrainHandle>, HiddenFilter>;
    using All = Query<Read<Terrain>, Read<ecs::WorldTransform>, Read<TerrainHandle>, HiddenFilter>;
    UniquePtr<Create> create;
    UniquePtr<All> all;
    static constexpr bool kHidden = IsSame<HiddenFilter, With<Hidden>>;
};
using TerrainOrphans = Query<Read<TerrainHandle>, Without<Terrain>>;

class TerrainSync
{
public:
    TerrainSync() = default;
    TerrainSync(TerrainSync&&) = default;

    void operator()(ecs::SystemContext& context)
    {
        SceneSyncReport& report = context.Resource<SceneSyncReport>();
        renderer::RenderScene* scene = context.Resource<SceneLink>().scene;
        if (scene == nullptr)
        {
            return;
        }
        if (yes_.all == nullptr || world_id_ != context.WorldId())
        {
            yes_.create = MakeUnique<TerrainQueries<With<Hidden>>::Create>(
                context.Query<Read<Terrain>, Read<ecs::WorldTransform>, Without<TerrainHandle>, With<Hidden>>());
            yes_.all = MakeUnique<TerrainQueries<With<Hidden>>::All>(
                context.Query<Read<Terrain>, Read<ecs::WorldTransform>, Read<TerrainHandle>, With<Hidden>>());
            no_.create = MakeUnique<TerrainQueries<Without<Hidden>>::Create>(
                context.Query<Read<Terrain>, Read<ecs::WorldTransform>, Without<TerrainHandle>, Without<Hidden>>());
            no_.all = MakeUnique<TerrainQueries<Without<Hidden>>::All>(
                context.Query<Read<Terrain>, Read<ecs::WorldTransform>, Read<TerrainHandle>, Without<Hidden>>());
            orphans_ = MakeUnique<TerrainOrphans>(context.Query<Read<TerrainHandle>, Without<Terrain>>());
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
        // Destroyed entities and removed handles, from the world's removal log.
        const usize removed = context.RemovedCount<TerrainHandle>();
        for (usize i = 0; i < removed; ++i)
        {
            const ecs::RemovedComponent<TerrainHandle> entry = context.RemovedAt<TerrainHandle>(i);
            if (entry.seq <= last_removal_seq_)
            {
                continue;
            }
            last_removal_seq_ = entry.seq;
            Drop(*scene, entry.value.id, report);
        }
        // Entities that lost their Terrain but kept the handle.
        orphans_->ForEach(
            [&](Entity entity, const TerrainHandle& handle)
            {
                Drop(*scene, handle.id, report);
                context.Commands().Remove<TerrainHandle>(entity);
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
        renderer::TerrainId id;
    };

    template <class Queries>
    void Sync(ecs::SystemContext& context, renderer::RenderScene& scene_ref, Queries& q, SceneSyncReport& report)
    {
        renderer::RenderScene* scene = &scene_ref;
        constexpr bool hidden = Queries::kHidden;
        // Changed terrains.
        q.all->ForEach(
            [&](Entity entity, const Terrain& terrain, const ecs::WorldTransform& w, const TerrainHandle& handle)
            {
                const renderer::TerrainObject* current = scene->FindTerrain(handle.id);
                if (current == nullptr)
                {
                    context.Commands().Remove<TerrainHandle>(entity);  // the create query retries it
                    return;
                }
                renderer::TerrainObject want;
                if (!Make(terrain, w, hidden, &want))
                {
                    Drop(*scene, handle.id, report);
                    context.Commands().Remove<TerrainHandle>(entity);
                    ++report.invalid;
                    return;
                }
                if (current->source.Get() != want.source.Get() || current->flags != want.flags ||
                    !SameMatrix(current->world, want.world) || !SameMaterial(current->material, want.material))
                {
                    scene->SetTerrain(handle.id, want);
                    ++report.terrains_set;
                }
            });
        // New terrains.
        q.create->ForEach(
            [&](Entity entity, const Terrain& terrain, const ecs::WorldTransform& w)
            {
                renderer::TerrainObject object;
                if (!Make(terrain, w, hidden, &object))
                {
                    ++report.invalid;
                    return;
                }
                const renderer::TerrainId id = scene->AddTerrain(object);
                if (!id.IsValid())
                {
                    ++report.refused_full;
                    return;
                }
                ++report.terrains_added;
                TerrainHandle handle;
                handle.id = id;
                context.Commands().Add<TerrainHandle>(entity, handle);
                pending_.Push(Pending{entity, id});
            });
    }

    static bool Make(const Terrain& terrain, const ecs::WorldTransform& w, bool hidden, renderer::TerrainObject* out)
    {
        if (!terrain.source || !terrain.source->Valid() || !renderer::TerrainWorldValid(w.matrix))
        {
            return false;
        }
        out->source = terrain.source;
        out->material = terrain.material;
        out->world = w.matrix;
        out->flags = hidden ? renderer::ObjectFlags::kHidden : 0u;
        return true;
    }

    static void Drop(renderer::RenderScene& scene, renderer::TerrainId id, SceneSyncReport& report)
    {
        if (id.IsValid() && scene.FindTerrain(id) != nullptr)
        {
            scene.RemoveTerrain(id);
            ++report.terrains_removed;
        }
    }

    TerrainQueries<With<Hidden>> yes_;
    TerrainQueries<Without<Hidden>> no_;
    UniquePtr<TerrainOrphans> orphans_;
    Vector<Pending> pending_;
    u64 world_id_ = 0;
    u64 last_removal_seq_ = 0;
};

}  // namespace

ecs::SystemDesc MakeTerrainSyncSystem(const char* name)
{
    ecs::SystemDesc desc(name, ecs::detail::SystemFn(TerrainSync()));
    return Move(desc)
        .Reads<Terrain, Hidden, ecs::WorldTransform>()
        .Writes<TerrainHandle>()
        .WritesResource<SceneLink>()
        .WritesResource<SceneSyncReport>();
}

}  // namespace vse::scene
